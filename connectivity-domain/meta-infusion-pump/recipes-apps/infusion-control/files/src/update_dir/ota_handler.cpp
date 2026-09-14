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
#include "spi_config.hpp"

extern "C"
{
#include "cmd.h"
#include "utl_crc16.h"
}

static const char* DEVICE = "/dev/spidev0.1";
static const int GPIO_READY_PIN = 5;
static const uint32_t SPEED = argus_spi::OTA_SPEED_HZ;   // ver spi_config.hpp
static const int CHUNK_DATA_SIZE = 48;

// --- PROTOCOLO V2 ---
#define CMD_SOF_1 0xAA
#define CMD_SOF_2 0x55
// SEU CMD.C USA 0xFFFF (PADRÃO CCITT)
#define CRC_SEED 0xFFFF

int fd_spi;
HalGpio* slave_ready_ptr;
uint8_t tx_buf[300];
uint8_t rx_buf[300];

int spi_transaction()
{
    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long) tx_buf;
    tr.rx_buf = (unsigned long) rx_buf;
    tr.len = 64;
    tr.speed_hz = SPEED;
    tr.bits_per_word = 8;

    int retries = 500;
    while(!slave_ready_ptr->get())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(--retries <= 0)
        {
            printf("\n[FATAL] Timeout Hardware: STM32 não levantou pino Ready!\n");
            return -1;
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    if(ioctl(fd_spi, SPI_IOC_MESSAGE(1), &tr) < 1)
        return -1;
    return 0;
}

bool esperar_ack(uint8_t cmd_esperado)
{
    int tentativas = 50;

    while(tentativas--)
    {
        memset(tx_buf, 0, 64);
        if(spi_transaction() < 0)
            return false;

        // Scanner V2
        int sof_index = -1;
        for(int i = 0; i < (64 - 7); i++)
        {
            if(rx_buf[i] == CMD_SOF_1 && rx_buf[i + 1] == CMD_SOF_2)
            {
                sof_index = i;
                break;
            }
        }

        if(sof_index >= 0)
        {
            uint8_t* p_pkt = &rx_buf[sof_index];
            uint8_t id_recebido = p_pkt[4]; // [0]AA [1]55 [2]DST [3]SRC [4]ID

            if(id_recebido == CMD_OTA_RES_ID)
            {
                // [5]LenL [6]LenH [7]ReqID [8]Status
                uint8_t req_originaria = p_pkt[7];
                uint8_t status = p_pkt[8];

                if(req_originaria == cmd_esperado && status == 0)
                {
                    return true;
                }
                else
                {
                    printf("-> NACK (Req: %02X Status: %d)\n", req_originaria, status);
                    return false;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    printf("\n   [TIMEOUT] ACK nao recebido.\n");
    return false;
}

// --- CORREÇÃO CRÍTICA AQUI ---
void finalizar_pacote_v2(int payload_end_idx)
{
    // O cmd.c do STM32 calcula o CRC sobre TODO o buffer recebido (incluindo AA 55)
    // até os 2 últimos bytes.
    //
    // tx_buf[0] = AA
    // ...
    // tx_buf[payload_end_idx] será CRC_L

    // Calculamos CRC do índice 0 até o fim do payload
    uint16_t crc = utl_crc16_data(tx_buf, payload_end_idx, CRC_SEED);

    tx_buf[payload_end_idx] = crc & 0xFF;
    tx_buf[payload_end_idx + 1] = crc >> 8;
}

bool enviar_chunk_seguro(uint32_t offset, std::vector<uint8_t>& dados)
{
    memset(tx_buf, 0, 64);

    tx_buf[0] = CMD_SOF_1;
    tx_buf[1] = CMD_SOF_2;
    tx_buf[2] = ADDR_SLAVE;
    tx_buf[3] = ADDR_MASTER;
    tx_buf[4] = CMD_OTA_CHUNK_REQ_ID;

    uint16_t payload_len = 4 + 1 + dados.size();
    tx_buf[5] = payload_len & 0xFF;
    tx_buf[6] = (payload_len >> 8) & 0xFF;

    int idx = 7;
    memcpy(&tx_buf[idx], &offset, 4);
    idx += 4;
    tx_buf[idx++] = (uint8_t) dados.size();
    memcpy(&tx_buf[idx], dados.data(), dados.size());
    idx += dados.size();

    finalizar_pacote_v2(idx); // idx agora aponta para onde vai o CRC

    if(spi_transaction() < 0)
        return false;
    if(!esperar_ack(CMD_OTA_CHUNK_REQ_ID))
        return false;

    return true;
}

int main(int argc, char* argv[])
{
    if(argc < 2)
    {
        printf("Uso: stm32-updater <bin>\n");
        return 1;
    }

    fd_spi = open(DEVICE, O_RDWR);
    if(fd_spi < 0)
    {
        perror("SPI");
        return 1;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = SPEED;
    ioctl(fd_spi, SPI_IOC_WR_MODE, &mode);
    ioctl(fd_spi, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd_spi, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    HalGpio slave_ready(GPIO_READY_PIN, HalGpio::Direction::Input, HalGpio::Edge::Rising, false, "/dev/gpiochip0");
    slave_ready_ptr = &slave_ready;

    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if(!file.is_open())
    {
        printf("Erro Arquivo\n");
        return 1;
    }
    uint32_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    printf("--- STM32 Updater V2.2 (Fix CRC Scope) ---\n");
    printf("Arquivo: %s (%d bytes)\n", argv[1], file_size);

    // 1. START
    printf("[1/3] Start OTA...\n");
    memset(tx_buf, 0, 64);

    tx_buf[0] = CMD_SOF_1;
    tx_buf[1] = CMD_SOF_2;
    tx_buf[2] = ADDR_SLAVE;
    tx_buf[3] = ADDR_MASTER;
    tx_buf[4] = CMD_OTA_START_REQ_ID;
    tx_buf[5] = 4;
    tx_buf[6] = 0;
    memcpy(&tx_buf[7], &file_size, 4);

    finalizar_pacote_v2(11);

    spi_transaction();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if(!esperar_ack(CMD_OTA_START_REQ_ID))
    {
        printf("[FALHA] Start rejeitado.\n");
        close(fd_spi);
        return 1;
    }

    // 2. CHUNKS
    printf("[2/3] Enviando Dados...\n");
    std::vector<uint8_t> buffer(CHUNK_DATA_SIZE);
    uint32_t offset = 0;

    while(file.read((char*) buffer.data(), CHUNK_DATA_SIZE) || file.gcount() > 0)
    {
        size_t bytes_read = file.gcount();
        buffer.resize(bytes_read);
        bool ok = false;
        for(int r = 0; r < 3; r++)
        {
            if(enviar_chunk_seguro(offset, buffer))
            {
                ok = true;
                break;
            }
            printf("R");
            fflush(stdout);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if(!ok)
        {
            printf("\n[ERRO] Offset %d\n", offset);
            close(fd_spi);
            return 1;
        }
        offset += bytes_read;
        printf("\rProgresso: %d / %d (%d%%)", offset, file_size, (offset * 100) / file_size);
        fflush(stdout);
        buffer.resize(CHUNK_DATA_SIZE);
    }

    // 3. END
    printf("\n[3/3] Finalizando...\n");
    memset(tx_buf, 0, 64);

    tx_buf[0] = CMD_SOF_1;
    tx_buf[1] = CMD_SOF_2;
    tx_buf[2] = ADDR_SLAVE;
    tx_buf[3] = ADDR_MASTER;
    tx_buf[4] = CMD_OTA_END_REQ_ID;
    tx_buf[5] = 0;
    tx_buf[6] = 0;

    finalizar_pacote_v2(7);

    spi_transaction();

    if(esperar_ack(CMD_OTA_END_REQ_ID))
    {
        printf("\nSUCESSO! ACK recebido. O STM32 vai reiniciar em instantes.\n");
        close(fd_spi);
        return 0; // Sucesso Real
    }
    else
    {
        printf("\n[ERRO] O STM32 não respondeu ao comando final.\n");
        close(fd_spi);
        return 1; // Falha Real
    }
}
