#ifndef LOGIC_ENGINE_H
#define LOGIC_ENGINE_H

#include <zephyr/kernel.h>
#include "protocol_defs.h"

/* * Interface Pública:
 * O Hub vai incluir isso para poder dar 'put' na fila de comandos.
 */
extern struct k_msgq cmd_queue;

void logic_thread_entry(void* p1, void* p2, void* p3);

#endif
