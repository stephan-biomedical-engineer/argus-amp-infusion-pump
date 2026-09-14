#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include "protocol_defs.h"

typedef struct
{
    int32_t bolha_mv;
    int32_t oclusao_mv;
    int32_t volume_pot_mv;
    int32_t timestamp;
} sensor_packet_t;

#endif
