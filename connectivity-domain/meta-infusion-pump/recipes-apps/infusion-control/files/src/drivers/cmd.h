#ifndef CMD_H
#define CMD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Configurações de Tamanho */
#define CMD_MAX_DATA_SIZE 250

// [MUDANÇA 1] Tamanho do Header sobe para 7 (AA+55+DST+SRC+ID+SIZE)
#define CMD_HDR_SIZE       7
#define CMD_TRAILER_SIZE   2
#define FRAME_MAX_CMD_SIZE (CMD_HDR_SIZE + CMD_MAX_DATA_SIZE + CMD_TRAILER_SIZE)
#define CMD_INVALID_ID     255
#define ADDR_MASTER        0x00
#define ADDR_SLAVE         0x01

// [MUDANÇA 2] Definição dos Bytes de Start of Frame
#define CMD_SOF_1_BYTE 0xAA
#define CMD_SOF_2_BYTE 0x55

typedef enum cmd_ids_e
{
    CMD_VERSION_REQ_ID = 0x01,
    CMD_VERSION_RES_ID = 0x02,
    CMD_GET_STATUS_REQ_ID = 0x03,
    CMD_GET_STATUS_RES_ID = 0x04,
    CMD_SET_CONFIG_REQ_ID = 0x10,
    CMD_SET_CONFIG_RES_ID = 0x11,
    CMD_ACTION_RUN_REQ_ID = 0x20,
    CMD_ACTION_PAUSE_REQ_ID = 0x21,
    CMD_ACTION_ABORT_REQ_ID = 0x22,
    CMD_ACTION_PURGE_REQ_ID = 0x23,
    CMD_ACTION_BOLUS_REQ_ID = 0x24,
    CMD_ACTION_RES_ID = 0x2F,
    CMD_GET_METRICS_REQ_ID = 0x30,
    CMD_GET_METRICS_RES_ID = 0x31,
    CMD_OTA_START_REQ_ID = 0x50,
    CMD_OTA_CHUNK_REQ_ID = 0x51,
    CMD_OTA_END_REQ_ID = 0x52,
    CMD_OTA_RES_ID = 0x5F,
} cmd_ids_t;

typedef enum
{
    CMD_OK = 0,
    CMD_ERR_INVALID_STATE,
    CMD_ERR_PARAM_RANGE,
    CMD_ERR_UNKNOWN_CMD,
    CMD_ERR_CHECKSUM,
    CMD_ERR_TIMEOUT,
    CMD_ERR_SYNC,
} cmd_status_t;

/* --- ESTRUTURAS DE PACOTE (WIRE FORMAT) --- */

typedef struct __attribute__((packed)) cmd_hdr_s
{
    // [MUDANÇA 3] Inserir SOF no início da estrutura
    uint8_t sof1; // 0xAA
    uint8_t sof2; // 0x55
    uint8_t dst;
    uint8_t src;
    uint8_t id;
    uint16_t size;
} cmd_hdr_t;

// ... O resto do arquivo (structs de payload) permanece IDÊNTICO ...
// ... Copie o restante do seu arquivo original aqui ...

typedef struct __attribute__((packed)) cmd_trl_s
{
    uint16_t crc;
} cmd_trl_t;

// ... (Mantenha todas as outras structs de payload do seu código original) ...

/* --- PAYLOADS (Mantenha igual ao seu) --- */
typedef struct cmd_version_req_s
{
} cmd_version_req_t;

typedef struct __attribute__((packed)) cmd_version_res_s
{
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} cmd_version_res_t;

typedef struct __attribute__((packed)) cmd_config_payload_s
{
    uint32_t volume;
    uint32_t flow_rate;
    uint8_t diameter;
    // uint8_t mode;
} cmd_config_payload_t;

typedef struct __attribute__((packed)) cmd_set_config_req_s
{
    cmd_config_payload_t config;
} cmd_set_config_req_t;

typedef struct __attribute__((packed)) cmd_set_config_res_s
{
    uint8_t status; 
} cmd_set_config_res_t;

typedef struct __attribute__((packed)) cmd_status_payload_s
{
    uint8_t current_state;
    uint32_t volume;
    uint32_t flow_rate_set;
    uint32_t pressure;
    uint8_t alarm_active;
} cmd_status_payload_t;

typedef struct cmd_get_status_req_s
{
} cmd_get_status_req_t;

typedef struct __attribute__((packed)) cmd_get_status_res_s
{
    cmd_status_payload_t status_data;
} cmd_get_status_res_t;

/* Comandos de Ação (Payload Vazio) */
typedef struct cmd_action_run_req_s
{
} cmd_action_run_req_t;

typedef struct cmd_action_pause_req_s
{
} cmd_action_pause_req_t;

typedef struct cmd_action_abort_req_s
{
} cmd_action_abort_req_t;

typedef struct cmd_action_purge_req_s
{
} cmd_action_purge_req_t;

typedef struct __attribute__((packed)) cmd_action_bolus_payload_s
{
    uint32_t bolus_volume;
    uint32_t bolus_rate;
} cmd_action_bolus_payload_t;

typedef struct __attribute__((packed)) cmd_action_bolus_req_s
{
    cmd_action_bolus_payload_t payload;
} cmd_action_bolus_req_t;

/* ---------------------------------------------------------------------
 * Metricas de tempo real do MCU
 * ---------------------------------------------------------------------
 * Payload de TRANSPORTE, distinto do hub_metrics_t interno. Duas razoes:
 *
 *  1. hub_metrics_t tem um uint64_t (soma das latencias) e o utl_io so
 *     tem primitivas de 32 bits. Aqui vai a MEDIA ja calculada.
 *
 *  2. Restricao de tamanho: o barramento move 64 bytes por transacao
 *     (SPI_PACKET_SIZE). Com header (7) e CRC (2) sobram 55 -- ou seja,
 *     no maximo 13 campos de 32 bits. Sao 13 exatamente: 52 bytes de
 *     payload, quadro de 61.
 *
 * Acrescentar campo aqui exige aumentar SPI_PACKET_SIZE nos DOIS lados.
 * ------------------------------------------------------------------- */
typedef struct __attribute__((packed)) cmd_metrics_payload_s
{
    uint32_t frames_seen;
    uint32_t crc_errors;
    uint32_t driver_errors;
    uint32_t spi_resets;
    uint32_t recoveries;
    uint32_t max_recovery_us;
    uint32_t cmd_count;
    uint32_t cmd_lat_max_us;
    uint32_t cmd_lat_avg_us;
    uint32_t loop_count;
    uint32_t loop_period_min_us;
    uint32_t loop_period_max_us;
    uint32_t deadline_misses;
} cmd_metrics_payload_t;

typedef struct cmd_get_metrics_req_s
{
} cmd_get_metrics_req_t;

typedef struct __attribute__((packed)) cmd_get_metrics_res_s
{
    cmd_metrics_payload_t m;
} cmd_get_metrics_res_t;

typedef struct __attribute__((packed)) cmd_action_res_s
{
    uint8_t cmd_req_id;
    uint8_t status;
} cmd_action_res_t;

typedef enum cmd_sizes_e
{
    CMD_VERSION_REQ_SIZE = 0,
    CMD_VERSION_RES_SIZE = sizeof(cmd_version_res_t),
    CMD_GET_STATUS_REQ_SIZE = 0,
    CMD_GET_STATUS_RES_SIZE = sizeof(cmd_get_status_res_t),
    CMD_SET_CONFIG_REQ_SIZE = sizeof(cmd_set_config_req_t),
    CMD_SET_CONFIG_RES_SIZE = sizeof(cmd_set_config_res_t),
    CMD_ACTION_REQ_SIZE = 0,
    CMD_ACTION_RES_SIZE = sizeof(cmd_action_res_t),
    CMD_ACTION_BOLUS_REQ_SIZE = sizeof(cmd_action_bolus_req_t),
    CMD_OTA_RES_SIZE = sizeof(cmd_action_res_t),
    CMD_GET_METRICS_REQ_SIZE = 0,
    CMD_GET_METRICS_RES_SIZE = sizeof(cmd_get_metrics_res_t),
} cmd_sizes_t;

typedef union cmd_cmds_u
{
    cmd_version_req_t version_req;
    cmd_version_res_t version_res;
    cmd_get_status_req_t status_req;
    cmd_get_status_res_t status_res;
    cmd_set_config_req_t config_req;
    cmd_set_config_res_t config_res;
    cmd_action_run_req_t run_req;
    cmd_action_pause_req_t pause_req;
    cmd_action_abort_req_t abort_req;
    cmd_action_purge_req_t purge_req;
    cmd_action_bolus_req_t bolus_req;
    cmd_action_res_t action_res;
    cmd_action_res_t ota_res;
    cmd_get_metrics_req_t metrics_req;
    cmd_get_metrics_res_t metrics_res;
} cmd_cmds_t;

#define CMD_NUM_CMDS 0x60

bool cmd_decode(uint8_t* buffer, size_t size, uint8_t* src, uint8_t* dst, cmd_ids_t* id, cmd_cmds_t* decoded_cmd);
bool cmd_encode(uint8_t* buffer, size_t* size, uint8_t* src, uint8_t* dst, cmd_ids_t* id, cmd_cmds_t* encoded_cmd);

// ... (Mantenha os protótipos de encoders/decoders específicos) ...
bool cmd_encode_version_req(uint8_t dst, uint8_t src, cmd_version_req_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_encode_version_res(uint8_t dst, uint8_t src, cmd_version_res_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_encode_status_req(uint8_t dst, uint8_t src, cmd_get_status_req_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_encode_status_res(uint8_t dst, uint8_t src, cmd_get_status_res_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_encode_config_req(uint8_t dst, uint8_t src, cmd_set_config_req_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_encode_config_res(uint8_t dst, uint8_t src, cmd_set_config_res_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_encode_action_run_req(uint8_t dst, uint8_t src, cmd_action_run_req_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_encode_action_pause_req(uint8_t dst, uint8_t src, cmd_action_pause_req_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_encode_action_abort_req(uint8_t dst, uint8_t src, cmd_action_abort_req_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_encode_action_purge_req(uint8_t dst, uint8_t src, cmd_action_purge_req_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_encode_action_bolus_req(uint8_t dst, uint8_t src, cmd_action_bolus_req_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_encode_get_metrics_req(uint8_t dst, uint8_t src, cmd_get_metrics_req_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_encode_get_metrics_res(uint8_t dst, uint8_t src, cmd_get_metrics_res_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_decode_get_metrics_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);
bool cmd_decode_get_metrics_res(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);
bool cmd_encode_action_res(uint8_t dst, uint8_t src, cmd_action_res_t* cmd, uint8_t* buffer, size_t* size);
bool cmd_encode_ota_res(uint8_t dst, uint8_t src, cmd_action_res_t* cmd, uint8_t* buffer, size_t* size);

bool cmd_decode_version_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);
bool cmd_decode_version_res(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);
bool cmd_decode_status_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);
bool cmd_decode_status_res(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);
bool cmd_decode_config_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);
bool cmd_decode_config_res(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);
bool cmd_decode_action_run_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);
bool cmd_decode_action_pause_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);
bool cmd_decode_action_abort_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);
bool cmd_decode_action_purge_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);
bool cmd_decode_action_bolus_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);
bool cmd_decode_action_res(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);

uint16_t crc16_ccitt(const uint8_t* data, size_t length);
bool cmd_decode_ota_generic(cmd_cmds_t* cmd, uint8_t* buffer, size_t size);

// ... (Structs OTA) ...
typedef struct __attribute__((packed))
{
    uint32_t total_size;
} cmd_ota_start_t;

typedef struct __attribute__((packed))
{
    uint32_t offset;
    uint8_t len;
    uint8_t data[48];
} cmd_ota_chunk_t;

typedef struct
{
} cmd_ota_end_t;

#endif
