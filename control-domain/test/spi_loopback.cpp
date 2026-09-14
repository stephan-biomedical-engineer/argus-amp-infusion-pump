#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include "hal_gpio.hpp" 

extern "C" {
    #include "cmd.h"
}

// CONFIGURAÇÕES
static const char *DEVICE = "/dev/spidev0.0";
static const uint32_t SPEED = 1000000;      
static const uint8_t BITS = 8;
static const uint8_t SPI_MODE = SPI_MODE_3; 
static const int SPI_PACKET_SIZE = 64; 

// PINO READY (PB0 do STM32 -> GPIO 25 da RPi)
static const int GPIO_READY_PIN = 25; 

// --- TRANSAÇÃO SPI ---
int spi_transaction(int fd_spi, HalGpio &slave_ready, uint8_t *tx_buf, uint8_t *rx_buf) 
{
    const int TIMEOUT_NS = 1000000000; 

    // 1.1 Espera Slave Ready
    if (slave_ready.get() == false) {
        if (slave_ready.wait_for_edge(TIMEOUT_NS) != HalGpio::Edge::Rising) {
            std::cerr << "[ERRO] Timeout esperando Slave Ready para escrita!" << std::endl;
            return -1;
        }
    }

    std::this_thread::sleep_for(std::chrono::microseconds(500)); 

    struct spi_ioc_transfer tr_write;
    memset(&tr_write, 0, sizeof(tr_write));
    tr_write.tx_buf = (unsigned long)tx_buf;
    tr_write.rx_buf = 0; 
    tr_write.len = SPI_PACKET_SIZE; 
    tr_write.speed_hz = SPEED;
    tr_write.bits_per_word = BITS;

    // 1.2 Envia
    if (ioctl(fd_spi, SPI_IOC_MESSAGE(1), &tr_write) < 1) {
        perror("Erro SPI Write");
        return -1;
    }

    // [CRÍTICO] Aumentado para 5ms para processamento de filas no Slave
    std::this_thread::sleep_for(std::chrono::microseconds(5000)); 

    // 2.1 Espera Resposta Pronta
    if (slave_ready.get() == false) {
        if (slave_ready.wait_for_edge(TIMEOUT_NS) != HalGpio::Edge::Rising) {
            std::cerr << "[ERRO] Timeout esperando Slave Ready para leitura!" << std::endl;
            return -1;
        }
    }

    std::this_thread::sleep_for(std::chrono::microseconds(500)); 

    struct spi_ioc_transfer tr_read;
    memset(&tr_read, 0, sizeof(tr_read));
    tr_read.tx_buf = 0; 
    tr_read.rx_buf = (unsigned long)rx_buf;
    tr_read.len = SPI_PACKET_SIZE; 
    tr_read.speed_hz = SPEED;
    tr_read.bits_per_word = BITS;

    // 2.2 Lê
    if (ioctl(fd_spi, SPI_IOC_MESSAGE(1), &tr_read) < 1) {
        perror("Erro SPI Read");
        return -1;
    }

    return 0;
}

int main() {
    printf("--- Teste Sequencial de Todos os Comandos ---\n");

    int fd_spi = open(DEVICE, O_RDWR);
    if (fd_spi < 0) { perror("Erro ao abrir spidev"); return 1; }

    uint8_t mode = SPI_MODE;
    uint8_t bits = BITS;
    uint32_t speed = SPEED;

    if (ioctl(fd_spi, SPI_IOC_WR_MODE, &mode) == -1) return 1;
    if (ioctl(fd_spi, SPI_IOC_WR_BITS_PER_WORD, &bits) == -1) return 1;
    if (ioctl(fd_spi, SPI_IOC_WR_MAX_SPEED_HZ, &speed) == -1) return 1;

    printf("Inicializando GPIO %d...\n", GPIO_READY_PIN);
    HalGpio slave_ready(GPIO_READY_PIN, HalGpio::Direction::Input, HalGpio::Edge::Rising);

    uint8_t tx_buf[SPI_PACKET_SIZE];
    uint8_t rx_buf[SPI_PACKET_SIZE];
    
    uint8_t master_addr = ADDR_MASTER;
    uint8_t slave_addr  = ADDR_SLAVE;
    
    int step = 0;
    const int MAX_STEPS = 9; 

    const int HEADER_SIZE = 5;
    const int TRAILER_SIZE = 2;

    while (true) {
        memset(tx_buf, 0, SPI_PACKET_SIZE);
        memset(rx_buf, 0, SPI_PACKET_SIZE);
        
        size_t encoded_size = 0;
        cmd_ids_t req_id;
        cmd_cmds_t req_cmd;
        memset(&req_cmd, 0, sizeof(req_cmd));

        switch (step) {
            case 0:
                printf("\n--- Passo 0: Pedindo VERSÃO ---\n");
                req_id = CMD_VERSION_REQ_ID;
                break;
            case 1:
                printf("\n--- Passo 1: Enviando CONFIGURAÇÃO (Vol:1000) ---\n");
                req_id = CMD_SET_CONFIG_REQ_ID;
                req_cmd.config_req.config.volume = 1000;
                req_cmd.config_req.config.flow_rate = 50;
                req_cmd.config_req.config.diameter = 10;
                req_cmd.config_req.config.mode = 1;
                break;
            case 2:
                printf("\n--- Passo 2: Pedindo STATUS (Check Config) ---\n");
                req_id = CMD_GET_STATUS_REQ_ID;
                break;
            case 3:
                printf("\n--- Passo 3: Comando RUN ---\n");
                req_id = CMD_ACTION_RUN_REQ_ID;
                break;
            case 4:
                printf("\n--- Passo 4: Pedindo STATUS (Check Running) ---\n");
                req_id = CMD_GET_STATUS_REQ_ID;
                break;
            case 5:
                printf("\n--- Passo 5: Comando BOLUS ---\n");
                req_id = CMD_ACTION_BOLUS_REQ_ID;
                break;
            case 6:
                printf("\n--- Passo 6: Comando PAUSE ---\n");
                req_id = CMD_ACTION_PAUSE_REQ_ID;
                break;
            case 7:
                printf("\n--- Passo 7: Pedindo STATUS (Check Paused) ---\n");
                req_id = CMD_GET_STATUS_REQ_ID;
                break;
            case 8:
                printf("\n--- Passo 8: Comando ABORT ---\n");
                req_id = CMD_ACTION_ABORT_REQ_ID;
                break;
        }

        step++;
        if (step > MAX_STEPS) step = 0;

        cmd_encode(tx_buf, &encoded_size, &master_addr, &slave_addr, &req_id, &req_cmd);

        if (spi_transaction(fd_spi, slave_ready, tx_buf, rx_buf) < 0) break;

        // Decodificação Little Endian
        uint16_t received_payload_len = (rx_buf[4] << 8) | rx_buf[3];
        
        if (received_payload_len > (SPI_PACKET_SIZE - HEADER_SIZE - TRAILER_SIZE)) {
            printf("[ERRO] Tamanho Payload Inválido: %d\n", received_payload_len);
            received_payload_len = 0;
        }

        size_t total_valid_len = HEADER_SIZE + received_payload_len + TRAILER_SIZE;
        uint8_t src, dst;
        cmd_ids_t res_id;
        cmd_cmds_t res_decoded;
        
        if (cmd_decode(rx_buf, total_valid_len, &src, &dst, &res_id, &res_decoded)) {
             printf("[RX] Resposta OK! ID: 0x%02X -> ", res_id);
             
             switch(res_id) {
                case CMD_GET_STATUS_RES_ID:
                    printf("[STATUS] Estado: %d | Vol: %d | FlowSet: %d\n", 
                    res_decoded.status_res.status_data.current_state,
                    res_decoded.status_res.status_data.volume,
                    res_decoded.status_res.status_data.flow_rate_set);
                    break;
                case CMD_VERSION_RES_ID:
                    printf("[VERSÃO] Firmware v%d.%d.%d\n", 
                        res_decoded.version_res.major,
                        res_decoded.version_res.minor,
                        res_decoded.version_res.patch);
                    break;
                case CMD_ACTION_RES_ID:
                    printf("[ACTION ACK] Cmd: 0x%02X | Status: %d\n", 
                        res_decoded.action_res.cmd_req_id,
                        res_decoded.action_res.status);
                    break;
                case CMD_SET_CONFIG_RES_ID:
                    printf("[CONFIG ACK] Status: %d\n", res_decoded.config_res.status);
                    break;
                default:
                    printf("Desconhecido.\n");
                    break;
             }
        }   
        else {
            printf("[RX] Erro CRC/Decode. (ValidLen: %d) RAW: %02X %02X...\n", (int)total_valid_len, rx_buf[0], rx_buf[1]);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    
    close(fd_spi);
    return 0;
}