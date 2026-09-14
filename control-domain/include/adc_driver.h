#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

#include <zephyr/kernel.h>
#include "sensor_data.h"

extern struct k_msgq sensor_data_q;

int adc_driver_init(void);
void adc_thread_entry(void* p1, void* p2, void* p3);

#endif
