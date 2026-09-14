#ifndef HUB_H
#define HUB_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include "protocol_defs.h"
#include "cmd.h"

#define HUB_THREAD_STACK_SIZE 4096
#define HUB_THREAD_PRIORITY   1

// --- VARIÁVEIS DO PARSER (MÁQUINA DE ESTADOS) ---
typedef enum
{
    STATE_WAIT_SOF1,
    STATE_WAIT_SOF2,
    STATE_READ_HEADER,
    STATE_READ_PAYLOAD,
    STATE_READ_CRC
} parser_state_t;

typedef enum {
    LOGIC_OK = 0,
    LOGIC_ERR_INVALID_STATE,
    LOGIC_ERR_NOT_ALLOWED,
} logic_result_t;

typedef struct {
    cmd_ids_t cmd_id;
    logic_result_t result;
} logic_ack_t;


extern volatile logic_result_t logic_last_result;
extern volatile command_id_t logic_last_cmd;
extern struct k_sem logic_cmd_sem;

/* =====================================================================
 * Metricas de tempo real do dominio critico
 * =====================================================================
 * Existem para responder ao que os revisores pediram nominalmente:
 * latencia, jitter, deadline miss rate, WCET, tempo de deteccao e tempo
 * de recuperacao -- do lado do MCU, que o gateway nao consegue medir.
 *
 * Todos os campos sao acumuladores simples atualizados no caminho
 * critico. Leitura e feita por CMD_GET_METRICS.
 *
 * A LEITURA E DESTRUTIVA: apos copiar, os acumuladores sao zerados.
 * Assim cada leitura devolve o intervalo desde a leitura anterior, e a
 * campanha consegue atribuir numeros a um cenario especifico. Sem isso
 * tudo seria acumulado desde o boot, e o ultimo cenario carregaria os
 * erros de todos os anteriores.
 *
 * Unidade: microssegundos, convertidos de ciclos por
 * k_cyc_to_us_near32(). Ciclos sao lidos com k_cycle_get_32(), que no
 * Cortex-M usa o contador de ciclos do proprio nucleo -- resolucao bem
 * abaixo do microssegundo e custo de uma leitura de registrador.
 * ===================================================================== */
typedef struct
{
    /* --- Integridade do barramento --- */
    uint32_t frames_seen;      /* quadros entregues ao decodificador   */
    uint32_t crc_errors;       /* rejeitados por CRC/estrutura         */
    uint32_t driver_errors;    /* spi_transceive() devolveu erro       */
    uint32_t spi_resets;       /* execucoes do auto-healing            */

    /* --- Deteccao e recuperacao (E6 da campanha) --- */
    uint32_t recoveries;       /* rejeicoes seguidas de quadro valido  */
    uint32_t last_crc_fail_us; /* instante da ultima rejeicao          */
    uint32_t last_recovery_us; /* rejeicao -> proximo quadro valido    */
    uint32_t max_recovery_us;  /* pior caso observado                  */

    /* --- Latencia comando -> atuacao --- */
    /* Do quadro decodificado ate motor_run()/motor_stop() retornar. E o
     * caminho de SOFTWARE, onde o RTOS pode de fato sofrer sob carga.
     * Nao confundir com o jitter do pulso do motor, que sai de timer em
     * hardware e e quase imune a carga de CPU. */
    uint32_t cmd_count;
    uint32_t cmd_lat_last_us;
    uint32_t cmd_lat_max_us;   /* WCET observado deste caminho         */
    uint64_t cmd_lat_sum_us;   /* para a media, sem guardar amostras   */

    /* --- Cadencia do laco de controle --- */
    /*
     * ATENCAO a semantica. A versao anterior media o intervalo entre
     * iteracoes CONSECUTIVAS, e isso estava errado: o k_poll tem duas
     * saidas com naturezas diferentes.
     *
     *   - TIMEOUT  -> bloqueou os 100 ms pedidos. E a cadencia do laco.
     *   - EVENTO   -> retornou na hora porque havia comando na fila.
     *                 Dezenas de microssegundos.
     *
     * Misturar as duas populacoes num max-min produzia "jitter de
     * 100 ms", que nao significa nada. Em campanha so de leitura o
     * defeito ficava invisivel (nenhum evento acordava o laco); com o
     * modo mix ele apareceu na hora.
     *
     * Agora medimos o TEMPO QUE O k_poll DE FATO BLOQUEOU, e so
     * contabilizamos como cadencia as saidas por timeout. O desvio em
     * relacao a LOOP_PERIOD_US e o jitter real do temporizador do RTOS
     * sob carga -- que e o numero que interessa ao artigo.
     */
    uint32_t loop_count;         /* iteracoes que sairam por TIMEOUT    */
    uint32_t loop_period_last_us;
    uint32_t loop_period_min_us; /* menor espera observada num timeout  */
    uint32_t loop_period_max_us; /* maior espera observada num timeout  */
    uint32_t deadline_misses;    /* timeouts acima de LOOP_DEADLINE_US  */
} hub_metrics_t;

/* Periodo alvo do laco e o timeout do k_poll (100 ms). A margem de
 * tolerancia define o que conta como deadline miss. */
#define LOOP_PERIOD_US   100000U
#define LOOP_DEADLINE_US 110000U   /* 10% de folga */

/* Copia atomica-o-suficiente das metricas (leitura fora do caminho
 * critico; campos independentes, sem invariante entre eles). */
void hub_get_metrics(hub_metrics_t* out);

/* Chamado pelo logic_engine a cada iteracao do laco e a cada comando
 * processado. Declarados aqui para nao duplicar a struct. */
/* wait_us: quanto o k_poll bloqueou de fato.
 * was_timeout: true se saiu por esgotamento do prazo, false se por
 *              evento. Iteracoes por evento nao entram na cadencia. */
void hub_metrics_loop_tick(uint32_t wait_us, bool was_timeout);
void hub_metrics_cmd_latency(uint32_t start_cycles);

int hub_init(void);

void hub_set_status(const pump_status_t* status);

void hub_thread_entry(void* p1, void* p2, void* p3);

int hub_get_command(pump_cmd_t* cmd);

#endif
