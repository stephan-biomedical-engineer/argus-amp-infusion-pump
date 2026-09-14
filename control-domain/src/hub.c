#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/app_version.h>
#include <zephyr/sys/reboot.h>
#include <string.h>
#if defined(CONFIG_SOC_SERIES_STM32F4X)
#include <soc.h>
#include <stm32_ll_spi.h>
#endif
#include "hub.h"
#include "markers.h"
#include "cmd.h"
#include "ota_handler.h"
#include "utl_io.h"
#include "sensor_data.h"

LOG_MODULE_REGISTER(hub, LOG_LEVEL_INF);

K_THREAD_DEFINE(hub_thread_data, HUB_THREAD_STACK_SIZE, hub_thread_entry, NULL, NULL, NULL, HUB_THREAD_PRIORITY, 0, 0);

static const struct device* spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));
static const struct gpio_dt_spec ready_pin = GPIO_DT_SPEC_GET(DT_ALIAS(spi_ready), gpios);

K_MSGQ_DEFINE(hub_cmd_q, sizeof(pump_cmd_t), 10, 4);

#if defined(CONFIG_SPI_ASYNC)
/* ---------------------------------------------------------------------
 * ARMAR ANTES DE SINALIZAR
 *
 * Com spi_transceive() (bloqueante), o periferico so e armado DENTRO da
 * chamada -- e o READY subia ANTES dela. Nessa janela o mestre ve
 * "pronto" e comeca a clocar enquanto o escravo ainda nao publicou a
 * resposta. O que sai no fio e o buffer pela metade: cabecalho valido,
 * payload incompleto e CRC zerado.
 *
 * Medido com analisador logico em 12/09/2026: 31 de 3539 respostas
 * (0,88%) saiam assim, e o gateway reportou 0,80% de falha na mesma
 * corrida. Todas foram pegas pelo CRC. Ver ANALISADOR_LOGICO.md, 3.5.
 *
 * A versao assincrona arma o periferico e retorna; so entao subimos o
 * READY. A janela deixa de existir.
 * ------------------------------------------------------------------- */
static struct k_poll_signal spi_done_sig;
#endif

// --- DEFINIÇÕES DE FRAMING (SOF) ---
#define SOF_BYTE_1 0xAA
#define SOF_BYTE_2 0x55

#define SPI_PACKET_SIZE 64                     // Tamanho do chunk físico lido do SPI
static uint8_t rx_raw_buffer[SPI_PACKET_SIZE]; // Buffer DMA
static uint8_t tx_buffer[SPI_PACKET_SIZE];     // Buffer de resposta

static parser_state_t p_state = STATE_WAIT_SOF1;
static uint8_t p_buffer[CMD_MAX_DATA_SIZE + 16]; // Buffer de montagem
static uint16_t p_index = 0;
static uint16_t p_expected_len = 0;

static int spi_consecutive_errors = 0;

/* --- Metricas do barramento (expostas por CMD_GET_METRICS) --- */
static hub_metrics_t metrics;

/* Instante em que o ultimo quadro foi rejeitado por CRC. Serve para
 * medir o tempo de recuperacao: da rejeicao ate o proximo quadro
 * valido. 0 = nao ha recuperacao pendente. */
static uint32_t crc_fail_cycle;

static const struct spi_config spi_cfg = 
{
    // Modo 0: CPOL=0, CPHA=0
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_PERIPHERAL,
    .frequency = 1000000,
    .slave = 0,
};

static pump_status_t status_cache;

// --- RESET DE HARDWARE (Auto-Cura) ---
/*
 * Duas implementacoes:
 *
 *  - STM32F4 (Blackpill): acesso direto aos registradores, identico ao
 *    firmware original. Preservado para reproduzir exatamente o
 *    comportamento ja medido em bancada.
 *
 *  - Demais SoCs (ex. STM32U5 da Nucleo): caminho portavel via API do
 *    Zephyr. O IP de SPI do U5 e o da geracao nova (TXDR/RXDR/IFCR em
 *    vez de DR/SR), entao a sequencia de registradores do F4 nem
 *    compila la.
 */
#if defined(CONFIG_SOC_SERIES_STM32F4X)

static void reset_spi_peripheral(void)
{
    SPI_TypeDef* spi_regs = (SPI_TypeDef*) DT_REG_ADDR(DT_NODELABEL(spi1));

    // Sequência de Reset Seguro
    spi_regs->CR1 &= ~SPI_CR1_SPE; // Desabilita
    k_busy_wait(100);

    // Limpa Flags
    volatile uint32_t temp;
    temp = spi_regs->DR;
    temp = spi_regs->SR;
    (void) temp;

    spi_regs->CR1 |= SPI_CR1_SPE; // Habilita
    LOG_WRN(">>> SPI HARDWARE RESET (F4, registradores) <<<");
}

#else

static void reset_spi_peripheral(void)
{
    /*
     * spi_release() aborta a transacao pendente e libera o barramento.
     * O proximo spi_transceive() do laco rearma o periferico.
     */
    int ret = spi_release(spi_dev, &spi_cfg);

    k_busy_wait(100);

    LOG_WRN(">>> SPI RESET (portavel, spi_release=%d) <<<", ret);
}

#endif

/* =====================================================================
 * Metricas
 * ===================================================================== */
void hub_get_metrics(hub_metrics_t* out)
{
    if(out)
        *out = metrics;
}

void hub_metrics_cmd_latency(uint32_t start_cycles)
{
    uint32_t us = k_cyc_to_us_near32(k_cycle_get_32() - start_cycles);

    metrics.cmd_count++;
    metrics.cmd_lat_last_us = us;
    metrics.cmd_lat_sum_us += us;
    if(us > metrics.cmd_lat_max_us)
        metrics.cmd_lat_max_us = us;
}

void hub_metrics_loop_tick(uint32_t wait_us, bool was_timeout)
{
    /* Iteracao acordada por EVENTO nao diz nada sobre a cadencia do
     * laco: ela mede o tempo ate um comando chegar, que depende do
     * gateway, nao do RTOS. Descartada aqui de proposito. */
    if(!was_timeout)
        return;

    metrics.loop_count++;
    metrics.loop_period_last_us = wait_us;

    if(metrics.loop_period_min_us == 0 || wait_us < metrics.loop_period_min_us)
        metrics.loop_period_min_us = wait_us;
    if(wait_us > metrics.loop_period_max_us)
        metrics.loop_period_max_us = wait_us;

    if(wait_us > LOOP_DEADLINE_US)
        metrics.deadline_misses++;
}

int hub_init(void)
{
    if(!device_is_ready(spi_dev))
        return -1;

    memset(tx_buffer, 0, sizeof(tx_buffer));
    memset(rx_raw_buffer, 0, sizeof(rx_raw_buffer));
    memset(&status_cache, 0, sizeof(status_cache));

    if(device_is_ready(ready_pin.port))
    {
        gpio_pin_configure_dt(&ready_pin, GPIO_OUTPUT_INACTIVE);
    }
    return 0;
}

static void fill_status_payload(cmd_status_payload_t* payload)
{
    payload->current_state = (uint8_t) status_cache.current_state;
    payload->volume = (uint32_t) status_cache.infused_volume;
    payload->flow_rate_set = status_cache.configured_flow_rate;
    payload->pressure = status_cache.pressure_mmhg;
    payload->alarm_active = (status_cache.current_state >= STATE_ALARM_BUBBLE);
}

void hub_set_status(const pump_status_t* status)
{
    status_cache = *status;
}
int hub_get_command(pump_cmd_t* cmd)
{
    return k_msgq_get(&hub_cmd_q, cmd, K_NO_WAIT);
}

// --- PROCESSADOR DE PACOTE VÁLIDO ---
static void process_valid_packet(uint8_t* buffer, size_t len)
{
    // Zera erros pois tivemos sucesso
    if(spi_consecutive_errors > 0)
    {
        LOG_INF("SPI Recuperado! Erros zerados.");
        spi_consecutive_errors = 0;
    }

    uint8_t src, dst;
    cmd_ids_t req_id, res_id;
    cmd_cmds_t req_data, res_data;
    size_t tx_len = 0;

    metrics.frames_seen++;

    memset(&res_data, 0, sizeof(res_data));

    // O 'buffer' aqui começa no byte 0 (que agora é SOF1 0xAA) graças à lógica do parser.
    bool decode_success = cmd_decode(buffer, len, &src, &dst, &req_id, &req_data);

    if(!decode_success)
    {
        /* ------------------------------------------------------------
         * Quadro rejeitado: CRC invalido ou estrutura corrompida.
         *
         * Antes isto so registrava um aviso e retornava -- nao contava
         * erro, nao acionava o auto-healing e nao respondia nada ao
         * gateway. Ou seja, o encadeamento "rejeicao por CRC dispara o
         * hot reset do periferico", que a Fig. 4 do artigo descreve,
         * nao existia: eram dois caminhos disjuntos.
         *
         * Agora a rejeicao alimenta o MESMO contador do erro de driver
         * e dispara o mesmo reset, e o gateway recebe NACK em vez de
         * silencio (que ele so perceberia por timeout).
         * ---------------------------------------------------------- */
        metrics.crc_errors++;
        metrics.last_crc_fail_us = k_cyc_to_us_near32(k_cycle_get_32());
        crc_fail_cycle = k_cycle_get_32();

        spi_consecutive_errors++;
        LOG_WRN("Quadro rejeitado (CRC/estrutura). Erros consecutivos: %d", spi_consecutive_errors);

        reset_spi_peripheral();
        metrics.spi_resets++;

        /* NACK: nao sabemos o req_id (o decode falhou), entao respondemos
         * um ACTION_RES generico com status de checksum. */
        {
            uint8_t nack_dst = ADDR_MASTER;
            uint8_t nack_src = ADDR_SLAVE;
            cmd_ids_t nack_id = CMD_ACTION_RES_ID;
            cmd_cmds_t nack_data;
            size_t nack_len = 0;

            memset(&nack_data, 0, sizeof(nack_data));
            nack_data.action_res.cmd_req_id = 0;
            nack_data.action_res.status = CMD_ERR_CHECKSUM;
            cmd_encode(tx_buffer, &nack_len, &nack_dst, &nack_src, &nack_id, &nack_data);
        }
        return;
    }

    /* Marcador A: sobe no instante em que o quadro foi decodificado com
     * sucesso. Desce no rearme do SPI, no laco do hub. */
    argus_marker_a_set(1);

    /* Quadro valido. Se havia rejeicao pendente, fecha a janela de
     * recuperacao: este e o instante em que a comunicacao voltou. */
    if(crc_fail_cycle != 0)
    {
        uint32_t delta = k_cycle_get_32() - crc_fail_cycle;
        metrics.last_recovery_us = k_cyc_to_us_near32(delta);
        if(metrics.last_recovery_us > metrics.max_recovery_us)
            metrics.max_recovery_us = metrics.last_recovery_us;
        metrics.recoveries++;
        crc_fail_cycle = 0;
        LOG_INF("Comunicacao recuperada em %u us", metrics.last_recovery_us);
    }

    pump_cmd_t internal_cmd = 
    {
        .id = CMD_NONE, 
        .param = 0.0f,
        .param2 = 0.0f,
    };

    switch(req_id)
    {
    case CMD_GET_STATUS_REQ_ID:
        res_id = CMD_GET_STATUS_RES_ID;
        fill_status_payload(&res_data.status_res.status_data);
        break;

    case CMD_GET_METRICS_REQ_ID:
    {
        /* Copia a struct crua no payload. O gateway a interpreta com o
         * mesmo hub_metrics_t -- por isso ela e packed no header. */
        res_id = CMD_GET_METRICS_RES_ID;

        /* hub_metrics_t (interno, com uint64) -> payload de transporte
         * (so uint32). A media e calculada aqui, uma vez, em vez de
         * carregar a soma no barramento. */
        res_data.metrics_res.m.frames_seen        = metrics.frames_seen;
        res_data.metrics_res.m.crc_errors         = metrics.crc_errors;
        res_data.metrics_res.m.driver_errors      = metrics.driver_errors;
        res_data.metrics_res.m.spi_resets         = metrics.spi_resets;
        res_data.metrics_res.m.recoveries         = metrics.recoveries;
        res_data.metrics_res.m.max_recovery_us    = metrics.max_recovery_us;
        res_data.metrics_res.m.cmd_count          = metrics.cmd_count;
        res_data.metrics_res.m.cmd_lat_max_us     = metrics.cmd_lat_max_us;
        res_data.metrics_res.m.cmd_lat_avg_us     =
            metrics.cmd_count ? (uint32_t) (metrics.cmd_lat_sum_us / metrics.cmd_count) : 0U;
        res_data.metrics_res.m.loop_count         = metrics.loop_count;
        res_data.metrics_res.m.loop_period_min_us = metrics.loop_period_min_us;
        res_data.metrics_res.m.loop_period_max_us = metrics.loop_period_max_us;
        res_data.metrics_res.m.deadline_misses    = metrics.deadline_misses;

        /* ------------------------------------------------------------
         * LEITURA DESTRUTIVA: zera os acumuladores.
         *
         * Sem isto, toda metrica e um acumulador desde o boot, e a
         * campanha nao consegue atribuir nada a um cenario especifico:
         * o "combined" carregaria os erros do "baseline", do "cpu", do
         * "io" e do "net" somados. Observado em bancada -- frames_seen
         * ia de 661 para 1211 e 1762 entre repeticoes do MESMO cenario.
         *
         * Zerando aqui, cada leitura devolve o INTERVALO desde a leitura
         * anterior, que e o que a campanha precisa. Os extremos
         * (min/max de periodo, WCET, pior recuperacao) passam a ser do
         * intervalo, nao do tempo de vida do firmware.
         *
         * O min volta a zero de proposito: zero significa "ainda sem
         * amostra" na comparacao de loop_period_min_us.
         * ---------------------------------------------------------- */
        memset(&metrics, 0, sizeof(metrics));
        break;
    }

    case CMD_VERSION_REQ_ID:
        res_id = CMD_VERSION_RES_ID;
        res_data.version_res.major = APP_VERSION_MAJOR;
        res_data.version_res.minor = APP_VERSION_MINOR;
        res_data.version_res.patch = APP_PATCHLEVEL;
        break;

    case CMD_SET_CONFIG_REQ_ID:
    {
        /* ----------------------------------------------------------------
         * SET_CONFIG e composto: vira tres comandos internos. Antes ele
         * respondia CMD_OK INCONDICIONALMENTE, sem esperar o
         * logic_engine processar -- entao um pedido rejeitado por faixa
         * terapeutica era reportado como aceito. Rejeitar em silencio e
         * pior que nao rejeitar: o gateway segue acreditando que a bomba
         * esta na vazao que ele pediu.
         *
         * Agora cada subcomando e enviado e aguardado; o primeiro que
         * falhar aborta o conjunto e determina o status devolvido.
         * -------------------------------------------------------------- */
        static const struct
        {
            command_id_t id;
            const char* nome;
        } passos[] = {
            {CMD_SET_RATE, "vazao"},
            {CMD_SET_VOLUME, "volume"},
            {CMD_SET_DIAMETER, "diametro"},
        };

        float valores[3];
        valores[0] = (float) req_data.config_req.config.flow_rate;
        valores[1] = (float) req_data.config_req.config.volume;
        valores[2] = (float) req_data.config_req.config.diameter;

        uint8_t cfg_status = CMD_OK;

        for(int i = 0; i < 3; i++)
        {
            internal_cmd.id = passos[i].id;
            internal_cmd.param = valores[i];

            k_sem_reset(&logic_cmd_sem);
            k_msgq_put(&hub_cmd_q, &internal_cmd, K_NO_WAIT);

            if(k_sem_take(&logic_cmd_sem, K_MSEC(50)) != 0)
            {
                LOG_ERR("SET_CONFIG: timeout no passo '%s'", passos[i].nome);
                cfg_status = CMD_ERR_TIMEOUT;
                break;
            }
            if(logic_last_result != LOGIC_OK)
            {
                LOG_ERR("SET_CONFIG: passo '%s' rejeitado pela faixa", passos[i].nome);
                cfg_status = CMD_ERR_PARAM_RANGE;
                break;
            }
        }

        res_id = CMD_SET_CONFIG_RES_ID;
        res_data.config_res.status = cfg_status;
        break;
    }

    case CMD_ACTION_BOLUS_REQ_ID:
    {
        internal_cmd.id = CMD_SET_BOLUS;
        
        // [CORREÇÃO 1] Acesso correto via .payload
        internal_cmd.param  = (float)req_data.bolus_req.payload.bolus_rate;   
        internal_cmd.param2 = (float)req_data.bolus_req.payload.bolus_volume; 

        // [CORREÇÃO 2] Lógica de Sincronia Obrigatória
        k_sem_reset(&logic_cmd_sem); // Limpa sinal antigo

        k_msgq_put(&hub_cmd_q, &internal_cmd, K_NO_WAIT); // Envia

        // Espera o logic_engine processar e responder
        if (k_sem_take(&logic_cmd_sem, K_MSEC(50)) == 0)
        {
            if (logic_last_cmd != internal_cmd.id)
                res_data.action_res.status = CMD_ERR_SYNC;
            else if (logic_last_result == LOGIC_OK)
                res_data.action_res.status = CMD_OK;
            else
                res_data.action_res.status = CMD_ERR_INVALID_STATE; 
        }
        else
        {
            res_data.action_res.status = CMD_ERR_TIMEOUT;
        }

        res_id = CMD_ACTION_RES_ID;
        res_data.action_res.cmd_req_id = req_id;
        break;
    }

    // --- LÓGICA OTA RESTAURADA E CORRIGIDA PARA HEADER V2 ---
    case CMD_OTA_START_REQ_ID: 
    {
        // Payload começa no byte 7 (Header Size)
        // Estrutura: [Size (4 bytes)]
        uint32_t size = utl_io_get32_fl(&buffer[CMD_HDR_SIZE]);

        LOG_INF("Comando OTA START Recebido. Tamanho: %d", size);
        ota_start(size);

        res_id = CMD_OTA_RES_ID;
        res_data.action_res.cmd_req_id = req_id;
        res_data.action_res.status = CMD_OK;
        break;
    }

    case CMD_OTA_CHUNK_REQ_ID: 
    {
        // Payload começa no byte 7
        // Estrutura Chunk: [Offset (4)] + [Len (1)] + [Data...]
        uint8_t* payload_ptr = &buffer[CMD_HDR_SIZE];

        // Offset está nos primeiros 4 bytes (indices 0-3 do payload)
        // uint32_t offset = utl_io_get32_fl(payload_ptr); // Não usamos aqui, mas existe

        // Len está no byte 4 do payload
        uint8_t chunk_len = payload_ptr[4];

        // Data começa no byte 5 do payload
        uint8_t* data_ptr = &payload_ptr[5];

        if(ota_write_chunk(data_ptr, chunk_len) == 0)
        {
            res_data.action_res.status = CMD_OK;
        }
        else
        {
            res_data.action_res.status = CMD_ERR_INVALID_STATE;
        }
        res_id = CMD_OTA_RES_ID;
        res_data.action_res.cmd_req_id = req_id;
        break;
    }

    case CMD_OTA_END_REQ_ID: 
    {
        LOG_INF("Comando OTA END Recebido.");
        res_id = CMD_OTA_RES_ID;
        res_data.action_res.cmd_req_id = req_id;
        res_data.action_res.status = CMD_OK;
        ota_finish();
        break;
    }
        // ----------------------------------------------------

    default:
        res_id = CMD_ACTION_RES_ID;
        res_data.action_res.status = CMD_OK;
        res_data.action_res.cmd_req_id = req_id;

        if(req_id >= CMD_ACTION_RUN_REQ_ID && req_id <= CMD_ACTION_BOLUS_REQ_ID)
        {
            switch(req_id)
            {
            case CMD_ACTION_RUN_REQ_ID:
                internal_cmd.id = CMD_START;
                break;
            case CMD_ACTION_PAUSE_REQ_ID:
                internal_cmd.id = CMD_PAUSE;
                break;
            case CMD_ACTION_ABORT_REQ_ID:
                internal_cmd.id = CMD_STOP;
                break;
            // case CMD_ACTION_BOLUS_REQ_ID:
            //     internal_cmd.id = CMD_SET_BOLUS;
            //     break;
            case CMD_ACTION_PURGE_REQ_ID:
                internal_cmd.id = CMD_SET_PURGE;
                break;
            default:
                internal_cmd.id = CMD_NONE;
                break;
            }
            if (internal_cmd.id != CMD_NONE)
            {
                internal_cmd.param = 0.0f;

                k_sem_reset(&logic_cmd_sem); 

                k_msgq_put(&hub_cmd_q, &internal_cmd, K_NO_WAIT);

                // Agora o take vai bloquear de verdade até o logic_engine processar
                if (k_sem_take(&logic_cmd_sem, K_MSEC(50)) == 0)
                {
                    // Verifica se o comando processado foi o que pedimos
                    if (logic_last_cmd != internal_cmd.id)
                    {
                        res_data.action_res.status = CMD_ERR_SYNC;
                    }
                    else if (logic_last_result == LOGIC_OK)
                    {
                        res_data.action_res.status = CMD_OK;
                    }
                    else
                    {
                        // Aqui ele vai pegar corretamente o LOGIC_ERR_INVALID_STATE
                        res_data.action_res.status = CMD_ERR_INVALID_STATE;
                    }
                }
                else
                {
                    res_data.action_res.status = CMD_ERR_TIMEOUT;
                }
            }

        }
        break;
    }

    // Prepara a resposta para a PRÓXIMA transação SPI
    cmd_encode(tx_buffer, &tx_len, &dst, &src, &res_id, &res_data);
}

// --- MÁQUINA DE ESTADOS (FEEDER) ---
void protocol_feed_byte(uint8_t byte)
{
    switch(p_state)
    {
    case STATE_WAIT_SOF1:
        if(byte == SOF_BYTE_1)
            p_state = STATE_WAIT_SOF2;
        break;

    case STATE_WAIT_SOF2:
        if(byte == SOF_BYTE_2)
        {
            p_state = STATE_READ_HEADER;
            p_index = 0;
            // Salva o SOF no buffer para validação do CRC depois
            p_buffer[p_index++] = SOF_BYTE_1;
            p_buffer[p_index++] = SOF_BYTE_2;
        }
        else if(byte == SOF_BYTE_1)
        {
            p_state = STATE_WAIT_SOF2; // Sequência AA AA...
        }
        else
        {
            p_state = STATE_WAIT_SOF1; // Lixo
        }
        break;

    case STATE_READ_HEADER:
        p_buffer[p_index++] = byte;
        // Header(7 bytes) = SOF1+SOF2 + 5 bytes restantes
        if(p_index >= CMD_HDR_SIZE)
        {
            // Extrai o tamanho esperado do payload
            // O campo size está nos últimos 2 bytes do header (indices 5 e 6)
            p_expected_len = utl_io_get16_fl(&p_buffer[5]);

            if(p_expected_len > CMD_MAX_DATA_SIZE)
            {
                p_state = STATE_WAIT_SOF1; // Tamanho inválido
            }
            else
            {
                p_state = STATE_READ_PAYLOAD;
            }
        }
        break;

    case STATE_READ_PAYLOAD:
        // Lê bytes de Payload + CRC
        if(p_expected_len > 0 || p_index < (CMD_HDR_SIZE + CMD_TRAILER_SIZE))
        {
            p_buffer[p_index++] = byte;
        }

        // Verifica se completou o frame (Header + Payload + CRC)
        if(p_index >= (CMD_HDR_SIZE + p_expected_len + CMD_TRAILER_SIZE))
        {
            process_valid_packet(p_buffer, p_index);
            p_state = STATE_WAIT_SOF1; // Volta a caçar
        }
        break;

    default:
        p_state = STATE_WAIT_SOF1;
        break;
    }
}

void hub_thread_entry(void* p1, void* p2, void* p3)
{
    LOG_INF("Hub Parser (Stream Mode) Iniciado.");

#if defined(CONFIG_SPI_ASYNC)
    k_poll_signal_init(&spi_done_sig);
#endif

    while(1)
    {
        struct spi_buf rx_buf = {.buf = rx_raw_buffer, .len = SPI_PACKET_SIZE};
        struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

        struct spi_buf tx_buf_s = {.buf = tx_buffer, .len = SPI_PACKET_SIZE};
        struct spi_buf_set tx_set = {.buffers = &tx_buf_s, .count = 1};

        /* Fecha a janela do quadro anterior antes de anunciar que estamos
         * prontos para o proximo. */
        argus_marker_a_set(0);

        int ret;

#if defined(CONFIG_SPI_ASYNC)
        k_poll_signal_reset(&spi_done_sig);
        ret = spi_transceive_signal(spi_dev, &spi_cfg, &tx_set, &rx_set, &spi_done_sig);

        if(ret == 0)
        {
            /* Periferico armado: so agora o mestre pode comecar. */
            gpio_pin_set_dt(&ready_pin, 1);

            struct k_poll_event evt = K_POLL_EVENT_INITIALIZER(
                K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &spi_done_sig);

            /* Espera indefinida, como na versao bloqueante: em modo
             * escravo quem decide quando a transferencia acontece e o
             * mestre. */
            (void) k_poll(&evt, 1, K_FOREVER);
            gpio_pin_set_dt(&ready_pin, 0);

            unsigned int sinalizado = 0;
            int resultado = 0;
            k_poll_signal_check(&spi_done_sig, &sinalizado, &resultado);
            ret = sinalizado ? resultado : -EIO;
        }
        else
        {
            /* Nao armou: nem chega a anunciar "pronto". */
            gpio_pin_set_dt(&ready_pin, 0);
        }
#else
        gpio_pin_set_dt(&ready_pin, 1);
        ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
        gpio_pin_set_dt(&ready_pin, 0);
#endif

        // --- TRATAMENTO DE ERRO FÍSICO ---
        if(ret < 0)
        {
            LOG_ERR("Erro SPI Driver: %d", ret);
            spi_consecutive_errors++;
            metrics.driver_errors++;

            // Tenta curar
            reset_spi_peripheral();
            metrics.spi_resets++;

            if(spi_consecutive_errors > 10) // 10 erros seguidos = Morte
            {
                LOG_ERR("FALHA CRITICA: Reiniciando Sistema...");
                k_sleep(K_MSEC(200));
                sys_reboot(SYS_REBOOT_COLD);
            }
            k_sleep(K_MSEC(10));
            continue;
        }

        // --- ALIMENTA O PARSER ---
        for(int i = 0; i < SPI_PACKET_SIZE; i++)
        {
            protocol_feed_byte(rx_raw_buffer[i]);
        }

        memset(rx_raw_buffer, 0, SPI_PACKET_SIZE);
        /*
         * ota_check_and_reboot() removido: o reboot pos-OTA e agendado
         * por k_work_schedule() dentro de ota_finish(), na workqueue do
         * sistema. A funcao antiga estava declarada e chamada mas nunca
         * definida -- link error.
         */
    }
}
