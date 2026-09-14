// ===========================================================================
// argus_bench — harness de medicao do barramento SPI gateway <-> MCU
// ===========================================================================
// Existe para responder ao que os dois revisores pediram: metricas
// QUANTITATIVAS, com cenario e numero de repeticoes declarados.
//
// Mede, por transacao:
//   - latencia ponta a ponta de send_command() (encode, handshake READY,
//     transferencia, resposta, validacao de CRC)
//   - sucesso/falha, para taxa de erro e tempo de recuperacao
//
// Emite CSV cru, uma linha por transacao. Nada de media calculada aqui:
// agregacao e trabalho do analisador, e guardar o dado bruto permite
// recalcular percentis depois sem repetir a bancada.
//
// Uso:
//   argus_bench -n 2000 -o repouso.csv --label repouso
//   argus_bench -n 2000 -o ddos.csv    --label ddos --interval-us 0
//
// IMPORTANTE: pare o servico antes, os dois disputam /dev/spidev0.1:
//   systemctl stop infusion-pump
// ===========================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include <csignal>
#include <unistd.h>

#include "hal_spi.hpp"
#include "hal_gpio.hpp"
#include "stm32_bridge.hpp"

extern "C"
{
#include "cmd.h"
}

#include "spi_config.hpp"

// --- Mesmos parametros do system/main.cpp ---
// A velocidade do SPI vem de spi_config.hpp -- a mesma do servico -- e so
// muda com --spi-hz, para varreduras.
static const char* SPI_NODE = "/dev/spidev0.1";
static const unsigned READY_GPIO = 5;
static const char* GPIO_CHIP = "/dev/gpiochip0";

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int) { g_stop = 1; }

static inline uint64_t now_ns(void)
{
    struct timespec ts;
    // MONOTONIC_RAW: nao sofre ajuste de NTP nem slewing, que e o que se
    // quer ao medir intervalo curto numa maquina com ntp ativo.
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

static void usage(const char* argv0)
{
    printf("Uso: %s [opcoes]\n", argv0);
    printf("  -n <int>          numero de transacoes medidas (default 1000)\n");
    printf("  -o <arquivo>      CSV de saida (default stdout)\n");
    printf("  --label <texto>   rotulo do cenario, vai em cada linha\n");
    printf("  --warmup <int>    transacoes descartadas antes de medir (default 50)\n");
    printf("  --interval-us <n> pausa entre transacoes (default 0 = maxima taxa)\n");
    printf("  --cmd <status|version|mix>  comando usado (default status)\n");
    printf("        mix = alterna status/config/run/pause; e o unico que\n");
    printf("              exercita o caminho comando->atuacao no MCU\n");
    printf("  --fault-every <n> injeta falha de CRC 1 a cada n (0=off, default 0)\n");
    printf("  --metrics <arq>   le as metricas do MCU no fim e grava aqui\n");
    printf("  --spi-hz <n>      clock do SPI em Hz (default %u, o do servico;\n",
           (unsigned) argus_spi::SPEED_HZ);
    printf("                    faixa %u..%u)\n",
           (unsigned) argus_spi::SPEED_MIN_HZ, (unsigned) argus_spi::SPEED_MAX_HZ);
    printf("  --show-config     imprime a configuracao efetiva e sai\n");
    printf("  --run-rate <mL/h> inicia a infusao antes de medir e pausa no fim;\n");
    printf("                    necessario para capturar o pulso do motor\n");
    printf("  -h                esta ajuda\n");
}

int main(int argc, char** argv)
{
    long n_iter = 1000;
    long n_warmup = 50;
    long interval_us = 0;
    std::string out_path;
    std::string label = "sem-rotulo";
    cmd_ids_t req_id = CMD_GET_STATUS_REQ_ID;
    std::string cmd_name = "status";
    long fault_every = 0;
    std::string metrics_path;
    uint32_t spi_hz = argus_spi::SPEED_HZ;
    bool show_config = false;
    uint32_t run_rate = 0;

    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto next = [&](void) -> const char* {
            if(i + 1 >= argc) { fprintf(stderr, "faltou valor para %s\n", a.c_str()); exit(2); }
            return argv[++i];
        };
        if(a == "-n")                 n_iter = atol(next());
        else if(a == "-o")            out_path = next();
        else if(a == "--label")       label = next();
        else if(a == "--warmup")      n_warmup = atol(next());
        else if(a == "--interval-us") interval_us = atol(next());
        else if(a == "--fault-every")  fault_every = atol(next());
        else if(a == "--metrics")      metrics_path = next();
        else if(a == "--spi-hz")
        {
            const char* v = next();
            if(!argus_spi::parse_speed(v, &spi_hz))
            {
                fprintf(stderr, "--spi-hz invalido: %s (faixa %u..%u Hz)\n", v,
                        (unsigned) argus_spi::SPEED_MIN_HZ, (unsigned) argus_spi::SPEED_MAX_HZ);
                return 2;
            }
        }
        else if(a == "--show-config")  show_config = true;
        else if(a == "--run-rate")
        {
            /* Teto de 1200 mL/h: e o limite mecanico declarado no firmware
             * (RATE_MAX_ML_H). Acima disso o MCU recusa e o motor nao gira,
             * o que daria uma captura vazia. */
            const long v = atol(next());
            if(v <= 0 || v > 1200)
            {
                fprintf(stderr, "--run-rate invalido: %ld (faixa 1..1200 mL/h)\n", v);
                return 2;
            }
            run_rate = (uint32_t) v;
        }
        else if(a == "--cmd")
        {
            cmd_name = next();
            if(cmd_name == "version")     req_id = CMD_VERSION_REQ_ID;
            else if(cmd_name == "status") req_id = CMD_GET_STATUS_REQ_ID;
            else if(cmd_name == "mix")    req_id = CMD_GET_STATUS_REQ_ID;  /* ver laco */
            else { fprintf(stderr, "comando desconhecido: %s\n", cmd_name.c_str()); return 2; }
        }
        else if(a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else { fprintf(stderr, "argumento desconhecido: %s\n", a.c_str()); usage(argv[0]); return 2; }
    }

    // Lido pelo run_campaign.sh para o environment.txt: a procedencia vem do
    // binario que vai medir, nao de um texto copiado no script.
    if(show_config)
    {
        printf("spi_hz=%u\n", (unsigned) spi_hz);
        printf("spi_hz_servico=%u\n", (unsigned) argus_spi::SPEED_HZ);
        return 0;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    // O dump hexadecimal do bridge custa uma syscall write por transacao.
    // Deixado ligado, apareceria dentro da janela medida.
    Stm32Bridge::set_verbose(false);

    // Injecao de falha de CRC (E6 da campanha). Desligada por padrao.
    // O alvo e o byte alto do CRC do quadro real -- ver a correcao em
    // stm32_bridge.cpp, onde antes se corrompia padding.
    Stm32Bridge::set_fault_injection((int) fault_every);

    HalSpi spi(SPI_NODE, spi_hz);
    HalGpio ready_pin(READY_GPIO, HalGpio::Direction::Input, HalGpio::Edge::Both, false, GPIO_CHIP);
    Stm32Bridge bridge(spi, ready_pin);

    // Buffers reaproveitados: alocar dentro do laco mediria o malloc junto.
    cmd_cmds_t req;
    cmd_cmds_t res;

    fprintf(stderr, "[bench] cenario=%s cmd=%s n=%ld warmup=%ld intervalo=%ldus falha=1/%ld spi=%uHz\n",
            label.c_str(), cmd_name.c_str(), n_iter, n_warmup, interval_us, fault_every,
            (unsigned) spi_hz);

    /* --- Infusao continua --------------------------------------------
     * Com a bomba parada, o pino do pulso fica estatico e nao ha o que
     * capturar no analisador logico. Aqui a infusao e iniciada antes da
     * medicao e pausada no fim, para que o motor gire durante toda a
     * janela de captura. O volume vai no teto para a infusao nao terminar
     * sozinha no meio da aquisicao. */
    if(run_rate > 0)
    {
        cmd_cmds_t q, r;
        memset(&q, 0, sizeof(q));
        memset(&r, 0, sizeof(r));
        q.config_req.config.volume    = 1000;
        q.config_req.config.flow_rate = run_rate;
        q.config_req.config.diameter  = 20;
        const bool c1 = bridge.send_command(CMD_SET_CONFIG_REQ_ID, &q, &r);

        memset(&q, 0, sizeof(q));
        memset(&r, 0, sizeof(r));
        const bool c2 = bridge.send_command(CMD_ACTION_RUN_REQ_ID, &q, &r);

        fprintf(stderr, "[bench] infusao continua a %u mL/h: config=%s run=%s\n",
                (unsigned) run_rate, c1 ? "ok" : "FALHOU", c2 ? "ok" : "FALHOU");
        usleep(200000);   /* regime antes de comecar a medir */
    }

    // --- Aquecimento: descartado ---
    // A primeira transacao paga abertura de device e primeiro contato do
    // handshake; incluir isso na amostra deslocaria a cauda.
    for(long i = 0; i < n_warmup && !g_stop; i++)
    {
        memset(&req, 0, sizeof(req));
        memset(&res, 0, sizeof(res));
        bridge.send_command(req_id, &req, &res);
        if(interval_us > 0) usleep(interval_us);
    }

    FILE* out = stdout;
    if(!out_path.empty())
    {
        out = fopen(out_path.c_str(), "w");
        if(!out) { perror("fopen"); return 1; }
    }

    fprintf(out, "seq,label,cmd,t_rel_us,latency_us,ok,cmd_id\n");

    // Reserva antecipada: o CSV e escrito DEPOIS do laco, para que I/O de
    // arquivo nao entre em nenhuma janela medida.
    struct Sample { uint64_t t_rel_ns; uint64_t lat_ns; int ok; uint8_t cmd_id; };
    std::vector<Sample> samples;
    samples.reserve((size_t) n_iter);

    const uint64_t t0 = now_ns();

    // ------------------------------------------------------------------
    // Modo "mix": por que ele existe
    // ------------------------------------------------------------------
    // Na primeira campanha o WCET do MCU saiu ZERO (cmd_count = 0). Nao
    // era defeito da instrumentacao: GET_STATUS e respondido direto no
    // hub.c, sem passar pela fila do logic_engine. O caminho
    // comando->atuacao -- que e onde o RTOS pode de fato sofrer, e o que
    // define WCET e deadline miss -- nunca era exercitado.
    //
    // O mix intercala comandos que ATRAVESSAM a fila. Proporcao pensada
    // para nao deixar a bomba num estado estranho: predomina leitura,
    // com atuacao periodica.
    const cmd_ids_t ciclo_mix[] = {
        CMD_GET_STATUS_REQ_ID,
        CMD_GET_STATUS_REQ_ID,
        CMD_SET_CONFIG_REQ_ID,   /* atravessa a fila: rate, volume, diametro */
        CMD_GET_STATUS_REQ_ID,
        CMD_ACTION_RUN_REQ_ID,   /* atravessa a fila: muda estado           */
        CMD_GET_STATUS_REQ_ID,
        CMD_ACTION_PAUSE_REQ_ID, /* atravessa a fila: volta ao repouso      */
        CMD_GET_STATUS_REQ_ID,
    };
    const size_t ciclo_len = sizeof(ciclo_mix) / sizeof(ciclo_mix[0]);
    const bool modo_mix = (cmd_name == "mix");

    for(long i = 0; i < n_iter && !g_stop; i++)
    {
        memset(&req, 0, sizeof(req));
        memset(&res, 0, sizeof(res));

        cmd_ids_t este = req_id;
        if(modo_mix)
        {
            este = ciclo_mix[i % ciclo_len];
            if(este == CMD_SET_CONFIG_REQ_ID)
            {
                /* Valores DENTRO da faixa terapeutica: aqui medimos
                 * latencia, nao rejeicao. Fora da faixa o comando seria
                 * recusado e o caminho ate o motor nao seria percorrido. */
                req.config_req.config.volume = 100;
                req.config_req.config.flow_rate = 50;
                req.config_req.config.diameter = 20;
            }
        }

        const uint64_t a = now_ns();
        const bool ok = bridge.send_command(este, &req, &res);
        const uint64_t b = now_ns();

        samples.push_back({a - t0, b - a, ok ? 1 : 0, (uint8_t) este});

        if(interval_us > 0) usleep(interval_us);
    }

    for(size_t i = 0; i < samples.size(); i++)
    {
        /* cmd_id por linha: no modo mix, comandos diferentes tem custos
         * diferentes, e a media conjunta esconderia isso. */
        fprintf(out, "%zu,%s,%s,%.3f,%.3f,%d,0x%02X\n",
                i, label.c_str(), cmd_name.c_str(),
                samples[i].t_rel_ns / 1000.0,
                samples[i].lat_ns / 1000.0,
                samples[i].ok, samples[i].cmd_id);
    }

    // ------------------------------------------------------------------
    // Metricas do lado do MCU
    // ------------------------------------------------------------------
    // O gateway mede latencia de transacao; o que acontece DENTRO do
    // dominio critico -- jitter do laco, deadline miss, WCET do caminho
    // comando->atuacao -- so o MCU sabe. Coletadas aqui, uma vez, fora
    // da janela medida.
    if(!metrics_path.empty())
    {
        Stm32Bridge::set_fault_injection(0);   // nao corromper a coleta

        cmd_cmds_t mreq, mres;
        memset(&mreq, 0, sizeof(mreq));
        memset(&mres, 0, sizeof(mres));

        if(bridge.send_command(CMD_GET_METRICS_REQ_ID, &mreq, &mres))
        {
            // Sanidade antes de gravar. O CRC ja passou e o ID ja bate,
            // mas um valor impossivel ainda indica leitura suspeita, e um
            // CSV com lixo silencioso e pior que um CSV faltando:
            // a analise agrega o lixo e ninguem percebe.
            //
            // Criterio: o MCU tem de ter visto pelo menos tantos quadros
            // quanto as transacoes que acabamos de fazer. A leitura e
            // destrutiva (firmware >= v1.1.23), entao frames_seen e o
            // intervalo desde a leitura anterior -- nunca menor que n.
            const cmd_metrics_payload_t& chk = mres.metrics_res.m;
            if(chk.frames_seen < (uint32_t) n_iter)
            {
                fprintf(stderr,
                        "[bench] ERRO: metricas implausiveis "
                        "(frames_seen=%u < %ld transacoes). CSV nao gravado.\n",
                        chk.frames_seen, n_iter);
                fprintf(stderr,
                        "[bench] respostas fora de sincronia ate agora: %lu\n",
                        Stm32Bridge::sync_errors());
                goto metricas_fim;
            }

            FILE* mf = fopen(metrics_path.c_str(), "w");
            if(mf)
            {
                const cmd_metrics_payload_t& m = mres.metrics_res.m;
                fprintf(mf, "label,metrica,valor\n");
                #define M(campo) fprintf(mf, "%s,%s,%u\n", label.c_str(), #campo, m.campo)
                M(frames_seen);        M(crc_errors);
                M(driver_errors);      M(spi_resets);
                M(recoveries);         M(max_recovery_us);
                M(cmd_count);          M(cmd_lat_max_us);
                M(cmd_lat_avg_us);     M(loop_count);
                M(loop_period_min_us); M(loop_period_max_us);
                M(deadline_misses);
                #undef M
                fclose(mf);
                fprintf(stderr, "[bench] metricas do MCU -> %s\n", metrics_path.c_str());
                fprintf(stderr, "[bench]   crc_errors=%u recoveries=%u max_recovery=%uus "
                                "deadline_misses=%u loop[%u..%u]us cmd_wcet=%uus\n",
                        m.crc_errors, m.recoveries, m.max_recovery_us,
                        m.deadline_misses, m.loop_period_min_us,
                        m.loop_period_max_us, m.cmd_lat_max_us);
            }
            else perror("fopen metrics");
        }
        else
        {
            fprintf(stderr, "[bench] AVISO: nao foi possivel ler metricas do MCU\n");
        }
    metricas_fim: ;
    }

    /* Pausa a infusao iniciada por --run-rate. Fica DEPOIS da leitura de
     * metricas para nao somar um comando extra aos contadores do MCU. */
    if(run_rate > 0)
    {
        cmd_cmds_t q, r;
        memset(&q, 0, sizeof(q));
        memset(&r, 0, sizeof(r));
        const bool ok = bridge.send_command(CMD_ACTION_PAUSE_REQ_ID, &q, &r);
        fprintf(stderr, "[bench] infusao pausada: %s\n", ok ? "ok" : "FALHOU");
    }

    long fails = 0;
    for(const auto& s : samples) if(!s.ok) fails++;

    fprintf(stderr, "[bench] %zu transacoes, %ld falhas (%.2f%%)\n",
            samples.size(), fails,
            samples.empty() ? 0.0 : (100.0 * fails / samples.size()));

    if(out != stdout) fclose(out);
    return 0;
}
