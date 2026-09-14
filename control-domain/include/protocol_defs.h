#ifndef PROTOCOL_DEFS_H
#define PROTOCOL_DEFS_H

#include <stdint.h>

typedef enum
{
    STATE_POWER_ON,
    STATE_IDLE,
    STATE_RUNNING,
    STATE_BOLUS,
    STATE_PURGE,
    STATE_PAUSED,
    STATE_KVO,
    STATE_END_INFUSION,
    STATE_ALARM_BUBBLE,
    STATE_ALARM_OCCLUSION,
    STATE_ALARM_DOOR,
    STATE_OFF
} pump_state_t;

typedef enum
{
    CMD_NONE = 0,
    CMD_START,
    CMD_PAUSE,
    CMD_STOP,
    CMD_SET_BOLUS,
    CMD_SET_PURGE,
    CMD_SET_RATE,
    CMD_SET_VOLUME,
    CMD_SET_DIAMETER,
    // CMD_SET_MODE,
    CMD_CLEAR_ALARM
} command_id_t;

/* =====================================================================
 * Limites terapeuticos — a autoridade final do dominio critico
 * =====================================================================
 * O MCU e a ultima linha entre um comando de rede e o atuador. Se ele
 * aceita qualquer vazao que chegue, o isolamento de dominios protege o
 * DETERMINISMO mas nao o PACIENTE: um gateway comprometido comanda dose
 * arbitraria.
 *
 * Verificado em bancada antes desta mudanca (experimento E13): pedidos
 * de 600, 1200, 5000 e 65000 mL/h foram TODOS aceitos sem saturacao.
 *
 * Os limites abaixo sao de ENGENHARIA, derivados do que a mecanica
 * suporta -- RATE_PURGE_DEFAULT ja usava 1200 mL/h como vazao maxima de
 * purga. Uma faixa CLINICA precisa vir de norma (NBR IEC 60601-2-24) e
 * do protocolo terapeutico, e deveria ser configuravel por prescricao.
 * Isto aqui e o piso de seguranca, nao a prescricao.
 * ===================================================================== */
#define RATE_MIN_ML_H        0U      /* 0 = parado, sempre valido      */
#define RATE_MAX_ML_H        1200U   /* teto mecanico do conjunto      */
#define VOLUME_MAX_ML        1000.0f /* volume alvo maximo por terapia */
#define SYRINGE_DIAM_MIN_MM  5U
#define SYRINGE_DIAM_MAX_MM  40U

typedef struct
{
    command_id_t id;
    float param;
    float param2;
} pump_cmd_t;

typedef struct
{
    pump_state_t current_state;
    float infused_volume;
    float target_volume;
    uint32_t configured_flow_rate;
    uint8_t syringe_diameter;
    // uint8_t infusion_mode;
    uint32_t pressure_mmhg;
    uint8_t alarm_code;
} pump_status_t;

#endif
