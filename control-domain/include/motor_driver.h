#ifndef MOTOR_DRIVER_H
#define MOTOR_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

int motor_init(void);
void motor_enable(bool enable);
void motor_run(uint32_t flow_rate_ml_h, uint8_t syringe_diameter);
void motor_stop(void);

#endif
