#include <zephyr/kernel.h>
#include <errno.h>
#include <zephyr/logging/log.h>
#include "sensor_data.h"
#include "encoder.h"
#include "motor_driver.h"
#include "cmd.h"
#include "hub.h"

#include "markers.h"
LOG_MODULE_REGISTER(logic_engine, LOG_LEVEL_INF);

/* Definição das Filas */
// K_MSGQ_DEFINE(cmd_queue, sizeof(pump_cmd_t), 10, 4);
extern struct k_msgq hub_cmd_q;
extern struct k_msgq sensor_data_q;
volatile command_id_t logic_last_cmd;
volatile logic_result_t logic_last_result;
struct k_sem logic_cmd_sem;

/* Estado Global da Aplicação (ÚNICA FONTE DE VERDADE) */
static pump_status_t global_status = 
{
    .current_state = STATE_IDLE,
    .infused_volume = 0.0f,
    .target_volume = 100.0f,
    .configured_flow_rate = 0,
    .pressure_mmhg = 0,
    .syringe_diameter = 20
};

/* Calibração: ml por grau do encoder (Exemplo) */
#define ML_PER_DEGREE 0.005f

static bool test_mode_warned = false;

/* Adicione no topo do logic_engine.c */
#define RATE_PURGE_DEFAULT 1200 // ml/h máxima
#define RATE_BOLUS_DEFAULT 600  // ml/h padrão de bolus

/* ---------------------------------------------------------------------
 * Validacao de faixa terapeutica
 * ---------------------------------------------------------------------
 * Chamada em TODO caminho que altera a vazao. Devolve false quando o
 * pedido esta fora da faixa; o chamador entao rejeita e responde
 * CMD_ERR_PARAM_RANGE, em vez de saturar em silencio.
 *
 * Rejeitar e melhor que saturar: saturar faria o gateway acreditar que
 * o comando foi obedecido. O operador precisa saber que o pedido foi
 * recusado, nao receber uma vazao diferente da que pediu sem aviso.
 * ------------------------------------------------------------------- */
static bool rate_is_valid(uint32_t rate_ml_h)
{
    return (rate_ml_h >= RATE_MIN_ML_H) && (rate_ml_h <= RATE_MAX_ML_H);
}

/* Precisamos saber qual vazão aplicar dependendo do estado */
uint32_t get_target_rate(pump_status_t* status)
{
    switch(status->current_state)
    {
    case STATE_RUNNING:
        return status->configured_flow_rate;
    case STATE_BOLUS:
        return RATE_BOLUS_DEFAULT;
    case STATE_PURGE:
        return RATE_PURGE_DEFAULT;
    default:
        return 0;
    }
}

void update_motor_hardware(pump_status_t* status)
{
    /* Marcador B: alto enquanto a atuacao executa. A largura do pulso inclui
     * os LOG_INF abaixo, que fazem parte do caminho como ele existe hoje. */
    argus_marker_b_set(1);

    // Verifica se é QUALQUER estado de movimento
    if(status->current_state == STATE_RUNNING || status->current_state == STATE_BOLUS ||
       status->current_state == STATE_PURGE)
    {
        motor_enable(true);

        // Pega a vazão correta para o estado atual
        uint32_t rate = get_target_rate(status);

        motor_run(rate, status->syringe_diameter);

        LOG_INF("Motor ON: Estado=%d, Vazao=%d ml/h", status->current_state, rate);
    }
    else
    {
        motor_stop();
        motor_enable(false);
        LOG_INF("Motor OFF: Estado=%d", status->current_state);
    }

    argus_marker_b_set(0);
}

void logic_thread_entry(void* p1, void* p2, void* p3)
{
    struct k_poll_event events[2];
    pump_cmd_t cmd;
    sensor_packet_t sensor;

    /* Inicializa Hardware Específico */
    encoder_init();
    motor_init();

    LOG_INF("Logic Engine Iniciada. Aguardando comandos...");

    /* Configura eventos de poll */
    k_poll_event_init(&events[0], K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &hub_cmd_q);

    k_poll_event_init(&events[1], K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &sensor_data_q);

    k_sem_init(&logic_cmd_sem, 0, 1);

    while(1)
    {
        /* Espera por algo acontecer (Comando ou Sensor) */
        const uint32_t poll_t0 = k_cycle_get_32();
        const int poll_rc = k_poll(events, 2, K_MSEC(100));
        const uint32_t poll_wait_us = k_cyc_to_us_near32(k_cycle_get_32() - poll_t0);

        /* Cadencia do laco.
         *
         * k_poll devolve -EAGAIN quando o prazo esgota sem evento algum;
         * 0 quando algum evento o acordou antes. So o primeiro caso
         * mede a cadencia -- o segundo mede o tempo ate um comando
         * chegar, que depende do gateway e nao do RTOS.
         *
         * O que registramos e o tempo que o k_poll BLOQUEOU de fato:
         * pedimos 100 ms, e o desvio em relacao a isso e o jitter do
         * temporizador sob carga. Acima de LOOP_DEADLINE_US conta como
         * deadline miss. */
        hub_metrics_loop_tick(poll_wait_us, (poll_rc == -EAGAIN));

        /* --- 1. Processamento de Comandos (Vindo do SPI/Hub) --- */
        if(events[0].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE)
        {
            k_msgq_get(&hub_cmd_q, &cmd, K_NO_WAIT);

            /* Marca o inicio do caminho comando -> atuacao. Fecha depois
             * de update_motor_hardware(), que e onde o PWM e de fato
             * reprogramado. */
            const uint32_t cmd_t0 = k_cycle_get_32();

            logic_ack_t ack = 
            {
                .cmd_id = cmd.id,
                .result = LOGIC_OK
            };

            LOG_INF("CMD Proc: ID=%d Param=%d Param2=%d", cmd.id, (int) cmd.param, (int) cmd.param2);

            bool state_changed = false;

            switch(cmd.id)
            {
            case CMD_START:
                if(global_status.current_state == STATE_IDLE || global_status.current_state == STATE_PAUSED)
                {
                    global_status.current_state = STATE_RUNNING;
                    state_changed = true;
                }
                // CASO 2: Redundância (Já está em BOLUS ou PURGE)
                // Explicação: O comando anterior (CMD_SET_BOLUS) já ligou o motor.
                // O "Start" que chegou agora é apenas uma confirmação do MQTT. 
                // A gente responde OK, mas NÃO muda o estado para RUNNING (senão cancelaria o Bolus).
                else if(global_status.current_state == STATE_BOLUS || global_status.current_state == STATE_PURGE)
                {
                    ack.result = LOGIC_OK; // Responde ACK (Sucesso)
                    global_status.current_state = STATE_RUNNING;
                    state_changed = false; // Não mexe no motor, deixa o Bolus continuar
                }
                // CASO 3: Erro (Ex: Já está em RUNNING ou em ALARME)
                else if (global_status.current_state == STATE_RUNNING)
                {
                     ack.result = LOGIC_OK; // Se já está em RUNNING, start é inócuo (Idempotência)
                }
                else
                {
                    ack.result = LOGIC_ERR_INVALID_STATE;
                }
                break;

            case CMD_PAUSE:
                // PAUSE deve funcionar em QUALQUER estado de movimento
                if(global_status.current_state == STATE_RUNNING || global_status.current_state == STATE_BOLUS ||
                   global_status.current_state == STATE_PURGE)
                {
                    global_status.current_state = STATE_PAUSED;
                    state_changed = true;
                }
                else
                {
                    ack.result = LOGIC_ERR_INVALID_STATE;
                }
                break;

            case CMD_STOP:
                global_status.current_state = STATE_IDLE;
                global_status.infused_volume = 0.0f; // Reseta volume
                global_status.configured_flow_rate = 0; // Reseta vazão
                global_status.target_volume = 0.0f; // Reseta alvo
                state_changed = true;
                break;

            // --- NOVOS CASES (Isso faltava!) ---
            case CMD_SET_BOLUS:
                if(global_status.current_state == STATE_IDLE || global_status.current_state == STATE_PAUSED)
                {
                    global_status.current_state = STATE_BOLUS;
                    {
                        const uint32_t pedido_bolus =
                            (cmd.param > 0.0f) ? (uint32_t) cmd.param : (uint32_t) RATE_BOLUS_DEFAULT;

                        if(!rate_is_valid(pedido_bolus))
                        {
                            LOG_ERR("REJEITADO: bolus %u mL/h fora da faixa [%u..%u]",
                                    pedido_bolus, RATE_MIN_ML_H, RATE_MAX_ML_H);
                            global_status.current_state = STATE_IDLE;
                            ack.result = LOGIC_ERR_NOT_ALLOWED;
                            break;
                        }
                        global_status.configured_flow_rate = pedido_bolus;
                    }
                    global_status.target_volume = cmd.param2; 
                    LOG_INF("Iniciando BOLUS de %d ml/h", global_status.configured_flow_rate);
                    state_changed = true;
                }
                else
                {
                    ack.result = LOGIC_ERR_INVALID_STATE;
                }
                break;

            case CMD_SET_PURGE:
                if(global_status.current_state == STATE_IDLE || global_status.current_state == STATE_PAUSED)
                {
                    global_status.current_state = STATE_PURGE;
                    global_status.configured_flow_rate = (uint32_t) RATE_PURGE_DEFAULT;
                    LOG_INF("Iniciando PURGE de %d ml/h", RATE_PURGE_DEFAULT);
                    state_changed = true;
                }
                else
                {
                    ack.result = LOGIC_ERR_INVALID_STATE;
                }
                break;
                // -----------------------------------

            case CMD_SET_RATE:
            {
                const uint32_t pedido = (uint32_t) cmd.param;

                if(!rate_is_valid(pedido))
                {
                    /* AUTORIDADE FINAL DO MCU. O gateway pode estar
                     * comprometido; a vazao pedida nao vira atuacao. */
                    LOG_ERR("REJEITADO: vazao %u mL/h fora da faixa [%u..%u]",
                            pedido, RATE_MIN_ML_H, RATE_MAX_ML_H);
                    ack.result = LOGIC_ERR_NOT_ALLOWED;
                    break;
                }

                global_status.configured_flow_rate = pedido;
                LOG_INF("Vazão RUN configurada para: %d", global_status.configured_flow_rate);
                if(global_status.current_state == STATE_RUNNING)
                    state_changed = true;
                break;
            }

            case CMD_SET_VOLUME:
                if(cmd.param < 0.0f || cmd.param > VOLUME_MAX_ML)
                {
                    LOG_ERR("REJEITADO: volume %d mL fora da faixa [0..%d]",
                            (int) cmd.param, (int) VOLUME_MAX_ML);
                    ack.result = LOGIC_ERR_NOT_ALLOWED;
                    break;
                }
                global_status.target_volume = (float) cmd.param;
                break;

            case CMD_SET_DIAMETER:
            {
                /* Diametro entra no calculo da area da seringa em
                 * motor_run(): zero causaria divisao por zero, e um
                 * valor absurdo distorce a vazao real sem alarme. */
                const uint32_t d = (uint32_t) cmd.param;
                if(d < SYRINGE_DIAM_MIN_MM || d > SYRINGE_DIAM_MAX_MM)
                {
                    LOG_ERR("REJEITADO: diametro %u mm fora da faixa [%u..%u]",
                            d, SYRINGE_DIAM_MIN_MM, SYRINGE_DIAM_MAX_MM);
                    ack.result = LOGIC_ERR_NOT_ALLOWED;
                    break;
                }
                global_status.syringe_diameter = (uint8_t) d;
                break;
            }
            // case CMD_SET_MODE:
            //     global_status.infusion_mode = (uint8_t) cmd.param;
            //     break;
            default:
                LOG_WRN("Comando desconhecido ou não tratado: %d", cmd.id);
                break;
            }

            events[0].state = K_POLL_STATE_NOT_READY;

            // k_msgq_put(&logic_ack_q, &ack, K_NO_WAIT);

            logic_last_cmd = cmd.id;
            logic_last_result = ack.result;
            k_sem_give(&logic_cmd_sem);

            // Só chama o driver se houve mudança relevante
            if(state_changed)
            {
                LOG_INF("Status changed from id: %d", logic_last_cmd);
                update_motor_hardware(&global_status);
            }

            /* Fecha a janela de latencia comando -> atuacao. */
            hub_metrics_cmd_latency(cmd_t0);
        }

        /* --- 2. Processamento de Sensores Analógicos (Bolha/Oclusão) --- */
        if(events[1].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE)
        {
            k_msgq_get(&sensor_data_q, &sensor, K_NO_WAIT);

            /* --- MODO DE TESTE (Bypass de Hardware) --- */
            /* Sobrescrevemos os valores lidos com valores fixos seguros */
            sensor.bolha_mv = 3000; // > 2000 (Sem bolha)
            sensor.oclusao_mv = 0;  // 0 pressão

            if(!test_mode_warned)
            {
                LOG_WRN("MODO TESTE ATIVO: Sensores simulados via software (Log único para evitar spam)");
                test_mode_warned = true;
            }

            // Atualiza pressão baseada no sensor de oclusão (calibração necessária)
            global_status.pressure_mmhg = sensor.oclusao_mv / 10;

            // Lógica de Segurança
            if(sensor.bolha_mv < 2000 && global_status.current_state == STATE_RUNNING)
            {
                // --- PONTO EXATO DO GATILHO (DUMP) ---
                LOG_ERR("!!! ALARME BOLHA DISPARADO !!!");
                LOG_INF("DUMP -> Leitura Sensor: %d mV", sensor.bolha_mv);
                LOG_INF("DUMP -> Estado Anterior: %d", global_status.current_state);
                LOG_INF("DUMP -> Volume Infundido: %d.%d", (int) global_status.infused_volume,
                        (int) (global_status.infused_volume * 10) % 10);
                // -------------------------------------

                global_status.current_state = STATE_ALARM_BUBBLE;
                update_motor_hardware(&global_status);
            }

            events[1].state = K_POLL_STATE_NOT_READY;
        }

        int32_t delta = encoder_get_delta();
        // LOG_INF("Delta Encoder: %d", delta);

        if(delta != 0)
        {
            LOG_INF("ENCODER DETECTADO: Delta=%d, RateAtual=%d", delta, global_status.configured_flow_rate);
            
            /* CENÁRIO A: O Encoder é um botão de ajuste (Knob) no painel */
            if(global_status.current_state == STATE_IDLE || global_status.current_state == STATE_PAUSED)
            {
                global_status.configured_flow_rate += delta;
                LOG_INF("Ajuste de Vazão: %d ml/h", global_status.configured_flow_rate);
            }

            /* CENÁRIO B: O Encoder está no motor (Feedback de movimento) */
            /* Se o motor girou, calculamos o volume infundido real */
            if(global_status.current_state == STATE_RUNNING ||
               global_status.current_state == STATE_BOLUS ||
               global_status.current_state == STATE_PURGE)
            {
                float vol_increment = (float) delta * ML_PER_DEGREE;
                // Só soma se for positivo (sentido da infusão)
                if(vol_increment > 0)
                {
                    global_status.infused_volume += vol_increment;
                }

                // Verifica se atingiu o alvo (VTBI - Volume To Be Infused)
                if(global_status.infused_volume >= global_status.target_volume)
                {
                    LOG_INF("Volume Alvo Atingido!");
                    global_status.current_state = STATE_END_INFUSION;
                    update_motor_hardware(&global_status); // Para o motor
                }
            }
        }

        /* --- 4. Atualizar o Hub SPI --- */
        // Envia o estado atualizado para que o Hub possa responder ao próximo poll do Mestre
        hub_set_status(&global_status);
    }
}

K_THREAD_DEFINE(logic_tid, 2048, logic_thread_entry, NULL, NULL, NULL, 3, 0, 0);
