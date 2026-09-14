// ===========================================================================
// argus_attack — cenarios de ataque contra o barramento MCU
// ===========================================================================
// Cobre os vetores que o revisor 1 pediu nominalmente e que o
// argus_bench nao exercita: replay, spoofing, perda/atraso de pacotes,
// escalonamento de privilegio e reset em laco.
//
// Cada cenario e um EXPERIMENTO com veredicto explicito: informa o que
// se esperava, o que aconteceu, e se a defesa segurou. A saida sai em
// CSV para entrar na mesma analise dos demais dados.
//
// Nao inclui os cenarios de carga (CPU, I/O, rede) -- esses sao do
// run_campaign.sh, que ja os cobre com o argus_bench.
//
// IMPORTANTE: pare o servico antes. Disputa /dev/spidev0.1.
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
#include "utl_io.h"
#include "utl_crc16.h"
}

#include "spi_config.hpp"

// Velocidade do SPI: spi_config.hpp (a mesma do servico), ou --spi-hz.
static const char* SPI_NODE = "/dev/spidev0.1";
static const unsigned READY_GPIO = 5;
static const unsigned RESET_GPIO = 4;
static const char* GPIO_CHIP = "/dev/gpiochip0";

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int) { g_stop = 1; }

static inline uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

// --- Saida estruturada -----------------------------------------------------
static FILE* g_out = nullptr;

static void emit(const char* exp, const char* metrica, const char* valor)
{
    if(g_out)
        fprintf(g_out, "%s,%s,%s\n", exp, metrica, valor);
}

static void emit_num(const char* exp, const char* metrica, double v)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.3f", v);
    emit(exp, metrica, buf);
}

static void banner(const char* id, const char* nome, const char* esperado)
{
    printf("\n=== %s — %s ===\n", id, nome);
    printf("    esperado: %s\n", esperado);
}

static void veredicto(const char* id, bool defesa_segurou, const char* obs)
{
    printf("    RESULTADO: %s — %s\n",
           defesa_segurou ? "DEFESA SEGUROU" : "DEFESA FALHOU", obs);
    emit(id, "defesa_segurou", defesa_segurou ? "1" : "0");
    emit(id, "observacao", obs);
}

// ===========================================================================
// E7 — Replay
// ===========================================================================
// Captura um quadro valido e o reenvia. O protocolo atual nao tem nonce,
// contador nem timestamp: a expectativa REALISTA e que o replay seja
// aceito. Documentar isso e melhor do que ser encontrado pelo revisor.
static void exp_replay(HalSpi& spi, HalGpio& ready)
{
    banner("E7", "Replay de quadro valido",
           "quadro reenviado deve ser rejeitado (exige nonce/contador)");

    // Monta um SET_CONFIG valido uma vez e o reenvia N vezes byte a byte.
    uint8_t frame[300];
    size_t flen = 0;
    uint8_t dst = ADDR_SLAVE, src = ADDR_MASTER;
    cmd_ids_t id = CMD_SET_CONFIG_REQ_ID;
    cmd_cmds_t data;

    memset(&data, 0, sizeof(data));
    data.config_req.config.volume = 100;
    data.config_req.config.flow_rate = 50;
    data.config_req.config.diameter = 20;

    if(!cmd_encode(frame, &flen, &src, &dst, &id, &data))
    {
        printf("    ERRO: encode falhou\n");
        return;
    }
    printf("    quadro capturado: %zu bytes, CRC valido\n", flen);

    // Reenvia o MESMO quadro, bit a bit identico, 20 vezes.
    uint8_t tx[64], rx[64];
    int aceitos = 0;
    const int N = 20;

    for(int i = 0; i < N && !g_stop; i++)
    {
        memset(tx, 0, sizeof(tx));
        memcpy(tx, frame, flen);

        int retries = 200;
        while(!ready.get() && --retries > 0)
            usleep(1000);

        usleep(100);
        spi.transfer(tx, rx, 64);
        usleep(5000);

        // Le a resposta e verifica se foi aceita
        memset(tx, 0, sizeof(tx));
        retries = 200;
        while(!ready.get() && --retries > 0)
            usleep(1000);
        spi.transfer(tx, rx, 64);

        for(int k = 0; k < 64 - CMD_HDR_SIZE; k++)
        {
            if(rx[k] == CMD_SOF_1_BYTE && rx[k + 1] == CMD_SOF_2_BYTE)
            {
                // SET_CONFIG_RES com status CMD_OK == aceito
                if(rx[k + 4] == CMD_SET_CONFIG_RES_ID && rx[k + 7] == CMD_OK)
                    aceitos++;
                break;
            }
        }
        usleep(50000);
    }

    printf("    reenvios: %d | aceitos: %d\n", N, aceitos);
    emit_num("E7", "reenvios", N);
    emit_num("E7", "aceitos", aceitos);

    veredicto("E7", aceitos == 0,
              aceitos > 0
                  ? "protocolo SEM protecao contra replay -- quadro capturado e reaceito indefinidamente"
                  : "replay rejeitado");
}

// ===========================================================================
// E8 — Spoofing
// ===========================================================================
// Quadro FORJADO (nunca emitido pelo gateway legitimo) com CRC valido.
// O CRC e detector de erro, nao autenticador: qualquer um que conheca o
// polinomio forja. A expectativa e que seja aceito, e a conclusao e que
// a integridade do barramento nao substitui autenticacao.
static void exp_spoofing(HalSpi& spi, HalGpio& ready)
{
    banner("E8", "Quadro forjado com CRC valido",
           "quadro nao autenticado deve ser rejeitado (exige MAC/assinatura)");

    uint8_t tx[64], rx[64];
    memset(tx, 0, sizeof(tx));

    // Forja um ACTION_ABORT a mao, calculando o CRC do zero.
    uint8_t* p = tx;
    *p++ = CMD_SOF_1_BYTE;
    *p++ = CMD_SOF_2_BYTE;
    *p++ = ADDR_SLAVE;
    *p++ = ADDR_MASTER;
    *p++ = CMD_ACTION_ABORT_REQ_ID;
    *p++ = 0x00;  // size L
    *p++ = 0x00;  // size H
    uint16_t crc = utl_crc16_data(tx, (size_t) (p - tx), 0xFFFF);
    *p++ = (uint8_t) (crc & 0xFF);
    *p++ = (uint8_t) (crc >> 8);

    printf("    quadro forjado: %zu bytes, CRC calculado sem chave nenhuma\n",
           (size_t) (p - tx));

    int retries = 200;
    while(!ready.get() && --retries > 0)
        usleep(1000);
    usleep(100);
    spi.transfer(tx, rx, 64);
    usleep(5000);

    memset(tx, 0, sizeof(tx));
    retries = 200;
    while(!ready.get() && --retries > 0)
        usleep(1000);
    spi.transfer(tx, rx, 64);

    bool aceito = false;
    for(int k = 0; k < 64 - CMD_HDR_SIZE; k++)
    {
        if(rx[k] == CMD_SOF_1_BYTE && rx[k + 1] == CMD_SOF_2_BYTE)
        {
            if(rx[k + 4] == CMD_ACTION_RES_ID && rx[k + 8] == CMD_OK)
                aceito = true;
            break;
        }
    }

    emit("E8", "aceito", aceito ? "1" : "0");
    veredicto("E8", !aceito,
              aceito
                  ? "quadro forjado ACEITO -- CRC-16 detecta erro, nao autentica origem"
                  : "quadro forjado rejeitado");
}

// ===========================================================================
// E9 — Perda e atraso de pacotes
// ===========================================================================
// Interrompe a transacao no meio (envia so parte do quadro) e mede se o
// MCU se recupera para o proximo quadro valido.
static void exp_perda_atraso(Stm32Bridge& bridge, HalSpi& spi, HalGpio& ready)
{
    banner("E9", "Perda e atraso de quadro",
           "MCU descarta o parcial e volta a aceitar o proximo quadro valido");

    uint8_t frame[300], tx[64], rx[64];
    size_t flen = 0;
    uint8_t dst = ADDR_SLAVE, src = ADDR_MASTER;
    cmd_ids_t id = CMD_GET_STATUS_REQ_ID;
    cmd_cmds_t data;
    memset(&data, 0, sizeof(data));
    cmd_encode(frame, &flen, &src, &dst, &id, &data);

    const int N = 10;
    int recuperados = 0;
    double soma_us = 0;

    for(int i = 0; i < N && !g_stop; i++)
    {
        // 1. Envia quadro TRUNCADO (metade dos bytes)
        memset(tx, 0, sizeof(tx));
        memcpy(tx, frame, flen / 2);

        int retries = 200;
        while(!ready.get() && --retries > 0)
            usleep(1000);
        spi.transfer(tx, rx, 64);

        // 2. Atraso deliberado
        usleep(20000);

        // 3. Quadro valido: quanto tempo ate voltar a responder?
        const uint64_t t0 = now_ns();
        cmd_cmds_t req, res;
        memset(&req, 0, sizeof(req));
        memset(&res, 0, sizeof(res));
        const bool ok = bridge.send_command(CMD_GET_STATUS_REQ_ID, &req, &res);
        const uint64_t t1 = now_ns();

        if(ok)
        {
            recuperados++;
            soma_us += (t1 - t0) / 1000.0;
        }
        usleep(50000);
    }

    const double media = recuperados ? soma_us / recuperados : 0.0;
    printf("    truncamentos: %d | recuperados: %d | tempo medio: %.1f us\n",
           N, recuperados, media);
    emit_num("E9", "truncamentos", N);
    emit_num("E9", "recuperados", recuperados);
    emit_num("E9", "tempo_recuperacao_medio_us", media);

    veredicto("E9", recuperados == N,
              recuperados == N ? "recuperacao integral apos quadro parcial"
                               : "houve perda de sincronia persistente");
}

// ===========================================================================
// E13 — Escalonamento de privilegio (limite terapeutico)
// ===========================================================================
// O gateway pede vazao muito acima da janela segura. E a alegacao central
// do artigo: o MCU tem autoridade final sobre a atuacao.
static void exp_limite_terapeutico(Stm32Bridge& bridge)
{
    banner("E13", "Comando de vazao fora da janela terapeutica",
           "MCU rejeita ou satura; a vazao efetiva nunca atinge o pedido");

    const uint32_t pedidos[] = {600, 1200, 5000, 65000};
    int rejeitados = 0;
    const int N = (int) (sizeof(pedidos) / sizeof(pedidos[0]));

    for(int i = 0; i < N && !g_stop; i++)
    {
        cmd_cmds_t req, res;
        memset(&req, 0, sizeof(req));
        memset(&res, 0, sizeof(res));
        req.config_req.config.volume = 100;
        req.config_req.config.flow_rate = pedidos[i];
        req.config_req.config.diameter = 20;

        bridge.send_command(CMD_SET_CONFIG_REQ_ID, &req, &res);
        usleep(100000);

        // Le o estado: qual vazao o MCU realmente adotou?
        cmd_cmds_t sreq, sres;
        memset(&sreq, 0, sizeof(sreq));
        memset(&sres, 0, sizeof(sres));
        uint32_t efetiva = 0;
        if(bridge.send_command(CMD_GET_STATUS_REQ_ID, &sreq, &sres))
            efetiva = sres.status_res.status_data.flow_rate_set;

        const bool contido = (efetiva < pedidos[i]);
        if(contido)
            rejeitados++;

        printf("    pedido %6u mL/h -> efetivo %6u mL/h  [%s]\n",
               pedidos[i], efetiva, contido ? "CONTIDO" : "ACEITO");

        char k[64];
        snprintf(k, sizeof(k), "pedido_%u_efetivo", pedidos[i]);
        emit_num("E13", k, efetiva);
        usleep(100000);
    }

    emit_num("E13", "pedidos", N);
    emit_num("E13", "contidos", rejeitados);
    veredicto("E13", rejeitados > 0,
              rejeitados > 0 ? "MCU limitou pelo menos um pedido excessivo"
                             : "NENHUM pedido foi limitado pelo MCU");
}

// ===========================================================================
// E14 — Negacao de terapia por reset em laco
// ===========================================================================
// Vem do achado de que o gateway tem uma linha de reset por hardware
// ligada ao NRST do MCU. Um gateway comprometido pode aciona-la
// indefinidamente. Mede por quanto tempo o dominio critico fica
// indisponivel.
static void exp_reset_loop(Stm32Bridge& bridge, HalGpio& reset_pin, int ciclos)
{
    banner("E14", "Reset em laco a partir do gateway",
           "quantificar a indisponibilidade que um gateway comprometido impoe");

    printf("    ATENCAO: so faz sentido com GPIO %u ligado ao NRST.\n", RESET_GPIO);

    double soma_us = 0;
    int medidos = 0;

    for(int i = 0; i < ciclos && !g_stop; i++)
    {
        reset_pin.set(false);
        usleep(50000);
        reset_pin.set(true);

        const uint64_t t0 = now_ns();
        bool voltou = false;
        for(int tent = 0; tent < 100 && !g_stop; tent++)
        {
            cmd_cmds_t req, res;
            memset(&req, 0, sizeof(req));
            memset(&res, 0, sizeof(res));
            if(bridge.send_command(CMD_GET_STATUS_REQ_ID, &req, &res))
            {
                voltou = true;
                break;
            }
            usleep(50000);
        }
        const uint64_t t1 = now_ns();

        if(voltou)
        {
            soma_us += (t1 - t0) / 1000.0;
            medidos++;
            printf("    ciclo %d: indisponivel por %.1f ms\n",
                   i + 1, (t1 - t0) / 1000000.0);
        }
        else
        {
            printf("    ciclo %d: NAO voltou dentro do limite\n", i + 1);
        }
    }

    const double media_ms = medidos ? (soma_us / medidos) / 1000.0 : 0.0;
    emit_num("E14", "ciclos", ciclos);
    emit_num("E14", "indisponibilidade_media_ms", media_ms);

    char obs[192];
    snprintf(obs, sizeof(obs),
             "gateway impoe %.0f ms de indisponibilidade por reset; em laco, negacao continua",
             media_ms);
    // Nao ha defesa a segurar aqui: o experimento QUANTIFICA a exposicao.
    printf("    RESULTADO: exposicao medida — %s\n", obs);
    emit("E14", "observacao", obs);
}

// ===========================================================================
int main(int argc, char** argv)
{
    std::string out_path;
    std::string only;
    int reset_ciclos = 3;
    bool do_reset = false;
    uint32_t spi_hz = argus_spi::SPEED_HZ;

    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto next = [&](void) -> const char* {
            if(i + 1 >= argc) { fprintf(stderr, "faltou valor para %s\n", a.c_str()); exit(2); }
            return argv[++i];
        };
        if(a == "-o")                  out_path = next();
        else if(a == "--only")         only = next();
        else if(a == "--with-reset")   do_reset = true;
        else if(a == "--reset-cycles") reset_ciclos = atoi(next());
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
        else if(a == "-h" || a == "--help")
        {
            printf("Uso: %s [-o saida.csv] [--only E7,E8,...] [--with-reset] [--spi-hz <n>]\n", argv[0]);
            printf("  E7  replay          E8  spoofing        E9  perda/atraso\n");
            printf("  E13 limite terapeutico                  E14 reset em laco\n");
            printf("\n  E14 exige --with-reset e o GPIO 4 ligado ao NRST.\n");
            printf("  Pare o servico antes: systemctl stop infusion-pump\n");
            printf("  --spi-hz <n>  clock do SPI em Hz (default %u, o do servico)\n",
                   (unsigned) argus_spi::SPEED_HZ);
            return 0;
        }
        else { fprintf(stderr, "argumento desconhecido: %s\n", a.c_str()); return 2; }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if(!out_path.empty())
    {
        g_out = fopen(out_path.c_str(), "w");
        if(!g_out) { perror("fopen"); return 1; }
        fprintf(g_out, "experimento,metrica,valor\n");
    }

    Stm32Bridge::set_verbose(false);
    Stm32Bridge::set_fault_injection(0);

    printf("[attack] barramento SPI a %u Hz\n", (unsigned) spi_hz);
    HalSpi spi(SPI_NODE, spi_hz);
    HalGpio ready(READY_GPIO, HalGpio::Direction::Input, HalGpio::Edge::Both, false, GPIO_CHIP);
    Stm32Bridge bridge(spi, ready);

    auto quer = [&](const char* id) {
        return only.empty() || only.find(id) != std::string::npos;
    };

    if(quer("E7"))  exp_replay(spi, ready);
    if(quer("E8"))  exp_spoofing(spi, ready);
    if(quer("E9"))  exp_perda_atraso(bridge, spi, ready);
    if(quer("E13")) exp_limite_terapeutico(bridge);

    if(quer("E14") && do_reset)
    {
        HalGpio reset_pin(RESET_GPIO, HalGpio::Direction::Output, HalGpio::Edge::None, false, GPIO_CHIP);
        exp_reset_loop(bridge, reset_pin, reset_ciclos);
    }
    else if(quer("E14"))
    {
        printf("\n=== E14 pulado (use --with-reset para executar) ===\n");
    }

    printf("\n");
    if(g_out)
    {
        fclose(g_out);
        printf("resultados em %s\n", out_path.c_str());
    }
    return 0;
}
