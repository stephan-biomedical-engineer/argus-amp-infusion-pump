#include "stm32_bridge.hpp"
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdio> // Para printf

extern "C"
{
#include "cmd.h"
#include "utl_io.h"
}

Stm32Bridge::Stm32Bridge(HalSpi& spi, HalGpio& ready_pin) : _spi(spi), _ready_pin(ready_pin) {}

// ---------------------------------------------------------------------------
// Espera de nivel no pino READY
// ---------------------------------------------------------------------------
// Antes isto era um laco de polling com sleep de 10 ms. Medido em bancada,
// dominava a latencia: a distribuicao ficava quantizada em multiplos de
// 10 ms (mediana 4,8 ms, degraus em 11, 14 e 21 ms) para uma transacao de
// 64 bytes que a 500 kHz leva ~1 ms. O numero descrevia a politica de
// polling, nao o barramento.
//
// Agora combina duas coisas, e as duas sao necessarias:
//
//   - CHECAGEM DE NIVEL primeiro. wait_for_edge() espera uma BORDA; se o
//     pino ja esta no nivel desejado, nao havera borda nenhuma e a espera
//     iria ate o timeout.
//
//   - ESPERA EM BORDA com reconfirmacao. O kernel enfileira os eventos
//     desde o request, entao uma borda ocorrida entre o get() e o
//     wait_for_edge() nao se perde. Ainda assim reconfirmamos o nivel a
//     cada evento: evento espurio ou acumulado nao deve ser tomado como
//     o que esperavamos.
//
// O timeout interno curto faz o laco reavaliar periodicamente, de modo
// que uma borda perdida custa milissegundos, nao o timeout inteiro.
bool Stm32Bridge::_wait_ready_level(bool want_high, int timeout_ms)
{
    if(_ready_pin.get() == want_high)
        return true;

    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeout_ms);

    while(true)
    {
        const auto restante = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  deadline - std::chrono::steady_clock::now()).count();
        if(restante <= 0)
            return false;

        // A janela de cada espera NUNCA pode exceder o que resta do
        // timeout pedido. Sem este limite, uma janela de 2 ms dentro de
        // um timeout de 2 ms podia gastar ate o dobro -- e foi por essa
        // familia de descuido que o ACK de 50 ms fez a latencia por
        // transacao saltar de ~4,8 ms para ~54,8 ms.
        //
        // Fatiar a espera tambem serve a outro proposito: reavaliar o
        // NIVEL periodicamente, em vez de confiar numa borda unica que
        // pode ter passado antes de comecarmos a esperar.
        const long janela = (restante < EDGE_WINDOW_NS) ? (long) restante : EDGE_WINDOW_NS;

        _ready_pin.wait_for_edge((int) janela);

        if(_ready_pin.get() == want_high)
            return true;
    }
}

// Transação Segura: Espera Hardware -> Transfere -> Confirma
bool Stm32Bridge::_safe_transfer(size_t len)
{
    // 1. Espera o STM32 sinalizar que esta pronto.
    if(!_wait_ready_level(true, READY_TIMEOUT_MS))
    {
        std::cerr << "[BRIDGE] Timeout Hardware: STM32 nao levantou Ready Pin" << std::endl;
        return false;
    }

    // 2. Janela de estabilizacao.
    //
    // Nao e "estabilizacao de DMA", apesar do nome antigo. Compensa uma
    // corrida no firmware: o READY sobe ANTES de spi_transceive() armar o
    // periferico, entao existe um instante em que o mestre ve "pronto" e
    // o escravo ainda nao esta. A correcao estrutural e do lado do MCU
    // (spi_transceive_signal: armar primeiro, sinalizar depois); enquanto
    // ela nao vier, esta janela precisa existir.
    std::this_thread::sleep_for(std::chrono::microseconds(READY_SETTLE_US));

    // 3. Transferência SPI
    const bool res = _spi.transfer(_tx_buf, _rx_buf, len);

    // 4. Confirma que o STM32 consumiu, aguardando a BORDA de descida.
    //
    //    Aqui NAO se reconfirma o nivel, e essa e a diferenca que importa.
    //    O firmware baixa o READY logo apos o transceive e o levanta de
    //    novo ao rearmar -- a janela em nivel baixo dura microssegundos.
    //    A primeira versao desta funcao esperava NIVEL baixo: detectava a
    //    borda certa, reconfirmava o nivel, encontrava o pino ja alto de
    //    novo, descartava o evento e seguia esperando ate o timeout.
    //    Medido em bancada: 54,8 ms por transacao, contra 4,8 ms do
    //    polling que isto veio substituir.
    //
    //    Nao e fatal se a borda nao vier: a proxima espera de nivel alto
    //    resolve. Por isso o retorno e ignorado.
    _wait_ready_falling(READY_ACK_TIMEOUT_MS);

    return res;
}

// ---------------------------------------------------------------------------
// Espera a borda de DESCIDA do READY
// ---------------------------------------------------------------------------
// Usada apenas para confirmar que o escravo consumiu o quadro. Diferente
// de _wait_ready_level(), decide pelo EVENTO e nao pelo nivel: o nivel
// baixo dura pouco demais para ser amostrado com confianca.
bool Stm32Bridge::_wait_ready_falling(int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeout_ms);

    while(std::chrono::steady_clock::now() < deadline)
    {
        const HalGpio::Edge e = _ready_pin.wait_for_edge(2 * 1000 * 1000);

        if(e == HalGpio::Edge::Falling)
            return true;

        if(e == HalGpio::Edge::None)
        {
            // Sem evento no intervalo. Se o pino esta baixo agora, a
            // descida ocorreu antes de comecarmos a esperar.
            if(!_ready_pin.get())
                return true;
        }
        // Edge::Rising: o escravo rearmou antes de observarmos a descida.
        // Segue esperando; o deadline encerra.
    }
    return false;
}


bool Stm32Bridge::send_command(cmd_ids_t req_id, cmd_cmds_t* req_data, cmd_cmds_t* res_data)
{
    // Limpa buffers
    std::memset(_tx_buf, 0, sizeof(_tx_buf));
    std::memset(_rx_buf, 0, sizeof(_rx_buf));

    size_t encoded_size = 0;
    uint8_t master = ADDR_MASTER;
    uint8_t slave = ADDR_SLAVE;

    // 1. Encode do Comando
    // IMPORTANTE: O cmd_encode (versão nova) já insere o SOF (AA 55) automaticamente.
    if(!cmd_encode(_tx_buf, &encoded_size, &master, &slave, &req_id, req_data))
    {
        std::cerr << "[BRIDGE] Erro de Encode" << std::endl;
        return false;
    }

    // Garante tamanho mínimo de transferência (64 bytes para manter o clock)
    size_t xfer_len = (encoded_size < 64) ? 64 : encoded_size;

    // ------------------------------------------------------------------
    // INJECAO DE FALHA DE CRC (experimento de auto-healing)
    // ------------------------------------------------------------------
    // CORRECAO: o alvo era _tx_buf[xfer_len - 1], ou seja o byte 63.
    // Como xfer_len e sempre 64 e os quadros reais tem 9 a 18 bytes, o
    // byte 63 e PADDING -- nunca o CRC. Verificado compilando o proprio
    // cmd.c e replicando o parser do firmware: quatro comandos, zero
    // rejeicoes. O experimento nunca injetou falha alguma.
    //
    // O CRC ocupa os dois ultimos bytes do QUADRO, em
    // [encoded_size-2] e [encoded_size-1].
    if(_fault_every_n > 0)
    {
        static long fault_counter = 0;
        if(++fault_counter >= _fault_every_n)
        {
            fault_counter = 0;
            _fault_count++;
            _tx_buf[encoded_size - 1] ^= 0xFF;   // byte alto do CRC
            if(_verbose)
            {
                std::cout << "[!] Falha de CRC injetada no byte "
                          << (encoded_size - 1) << " (quadro de "
                          << encoded_size << " bytes)" << std::endl;
            }
        }
    }

    // 2. Envia o Comando
    if(!_safe_transfer(xfer_len))
    {
        return false;
    }

    // Sem delay fixo aqui.
    //
    // Havia 2 ms cravados "para dar tempo do STM32 acordar a thread e
    // baixar o Ready Pin". Dois problemas: o passo 4 do _safe_transfer ja
    // aguarda exatamente isso, e 2 ms fixos viravam piso de latencia em
    // TODA transacao, medido ou nao. O proximo _safe_transfer espera o
    // READY subir de novo, que e o sinal real de que o MCU esta pronto
    // para a resposta.

    // 3. Lê a Resposta (Imediatamente)
    // Prepara Dummys
    std::memset(_tx_buf, 0, 64);

    // Lê 64 bytes de resposta (pode conter lixo + resposta)
    if(!_safe_transfer(64))
    {
        return false;
    }

    if(_verbose)
    {
        printf("[SPI RAW RX]: ");
        for(int rx_byte = 0; rx_byte < 16; rx_byte++)
        {
            printf("%02X ", _rx_buf[rx_byte]);
        }
        printf("\n");
    }

    // 4. SCANNER DE SOF (A Mágica da Sincronia)
    // Em vez de assumir que a resposta está no byte 0 ou 2, procuramos a assinatura.

    int sof_index = -1;
    // Varre o buffer procurando AA 55
    for(int scan_sof_idx = 0; scan_sof_idx < (64 - CMD_HDR_SIZE); scan_sof_idx++)
    {
        if(_rx_buf[scan_sof_idx] == CMD_SOF_1_BYTE && _rx_buf[scan_sof_idx + 1] == CMD_SOF_2_BYTE)
        {
            sof_index = scan_sof_idx;
            break;
        }
    }

    if(sof_index < 0)
    {
        std::cerr << "[BRIDGE] Erro: SOF nao encontrado. Dump RX (16 bytes): ";
        for(int rx_byte = 0; rx_byte < 16; rx_byte++)
        {
            printf("%02X ", _rx_buf[rx_byte]);
        }
        printf("\n");

        return false;
    }

    // Aponta para o início real do pacote encontrado
    uint8_t* p_packet = &_rx_buf[sof_index];

    // Calcula tamanho total esperado
    // Header V2 tem 7 bytes. O Payload Size está nos bytes 5 e 6 (indices relativos ao SOF).
    // p_packet[0]=AA, [1]=55, [2]=DST, [3]=SRC, [4]=ID, [5]=SizeL, [6]=SizeH

    uint16_t payload_len = utl_io_get16_fl(&p_packet[5]);
    size_t total_valid_len = CMD_HDR_SIZE + payload_len + CMD_TRAILER_SIZE;

    // Decodifica a partir do SOF encontrado
    // O cmd_decode novo já sabe pular o SOF interno.
    uint8_t src, dst;
    cmd_ids_t res_id_decoded;

    if(cmd_decode(p_packet, total_valid_len, &src, &dst, &res_id_decoded, res_data))
    {
        // O CRC diz que o quadro chegou intacto. NAO diz que e o quadro
        // certo. Se o enlace escorregar uma resposta -- o gateway le a
        // resposta do comando ANTERIOR -- o payload passa no CRC e e
        // reinterpretado como o tipo que o chamador esperava.
        //
        // Observado em bancada (campanha bench-p3big, cenario combined,
        // repeticao 2): uma leitura de GET_METRICS devolveu
        // frames_seen=5 e crc_errors=12800 com loop_count=0, numeros que
        // nao existiam. O CSV gravou o lixo sem qualquer aviso, e a
        // repeticao seguinte veio com o dobro dos quadros porque o MCU
        // nunca chegou a zerar os contadores.
        //
        // Sem esta checagem toda medida do artigo fica a merce de um
        // escorregao de sincronia que nao deixa rastro.
        const cmd_ids_t esperado = expected_response_id(req_id);
        if(esperado != CMD_INVALID_ID && res_id_decoded != esperado)
        {
            std::cerr << "[BRIDGE] Resposta fora de sincronia: esperado 0x"
                      << std::hex << (int) esperado << ", recebido 0x"
                      << (int) res_id_decoded << std::dec << std::endl;
            _sync_errors++;
            return false;
        }
        return true;
    }

    std::cerr << "[BRIDGE] Erro de Checksum na resposta (SOF achado em " << sof_index << ")" << std::endl;
    return false;
}

/* Mapeia requisicao -> resposta esperada. Os ACTION_* compartilham uma
 * unica resposta (CMD_ACTION_RES_ID); os OTA_* idem. Comandos sem
 * resposta declarada devolvem CMD_INVALID_ID e escapam da checagem, para
 * que um comando novo nao quebre por esquecimento. */
cmd_ids_t Stm32Bridge::expected_response_id(cmd_ids_t req)
{
    switch(req)
    {
    case CMD_VERSION_REQ_ID:      return CMD_VERSION_RES_ID;
    case CMD_GET_STATUS_REQ_ID:   return CMD_GET_STATUS_RES_ID;
    case CMD_SET_CONFIG_REQ_ID:   return CMD_SET_CONFIG_RES_ID;
    case CMD_GET_METRICS_REQ_ID:  return CMD_GET_METRICS_RES_ID;
    case CMD_ACTION_RUN_REQ_ID:
    case CMD_ACTION_PAUSE_REQ_ID:
    case CMD_ACTION_ABORT_REQ_ID:
    case CMD_ACTION_PURGE_REQ_ID:
    case CMD_ACTION_BOLUS_REQ_ID: return CMD_ACTION_RES_ID;
    case CMD_OTA_START_REQ_ID:
    case CMD_OTA_CHUNK_REQ_ID:
    case CMD_OTA_END_REQ_ID:      return CMD_OTA_RES_ID;
    default:                      return (cmd_ids_t) CMD_INVALID_ID;
    }
}

