/* ota_update.cpp - VERSÃO DEBUG HARDCORE */
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/spi/spidev.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <fstream>
#include "hal_gpio.hpp" 

extern "C" {
    #include "cmd.h"
}

static const char *DEVICE = "/dev/spidev0.0";
static const int GPIO_READY_PIN = 25; 
static const uint8_t SPI_MODE = SPI_MODE_0;
static const uint8_t BITS = 8;
static const uint32_t SPEED = 100000; 
static const int CHUNK_DATA_SIZE = 48; 

int fd_spi;
HalGpio *slave_ready_ptr;
uint8_t tx_buf[300]; 
uint8_t rx_buf[300];

int spi_transaction() {
    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long)tx_buf;
    tr.rx_buf = (unsigned long)rx_buf;
    tr.len = 64; 
    tr.speed_hz = SPEED;
    tr.bits_per_word = 8;

    int retries = 500; 
    while (!slave_ready_ptr->get()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        retries--;
        if (retries <= 0) {
            printf("\n[FATAL] Timeout Hardware: STM32 não levantou pino Ready!\n");
            return -1;
        }
    }

    // --- MUDANÇA CRÍTICA 1: Delay aumentado para 2ms ---
    // Isso garante que o DMA do STM32 assumiu o bus MISO
    std::this_thread::sleep_for(std::chrono::milliseconds(2)); 
    
    if (ioctl(fd_spi, SPI_IOC_MESSAGE(1), &tr) < 1) return -1;
    return 0;
}

bool esperar_ack(uint8_t cmd_esperado) {
    int tentativas = 50; 

    printf("\n   [DEBUG] Aguardando ACK para CMD %02X...", cmd_esperado);

    while (tentativas--) {
        memset(tx_buf, 0, 64);
        
        if (spi_transaction() < 0) return false;

        uint8_t id_recebido = rx_buf[2];

        // --- MUDANÇA CRÍTICA 2: Dump do que foi recebido se não for zero ---
        if (id_recebido != 0x00 && id_recebido != 0xFF) {
            printf("\n   [RX DUMP] ");
            for(int i=0; i<10; i++) printf("%02X ", rx_buf[i]);
            
            if (id_recebido == CMD_OTA_RES_ID) { // 0x5F
                uint8_t req_originaria = rx_buf[5]; 
                uint8_t status = rx_buf[6];

                if (req_originaria == cmd_esperado && status == 0) {
                    printf("-> ACK OK!\n");
                    return true;
                } else {
                    printf("-> Erro Lógico (Req: %02X Status: %d)", req_originaria, status);
                    // Não retorna false aqui, tenta de novo caso seja lixo
                }
            } else {
                printf("-> ID Inesperado (%02X)", id_recebido);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Imprime o último buffer lido mesmo se for zero, para sabermos
    printf("\n   [TIMEOUT] Falha. Último Buffer RX: ");
    for(int i=0; i<10; i++) printf("%02X ", rx_buf[i]);
    printf("\n");
    return false;
}

bool enviar_chunk_seguro(uint32_t offset, std::vector<uint8_t> &dados) {
    memset(tx_buf, 0, 64);
    
    cmd_ota_chunk_t chunk_pkt;
    chunk_pkt.offset = offset;
    chunk_pkt.len = dados.size();
    memcpy(chunk_pkt.data, dados.data(), dados.size());

    tx_buf[0] = ADDR_MASTER;
    tx_buf[1] = ADDR_SLAVE;
    tx_buf[2] = CMD_OTA_CHUNK_REQ_ID;
    
    uint16_t payload_len = 4 + 1 + dados.size();
    tx_buf[3] = payload_len & 0xFF;
    tx_buf[4] = (payload_len >> 8) & 0xFF;

    memcpy(&tx_buf[5], &chunk_pkt, payload_len);

    uint16_t crc = crc16_ccitt(tx_buf, 5 + payload_len);
    tx_buf[5 + payload_len] = crc & 0xFF;       
    tx_buf[5 + payload_len + 1] = crc >> 8;     

    if (spi_transaction() < 0) return false;

    if (!esperar_ack(CMD_OTA_CHUNK_REQ_ID)) {
        return false;
    }
    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;

    fd_spi = open(DEVICE, O_RDWR);
    uint8_t mode = SPI_MODE; uint8_t bits = BITS; uint32_t speed = SPEED;
    ioctl(fd_spi, SPI_IOC_WR_MODE, &mode);
    ioctl(fd_spi, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd_spi, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    HalGpio slave_ready(GPIO_READY_PIN, HalGpio::Direction::Input, HalGpio::Edge::Rising);
    slave_ready_ptr = &slave_ready;

    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    uint32_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    printf("--- Iniciando OTA Seguro (DEBUG) ---\n");

    // 1. START
    printf(">> Enviando START...\n");
    memset(tx_buf, 0, 64);
    tx_buf[2] = CMD_OTA_START_REQ_ID; 
    tx_buf[3] = 4; tx_buf[4] = 0;     
    memcpy(&tx_buf[5], &file_size, 4); 
    uint16_t crc = crc16_ccitt(tx_buf, 9);
    tx_buf[9] = crc & 0xFF; tx_buf[10] = crc >> 8;
    
    spi_transaction();

    printf(">> Aguardando ACK do START...\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); 
    
    if (!esperar_ack(CMD_OTA_START_REQ_ID)) {
        printf("[FALHA] Abortando.\n");
        close(fd_spi);
        return 1;
    }

    // 2. CHUNKS
    printf(">> Enviando Chunks...\n");
    std::vector<uint8_t> buffer(CHUNK_DATA_SIZE);
    uint32_t offset = 0;

    while (file.read((char*)buffer.data(), CHUNK_DATA_SIZE) || file.gcount() > 0) {
        size_t bytes_read = file.gcount();
        buffer.resize(bytes_read);
        
        bool chunk_gravado = false;
        for (int retry = 0; retry < 3; retry++) {
            if (enviar_chunk_seguro(offset, buffer)) {
                chunk_gravado = true;
                break;
            }
            printf("\n[RETRY] Falha no offset %d.\n", offset);
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
        }

        if (!chunk_gravado) {
            printf("\n[ERRO FATAL] Impossível gravar offset %d.\n", offset);
            close(fd_spi);
            return 1;
        }

        offset += bytes_read;
        printf("\rProgresso: %d / %d bytes [OK]", offset, file_size);
        fflush(stdout);
        buffer.resize(CHUNK_DATA_SIZE); 
    }

    // 3. END
    printf("\n>> Enviando END...\n");
    memset(tx_buf, 0, 64);
    tx_buf[0] = ADDR_MASTER; tx_buf[1] = ADDR_SLAVE; tx_buf[2] = CMD_OTA_END_REQ_ID; 
    uint16_t crc_end = crc16_ccitt(tx_buf, 5);
    tx_buf[5] = crc_end & 0xFF; tx_buf[6] = crc_end >> 8;
    
    spi_transaction();
    esperar_ack(CMD_OTA_END_REQ_ID);

    close(fd_spi);
    return 0;
}