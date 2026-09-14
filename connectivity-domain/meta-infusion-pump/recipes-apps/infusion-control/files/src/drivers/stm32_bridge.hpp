#ifndef STM32_BRIDGE_HPP
#define STM32_BRIDGE_HPP

#include "hal_spi.hpp"
#include "hal_gpio.hpp"
#include <cstdint>
#include <vector>

extern "C"
{
#include "cmd.h"
}

class Stm32Bridge
{
public:
    // Recebe referências para as HALs já instanciadas
    Stm32Bridge(HalSpi& spi, HalGpio& ready_pin);

    // Método principal síncrono
    bool send_command(cmd_ids_t req_id, cmd_cmds_t* req_data, cmd_cmds_t* res_data);

    // Dump hexadecimal por transacao. Ligado por padrao (comportamento
    // historico). O harness de medicao desliga: um printf por transacao
    // custa uma syscall write e contaminaria a latencia medida.
    static void set_verbose(bool v) { _verbose = v; }

    // ----------------------------------------------------------------
    // Injecao de falha de CRC, para o experimento de auto-healing
    // ----------------------------------------------------------------
    // every_n = 0 desliga (DEFAULT). every_n = N corrompe 1 a cada N
    // transacoes.
    //
    // Desligado por padrao de proposito: antes isto rodava SEMPRE, a
    // cada 100 transacoes, inclusive no servico em producao. Corromper
    // quadro de propósito num servico que comanda bomba de infusao nao
    // e comportamento aceitavel fora de bancada.
    static void set_fault_injection(int every_n) { _fault_every_n = every_n; }
    static long fault_injected_count(void) { return _fault_count; }

    // Respostas descartadas por chegarem fora de sincronia: o CRC passou,
    // mas o ID nao era o do comando enviado. Ver stm32_bridge.cpp.
    static unsigned long sync_errors(void) { return _sync_errors; }

    void suspend_hardware()
    {
        _ready_pin.release();
        _spi.close_device();
    }

    void resume_hardware()
    {
        _spi.open_device();
        _ready_pin.acquire();
    }

private:
    static inline bool _verbose = true;
    static inline int  _fault_every_n = 0;   // 0 = desligado
    static inline long _fault_count = 0;
    static inline unsigned long _sync_errors = 0;

    // Resposta que cada requisicao deve produzir. Base da checagem de
    // sincronia em send_command.
    static cmd_ids_t expected_response_id(cmd_ids_t req);

    HalSpi& _spi;
    HalGpio& _ready_pin;

    // Buffers internos
    uint8_t _tx_buf[300];
    uint8_t _rx_buf[300];

    // Espera o READY atingir um NIVEL (borda + reconfirmacao). Serve
    // para "pronto", que e um estado persistente.
    bool _wait_ready_level(bool want_high, int timeout_ms);

    // Espera a BORDA de descida, sem reconfirmar nivel. Serve para
    // "consumido", que e uma transicao de microssegundos.
    bool _wait_ready_falling(int timeout_ms);

    // O método que replica o spi_transaction do loopback
    bool _safe_transfer(size_t len);

    // Timeouts do handshake. O de 5 s preserva a folga do laco antigo
    // (500 iteracoes x 10 ms); os outros sao curtos porque agora a
    // espera e por borda, nao por amostragem.
    static constexpr int READY_TIMEOUT_MS     = 5000;
    // Curto de proposito: e apenas confirmatorio, e a proxima espera de
    // nivel alto cobre o caso de a borda escapar. Com 50 ms, cada borda
    // perdida custava 50 ms na latencia da transacao.
    // Curto de proposito, e nao e fatal falhar. O passo que ele governa e
    // apenas confirmatorio: o firmware desce o READY e rearma em seguida,
    // entao a borda de descida pode ja ter passado quando chegamos la.
    // Com 50 ms aqui, a latencia por transacao ia de ~4,8 ms para
    // ~54,8 ms -- esperavamos o timeout inteiro por um evento que nao
    // vinha mais. O laco original gastava no maximo 1 ms neste ponto.
    static constexpr int  READY_ACK_TIMEOUT_MS = 5;
    static constexpr int  READY_SETTLE_US      = 100;

    // Fatia maxima de cada espera de borda. Menor que os timeouts acima,
    // para o laco reavaliar o nivel em vez de apostar numa borda unica.
    static constexpr long EDGE_WINDOW_NS       = 1000L * 1000L;  /* 1 ms */
};

#endif
