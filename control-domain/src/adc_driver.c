#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include "adc_driver.h"

LOG_MODULE_REGISTER(adc_driver, LOG_LEVEL_INF);

/*
 * A fila e SEMPRE definida: o logic_engine faz k_poll sobre ela e
 * precisa que ela exista mesmo quando nao ha ADC na placa.
 */
K_MSGQ_DEFINE(sensor_data_q, sizeof(sensor_packet_t), 10, 4);

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

/*
 * O ADC e OPCIONAL. Sem o no zephyr,user com io-channels (caso do
 * perfil minimo da Nucleo), a thread de aquisicao nao e criada e
 * nenhum pacote de sensor e publicado.
 *
 * Observacao: mesmo no perfil completo, o logic_engine hoje sobrescreve
 * bolha_mv/oclusao_mv com valores fixos ("MODO DE TESTE"), entao a
 * ausencia do ADC nao muda o comportamento observavel da bomba.
 */
#if DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, io_channels)
#define ARGUS_HAS_ADC 1
#else
#define ARGUS_HAS_ADC 0
#endif

#if ARGUS_HAS_ADC

static const struct adc_dt_spec sensor_1 = ADC_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, 0);
static const struct adc_dt_spec sensor_2 = ADC_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, 1);
static const struct adc_dt_spec sensor_3 = ADC_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, 2);

static const struct adc_dt_spec* adcs[] = {&sensor_1, &sensor_2, &sensor_3};

static int16_t buf;
static struct adc_sequence sequence = {
    .buffer = &buf,
    .buffer_size = sizeof(buf),
    .calibrate = false,
};

#define FILT_LEN 10
static int32_t filt_buffer[FILT_LEN] = {0};
static int filt_idx = 0;
static int32_t filt_accumulator = 0;

int adc_driver_init(void)
{
    for(int i = 0; i < 3; i++)
    {
        if(!adc_is_ready_dt(adcs[i]))
        {
            LOG_ERR("ADC device not ready");
            return -1;
        }
        adc_channel_setup_dt(adcs[i]);
    }
    return 0;
}

/* Função interna estática */
static int adc_driver_read_channel(uint8_t channel_idx, uint16_t* value)
{
    if(channel_idx >= 3)
        return -1;

    adc_sequence_init_dt(adcs[channel_idx], &sequence);
    int err = adc_read(adcs[channel_idx]->dev, &sequence);

    if(err < 0)
    {
        LOG_ERR("Read failed ch %d", channel_idx);
        return err;
    }

    if(buf < 0)
        buf = 0;
    *value = (uint16_t) buf;
    return 0;
}

/* Função da Thread */
void adc_thread_entry(void* p1, void* p2, void* p3)
{
    sensor_packet_t packet;
    int32_t raw_mv_buf[3];

    adc_driver_init();
    LOG_INF("ADC Driver Started");

    while(1)
    {
        for(int i = 0; i < 3; i++)
        {
            uint16_t raw;
            // [CORREÇÃO] Nome da função corrigido aqui:
            if(adc_driver_read_channel(i, &raw) == 0)
            {
                int32_t val_mv = (int32_t) raw;

                int err = adc_raw_to_millivolts_dt(adcs[i], &val_mv);
                if(err < 0)
                {
                    LOG_ERR("Conversão mV falhou canal %d", i);
                    val_mv = 0;
                }

                raw_mv_buf[i] = val_mv;
            }
        }

        packet.bolha_mv = raw_mv_buf[0];

        filt_accumulator -= filt_buffer[filt_idx];
        filt_buffer[filt_idx] = raw_mv_buf[1];
        filt_accumulator += raw_mv_buf[1];
        filt_idx = (filt_idx + 1) % FILT_LEN;
        packet.oclusao_mv = filt_accumulator / FILT_LEN;

        packet.volume_pot_mv = raw_mv_buf[2];
        packet.timestamp = k_uptime_get_32();

        k_msgq_put(&sensor_data_q, &packet, K_NO_WAIT);
        k_sleep(K_MSEC(10));
    }
}

K_THREAD_DEFINE(adc_tid, 1024, adc_thread_entry, NULL, NULL, NULL, 2, 0, 0);

#else /* !ARGUS_HAS_ADC ------------------------------------------------- */

int adc_driver_init(void)
{
    LOG_INF("ADC ausente neste target (sem io-channels) - stub ativo.");
    return 0;
}

void adc_thread_entry(void* p1, void* p2, void* p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
}

#endif /* ARGUS_HAS_ADC */
