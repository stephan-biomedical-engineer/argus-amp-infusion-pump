#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <stdint.h>
#include <stddef.h>

void ota_start(uint32_t total_size);
int ota_write_chunk(uint8_t* data, size_t len);
void ota_finish(void);

#endif
