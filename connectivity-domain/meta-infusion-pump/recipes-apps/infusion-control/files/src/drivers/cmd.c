#include "cmd.h"
#include "utl_io.h"
#include "utl_crc16.h"

// [HELPER] Função para escrever o SOF automaticamente
static void write_sof(uint8_t** pbuf)
{
    utl_io_put8_tl_ap(CMD_SOF_1_BYTE, *pbuf);
    utl_io_put8_tl_ap(CMD_SOF_2_BYTE, *pbuf);
}

bool cmd_decode(uint8_t* buffer, size_t size, uint8_t* src, uint8_t* dst, cmd_ids_t* id, cmd_cmds_t* decoded_cmd)
{
    if(size < CMD_HDR_SIZE + CMD_TRAILER_SIZE)
        return false;

    uint8_t* pbuf = buffer;

    // [MUDANÇA CRÍTICA DECODE]
    // Pula os 2 bytes de SOF (AA 55) para chegar no DST.
    // O parser do hub.c já validou que eles existem.
    utl_io_get8_fl_ap(pbuf); // Pula SOF1
    utl_io_get8_fl_ap(pbuf); // Pula SOF2

    // Agora lê o resto normalmente
    *dst = utl_io_get8_fl_ap(pbuf);
    *src = utl_io_get8_fl_ap(pbuf);
    uint8_t raw_id = utl_io_get8_fl_ap(pbuf);
    *id = (cmd_ids_t) raw_id;
    uint16_t payload_size = utl_io_get16_fl_ap(pbuf);

    if(*id >= CMD_NUM_CMDS)
        return false;

    size_t real_packet_len = payload_size + CMD_HDR_SIZE + CMD_TRAILER_SIZE;
    if(size < real_packet_len)
        return false;

    // O CRC agora é calculado sobre TODO o pacote (incluindo SOF)
    uint16_t crc = utl_io_get16_fl(buffer + real_packet_len - 2);
    uint16_t crc_calc = utl_crc16_data(buffer, real_packet_len - 2, 0xFFFF);

    if(crc != crc_calc)
        return false;

    // ... (Switch e Tabela de Decoders permanecem iguais ao seu original) ...
    // Vou resumir aqui, mas MANTENHA o conteúdo do seu switch e tabela originais
    switch(*id)
    {
    case CMD_VERSION_REQ_ID:
    case CMD_VERSION_RES_ID:
    case CMD_GET_STATUS_REQ_ID:
    case CMD_GET_STATUS_RES_ID:
    case CMD_SET_CONFIG_REQ_ID:
    case CMD_SET_CONFIG_RES_ID:
    case CMD_ACTION_RUN_REQ_ID:
    case CMD_ACTION_PAUSE_REQ_ID:
    case CMD_ACTION_ABORT_REQ_ID:
    case CMD_ACTION_PURGE_REQ_ID:
    case CMD_ACTION_BOLUS_REQ_ID:
    case CMD_ACTION_RES_ID:
    case CMD_OTA_START_REQ_ID:
    case CMD_OTA_CHUNK_REQ_ID:
    case CMD_OTA_END_REQ_ID:
    case CMD_OTA_RES_ID:
    case CMD_GET_METRICS_REQ_ID:
    case CMD_GET_METRICS_RES_ID:
        break;
    default:
        return false;
    }

    bool (*decoders[])(cmd_cmds_t* cmd, uint8_t* buffer, size_t size) = {
        [CMD_VERSION_REQ_ID] = cmd_decode_version_req,
        [CMD_VERSION_RES_ID] = cmd_decode_version_res,
        [CMD_GET_STATUS_REQ_ID] = cmd_decode_status_req,
        [CMD_GET_STATUS_RES_ID] = cmd_decode_status_res,
        [CMD_SET_CONFIG_REQ_ID] = cmd_decode_config_req,
        [CMD_SET_CONFIG_RES_ID] = cmd_decode_config_res,
        [CMD_ACTION_RES_ID] = cmd_decode_action_res,
        [CMD_ACTION_RUN_REQ_ID] = cmd_decode_action_run_req,
        [CMD_ACTION_PAUSE_REQ_ID] = cmd_decode_action_pause_req,
        [CMD_ACTION_ABORT_REQ_ID] = cmd_decode_action_abort_req,
        [CMD_ACTION_PURGE_REQ_ID] = cmd_decode_action_purge_req,
        [CMD_ACTION_BOLUS_REQ_ID] = cmd_decode_action_bolus_req,
        [CMD_OTA_START_REQ_ID] = cmd_decode_ota_generic,
        [CMD_OTA_CHUNK_REQ_ID] = cmd_decode_ota_generic,
        [CMD_OTA_END_REQ_ID] = cmd_decode_ota_generic,
        [CMD_OTA_RES_ID] = cmd_decode_ota_generic,
        [CMD_GET_METRICS_REQ_ID] = cmd_decode_get_metrics_req,
        [CMD_GET_METRICS_RES_ID] = cmd_decode_get_metrics_res,
    };

    if(decoders[(uint8_t) *id] == NULL)
        return false;

    // Passamos pbuf, que agora aponta para o INÍCIO DO PAYLOAD
    return decoders[(uint8_t) *id](decoded_cmd, pbuf, payload_size);
}

// ... (Mantenha a função cmd_encode igual, ela apenas chama os específicos) ...

/* --- Metricas de tempo real do MCU --- */
bool cmd_encode_get_metrics_req(uint8_t dst, uint8_t src, cmd_get_metrics_req_t* cmd, uint8_t* buffer, size_t* size)
{
    (void) cmd;
    uint8_t* pbuf = buffer;
    write_sof(&pbuf);
    utl_io_put8_tl_ap(dst, pbuf);
    utl_io_put8_tl_ap(src, pbuf);
    utl_io_put8_tl_ap(CMD_GET_METRICS_REQ_ID, pbuf);
    utl_io_put16_tl_ap(CMD_GET_METRICS_REQ_SIZE, pbuf);
    utl_io_put16_tl_ap(utl_crc16_data(buffer, (pbuf - buffer), 0xFFFF), pbuf);
    *size = (pbuf - buffer);
    return true;
}

bool cmd_encode_get_metrics_res(uint8_t dst, uint8_t src, cmd_get_metrics_res_t* cmd, uint8_t* buffer, size_t* size)
{
    uint8_t* pbuf = buffer;
    write_sof(&pbuf);
    utl_io_put8_tl_ap(dst, pbuf);
    utl_io_put8_tl_ap(src, pbuf);
    utl_io_put8_tl_ap(CMD_GET_METRICS_RES_ID, pbuf);
    utl_io_put16_tl_ap(CMD_GET_METRICS_RES_SIZE, pbuf);
    utl_io_put32_tl_ap(cmd->m.frames_seen, pbuf);
    utl_io_put32_tl_ap(cmd->m.crc_errors, pbuf);
    utl_io_put32_tl_ap(cmd->m.driver_errors, pbuf);
    utl_io_put32_tl_ap(cmd->m.spi_resets, pbuf);
    utl_io_put32_tl_ap(cmd->m.recoveries, pbuf);
    utl_io_put32_tl_ap(cmd->m.max_recovery_us, pbuf);
    utl_io_put32_tl_ap(cmd->m.cmd_count, pbuf);
    utl_io_put32_tl_ap(cmd->m.cmd_lat_max_us, pbuf);
    utl_io_put32_tl_ap(cmd->m.cmd_lat_avg_us, pbuf);
    utl_io_put32_tl_ap(cmd->m.loop_count, pbuf);
    utl_io_put32_tl_ap(cmd->m.loop_period_min_us, pbuf);
    utl_io_put32_tl_ap(cmd->m.loop_period_max_us, pbuf);
    utl_io_put32_tl_ap(cmd->m.deadline_misses, pbuf);
    utl_io_put16_tl_ap(utl_crc16_data(buffer, (pbuf - buffer), 0xFFFF), pbuf);
    *size = (pbuf - buffer);
    return true;
}

bool cmd_decode_get_metrics_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    (void) cmd; (void) buffer;
    return size == 0;
}

bool cmd_decode_get_metrics_res(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    uint8_t* pbuf = buffer;
    if(size != CMD_GET_METRICS_RES_SIZE)
        return false;
    cmd->metrics_res.m.frames_seen = utl_io_get32_fl_ap(pbuf);
    cmd->metrics_res.m.crc_errors = utl_io_get32_fl_ap(pbuf);
    cmd->metrics_res.m.driver_errors = utl_io_get32_fl_ap(pbuf);
    cmd->metrics_res.m.spi_resets = utl_io_get32_fl_ap(pbuf);
    cmd->metrics_res.m.recoveries = utl_io_get32_fl_ap(pbuf);
    cmd->metrics_res.m.max_recovery_us = utl_io_get32_fl_ap(pbuf);
    cmd->metrics_res.m.cmd_count = utl_io_get32_fl_ap(pbuf);
    cmd->metrics_res.m.cmd_lat_max_us = utl_io_get32_fl_ap(pbuf);
    cmd->metrics_res.m.cmd_lat_avg_us = utl_io_get32_fl_ap(pbuf);
    cmd->metrics_res.m.loop_count = utl_io_get32_fl_ap(pbuf);
    cmd->metrics_res.m.loop_period_min_us = utl_io_get32_fl_ap(pbuf);
    cmd->metrics_res.m.loop_period_max_us = utl_io_get32_fl_ap(pbuf);
    cmd->metrics_res.m.deadline_misses = utl_io_get32_fl_ap(pbuf);
    return true;
}

bool cmd_encode(uint8_t* buffer, size_t* size, uint8_t* src, uint8_t* dst, cmd_ids_t* id, cmd_cmds_t* encoded_cmd)
{
    // ... (Copie o switch do seu arquivo original) ...
    // Vou colocar o exemplo de um case para você ver:
    bool status = false;
    switch(*id)
    {
    case CMD_VERSION_REQ_ID:
        status = cmd_encode_version_req(*dst, *src, &encoded_cmd->version_req, buffer, size);
        break;
    case CMD_GET_STATUS_REQ_ID:
        status = cmd_encode_status_req(*dst, *src, &encoded_cmd->status_req, buffer, size);
        break;
    case CMD_SET_CONFIG_REQ_ID:
        status = cmd_encode_config_req(*dst, *src, &encoded_cmd->config_req, buffer, size);
        break;
    case CMD_ACTION_RUN_REQ_ID:
        status = cmd_encode_action_run_req(*dst, *src, &encoded_cmd->run_req, buffer, size);
        break;
    case CMD_ACTION_PAUSE_REQ_ID:
        status = cmd_encode_action_pause_req(*dst, *src, &encoded_cmd->pause_req, buffer, size);
        break;
    case CMD_ACTION_ABORT_REQ_ID:
        status = cmd_encode_action_abort_req(*dst, *src, &encoded_cmd->abort_req, buffer, size);
        break;
    case CMD_ACTION_PURGE_REQ_ID:
        status = cmd_encode_action_purge_req(*dst, *src, &encoded_cmd->purge_req, buffer, size);
        break;
    case CMD_ACTION_BOLUS_REQ_ID:
        status = cmd_encode_action_bolus_req(*dst, *src, &encoded_cmd->bolus_req, buffer, size);
        break;
    case CMD_VERSION_RES_ID:
        status = cmd_encode_version_res(*dst, *src, &encoded_cmd->version_res, buffer, size);
        break;
    case CMD_GET_STATUS_RES_ID:
        status = cmd_encode_status_res(*dst, *src, &encoded_cmd->status_res, buffer, size);
        break;
    case CMD_SET_CONFIG_RES_ID:
        status = cmd_encode_config_res(*dst, *src, &encoded_cmd->config_res, buffer, size);
        break;
    case CMD_ACTION_RES_ID:
        status = cmd_encode_action_res(*dst, *src, &encoded_cmd->action_res, buffer, size);
        break;
    case CMD_OTA_RES_ID:
        status = cmd_encode_ota_res(*dst, *src, &encoded_cmd->ota_res, buffer, size);
        break;
    case CMD_GET_METRICS_REQ_ID:
        status = cmd_encode_get_metrics_req(*dst, *src, &encoded_cmd->metrics_req, buffer, size);
        break;
    case CMD_GET_METRICS_RES_ID:
        status = cmd_encode_get_metrics_res(*dst, *src, &encoded_cmd->metrics_res, buffer, size);
        break;
    default:
        status = false;
        break;
    }
    return status;
}

// [MUDANÇA CRÍTICA ENCODE] Função genérica de header com SOF
static bool cmd_encode_header_only(uint8_t dst, uint8_t src, cmd_ids_t id, uint8_t* buffer, size_t* size)
{
    uint8_t* pbuf = buffer;

    // 1. Escreve SOF
    write_sof(&pbuf);

    // 2. Escreve Resto do Header
    utl_io_put8_tl_ap(dst, pbuf);
    utl_io_put8_tl_ap(src, pbuf);
    utl_io_put8_tl_ap(id, pbuf);
    utl_io_put16_tl_ap(0, pbuf); // Payload Size 0

    // 3. Calcula CRC (SOF + Header)
    size_t len_so_far = pbuf - buffer;
    utl_io_put16_tl_ap(utl_crc16_data(buffer, len_so_far, 0xFFFF), pbuf);

    *size = (pbuf - buffer);
    return true;
}

// ... Encoders Simples (Chamam cmd_encode_header_only) ...
// (Mantenha as funções wrapper como run_req, pause_req, etc. iguais ao seu código original)
bool cmd_encode_version_req(uint8_t dst, uint8_t src, cmd_version_req_t* cmd, uint8_t* buffer, size_t* size)
{
    return cmd_encode_header_only(dst, src, CMD_VERSION_REQ_ID, buffer, size);
}
bool cmd_encode_status_req(uint8_t dst, uint8_t src, cmd_get_status_req_t* cmd, uint8_t* buffer, size_t* size)
{
    return cmd_encode_header_only(dst, src, CMD_GET_STATUS_REQ_ID, buffer, size);
}
bool cmd_encode_action_run_req(uint8_t dst, uint8_t src, cmd_action_run_req_t* cmd, uint8_t* buffer, size_t* size)
{
    return cmd_encode_header_only(dst, src, CMD_ACTION_RUN_REQ_ID, buffer, size);
}
bool cmd_encode_action_pause_req(uint8_t dst, uint8_t src, cmd_action_pause_req_t* cmd, uint8_t* buffer, size_t* size)
{
    return cmd_encode_header_only(dst, src, CMD_ACTION_PAUSE_REQ_ID, buffer, size);
}
bool cmd_encode_action_abort_req(uint8_t dst, uint8_t src, cmd_action_abort_req_t* cmd, uint8_t* buffer, size_t* size)
{
    return cmd_encode_header_only(dst, src, CMD_ACTION_ABORT_REQ_ID, buffer, size);
}
bool cmd_encode_action_purge_req(uint8_t dst, uint8_t src, cmd_action_purge_req_t* cmd, uint8_t* buffer, size_t* size)
{
    return cmd_encode_header_only(dst, src, CMD_ACTION_PURGE_REQ_ID, buffer, size);
}
bool cmd_encode_action_bolus_req(uint8_t dst, uint8_t src, cmd_action_bolus_req_t* cmd, uint8_t* buffer, size_t* size)
{
    uint8_t* pbuf = buffer;
    write_sof(&pbuf);
    utl_io_put8_tl_ap(dst, pbuf);
    utl_io_put8_tl_ap(src, pbuf);
    utl_io_put8_tl_ap(CMD_ACTION_BOLUS_REQ_ID, pbuf);
    utl_io_put16_tl_ap(CMD_ACTION_BOLUS_REQ_SIZE, pbuf);

    utl_io_put32_tl_ap(cmd->payload.bolus_volume, pbuf);
    utl_io_put32_tl_ap(cmd->payload.bolus_rate, pbuf);

    utl_io_put16_tl_ap(utl_crc16_data(buffer, (pbuf - buffer), 0xFFFF), pbuf);
    *size = (pbuf - buffer);

    return true;

}

// [MUDANÇA CRÍTICA ENCODE] Payloads Complexos precisam de write_sof() manual
bool cmd_encode_config_req(uint8_t dst, uint8_t src, cmd_set_config_req_t* cmd, uint8_t* buffer, size_t* size)
{
    uint8_t* pbuf = buffer;

    write_sof(&pbuf); // <--- AQUI

    utl_io_put8_tl_ap(dst, pbuf);
    utl_io_put8_tl_ap(src, pbuf);
    utl_io_put8_tl_ap(CMD_SET_CONFIG_REQ_ID, pbuf);
    utl_io_put16_tl_ap(CMD_SET_CONFIG_REQ_SIZE, pbuf);

    utl_io_put32_tl_ap(cmd->config.volume, pbuf);
    utl_io_put32_tl_ap(cmd->config.flow_rate, pbuf);
    utl_io_put8_tl_ap(cmd->config.diameter, pbuf);
    // utl_io_put8_tl_ap(cmd->config.mode, pbuf);

    utl_io_put16_tl_ap(utl_crc16_data(buffer, (pbuf - buffer), 0xFFFF), pbuf);
    *size = (pbuf - buffer);
    return true;
}

bool cmd_encode_version_res(uint8_t dst, uint8_t src, cmd_version_res_t* cmd, uint8_t* buffer, size_t* size)
{
    uint8_t* pbuf = buffer;
    write_sof(&pbuf); // <--- AQUI
    utl_io_put8_tl_ap(dst, pbuf);
    utl_io_put8_tl_ap(src, pbuf);
    utl_io_put8_tl_ap(CMD_VERSION_RES_ID, pbuf);
    utl_io_put16_tl_ap(CMD_VERSION_RES_SIZE, pbuf);
    utl_io_put8_tl_ap(cmd->major, pbuf);
    utl_io_put8_tl_ap(cmd->minor, pbuf);
    utl_io_put8_tl_ap(cmd->patch, pbuf);
    utl_io_put16_tl_ap(utl_crc16_data(buffer, (pbuf - buffer), 0xFFFF), pbuf);
    *size = (pbuf - buffer);
    return true;
}

bool cmd_encode_status_res(uint8_t dst, uint8_t src, cmd_get_status_res_t* cmd, uint8_t* buffer, size_t* size)
{
    uint8_t* pbuf = buffer;
    write_sof(&pbuf); // <--- AQUI
    utl_io_put8_tl_ap(dst, pbuf);
    utl_io_put8_tl_ap(src, pbuf);
    utl_io_put8_tl_ap(CMD_GET_STATUS_RES_ID, pbuf);
    utl_io_put16_tl_ap(CMD_GET_STATUS_RES_SIZE, pbuf);
    utl_io_put8_tl_ap(cmd->status_data.current_state, pbuf);
    utl_io_put32_tl_ap(cmd->status_data.volume, pbuf);
    utl_io_put32_tl_ap(cmd->status_data.flow_rate_set, pbuf);
    utl_io_put32_tl_ap(cmd->status_data.pressure, pbuf);
    utl_io_put8_tl_ap(cmd->status_data.alarm_active, pbuf);
    utl_io_put16_tl_ap(utl_crc16_data(buffer, (pbuf - buffer), 0xFFFF), pbuf);
    *size = (pbuf - buffer);
    return true;
}

bool cmd_encode_config_res(uint8_t dst, uint8_t src, cmd_set_config_res_t* cmd, uint8_t* buffer, size_t* size)
{
    uint8_t* pbuf = buffer;
    write_sof(&pbuf); // <--- AQUI
    utl_io_put8_tl_ap(dst, pbuf);
    utl_io_put8_tl_ap(src, pbuf);
    utl_io_put8_tl_ap(CMD_SET_CONFIG_RES_ID, pbuf);
    utl_io_put16_tl_ap(CMD_SET_CONFIG_RES_SIZE, pbuf);
    utl_io_put8_tl_ap(cmd->status, pbuf);
    utl_io_put16_tl_ap(utl_crc16_data(buffer, (pbuf - buffer), 0xFFFF), pbuf);
    *size = (pbuf - buffer);
    return true;
}

bool cmd_encode_action_res(uint8_t dst, uint8_t src, cmd_action_res_t* cmd, uint8_t* buffer, size_t* size)
{
    uint8_t* pbuf = buffer;
    write_sof(&pbuf); // <--- AQUI
    utl_io_put8_tl_ap(dst, pbuf);
    utl_io_put8_tl_ap(src, pbuf);
    utl_io_put8_tl_ap(CMD_ACTION_RES_ID, pbuf);
    utl_io_put16_tl_ap(CMD_ACTION_RES_SIZE, pbuf);
    utl_io_put8_tl_ap(cmd->cmd_req_id, pbuf);
    utl_io_put8_tl_ap(cmd->status, pbuf);
    utl_io_put16_tl_ap(utl_crc16_data(buffer, (pbuf - buffer), 0xFFFF), pbuf);
    *size = (pbuf - buffer);
    return true;
}

bool cmd_encode_ota_res(uint8_t dst, uint8_t src, cmd_action_res_t* cmd, uint8_t* buffer, size_t* size)
{
    uint8_t* pbuf = buffer;
    write_sof(&pbuf); // <--- AQUI
    utl_io_put8_tl_ap(dst, pbuf);
    utl_io_put8_tl_ap(src, pbuf);
    utl_io_put8_tl_ap(CMD_OTA_RES_ID, pbuf);
    utl_io_put16_tl_ap(CMD_OTA_RES_SIZE, pbuf);
    utl_io_put8_tl_ap(cmd->cmd_req_id, pbuf);
    utl_io_put8_tl_ap(cmd->status, pbuf);
    utl_io_put16_tl_ap(utl_crc16_data(buffer, (pbuf - buffer), 0xFFFF), pbuf);
    *size = (pbuf - buffer);
    return true;
}

// [DECODERS ESPECÍFICOS]
// Estes NÂO mudam em relação ao seu original, pois eles só leem o payload.
// O 'cmd_decode' principal já tratou o header e SOF.
// (Mantenha todas as funções cmd_decode_xxx do seu arquivo original aqui)
bool cmd_decode_version_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    return size == 0;
}
bool cmd_decode_status_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    return size == 0;
}
bool cmd_decode_action_run_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    return size == 0;
}
bool cmd_decode_action_pause_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    return size == 0;
}
bool cmd_decode_action_abort_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    return size == 0;
}
bool cmd_decode_action_purge_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    return size == 0;
}
bool cmd_decode_action_bolus_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    uint8_t* pbuf = buffer;
    if(size != CMD_ACTION_BOLUS_REQ_SIZE)
        return false;
    cmd->bolus_req.payload.bolus_volume = utl_io_get32_fl_ap(pbuf);
    cmd->bolus_req.payload.bolus_rate = utl_io_get32_fl_ap(pbuf);
    return true;
}

bool cmd_decode_config_req(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    uint8_t* pbuf = buffer;
    if(size != CMD_SET_CONFIG_REQ_SIZE)
        return false;
    cmd->config_req.config.volume = utl_io_get32_fl_ap(pbuf);
    cmd->config_req.config.flow_rate = utl_io_get32_fl_ap(pbuf);
    cmd->config_req.config.diameter = utl_io_get8_fl_ap(pbuf);
    // cmd->config_req.config.mode = utl_io_get8_fl_ap(pbuf);
    return true;
}

// ... (Mantenha os decoders de resposta: version_res, status_res, etc. iguais ao seu original)
bool cmd_decode_version_res(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    uint8_t* pbuf = buffer;
    if(size != CMD_VERSION_RES_SIZE)
        return false;
    cmd->version_res.major = utl_io_get8_fl_ap(pbuf);
    cmd->version_res.minor = utl_io_get8_fl_ap(pbuf);
    cmd->version_res.patch = utl_io_get8_fl_ap(pbuf);
    return true;
}
bool cmd_decode_status_res(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    uint8_t* pbuf = buffer;
    if(size != CMD_GET_STATUS_RES_SIZE)
        return false;
    cmd->status_res.status_data.current_state = utl_io_get8_fl_ap(pbuf);
    cmd->status_res.status_data.volume = utl_io_get32_fl_ap(pbuf);
    cmd->status_res.status_data.flow_rate_set = utl_io_get32_fl_ap(pbuf);
    cmd->status_res.status_data.pressure = utl_io_get32_fl_ap(pbuf);
    cmd->status_res.status_data.alarm_active = utl_io_get8_fl_ap(pbuf);
    return true;
}
bool cmd_decode_config_res(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    uint8_t* pbuf = buffer;
    if(size != CMD_SET_CONFIG_RES_SIZE)
        return false;
    cmd->config_res.status = (cmd_status_t) utl_io_get8_fl_ap(pbuf);
    return true;
}
bool cmd_decode_action_res(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    uint8_t* pbuf = buffer;
    if(size != CMD_ACTION_RES_SIZE)
        return false;
    cmd->action_res.cmd_req_id = (cmd_ids_t) utl_io_get8_fl_ap(pbuf);
    cmd->action_res.status = (cmd_status_t) utl_io_get8_fl_ap(pbuf);
    return true;
}
bool cmd_decode_ota_generic(cmd_cmds_t* cmd, uint8_t* buffer, size_t size)
{
    return true;
}
