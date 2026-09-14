#include "encoder.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(encoder_hal, LOG_LEVEL_INF);

/*
 * O encoder por quadratura e OPCIONAL. Se a board nao declara o alias
 * qdec0 no Device Tree (caso do perfil minimo da Nucleo), este arquivo
 * inteiro compila para stubs sem custo e o logic_engine simplesmente
 * enxerga delta = 0.
 */
#if DT_NODE_EXISTS(DT_ALIAS(qdec0))
#define ARGUS_HAS_QDEC 1
#else
#define ARGUS_HAS_QDEC 0
#endif

#if ARGUS_HAS_QDEC

/* Pega a referência do Device Tree (alias qdec0) */
static const struct device* const dev_qdec = DEVICE_DT_GET(DT_ALIAS(qdec0));

/* Variável estática para guardar a última posição lida (para cálculo de delta) */
static int32_t last_angle = 0;

int encoder_init(void)
{
    if(!device_is_ready(dev_qdec))
    {
        LOG_ERR("Hardware do Encoder (Timer 1) não está pronto!");
        return -ENODEV;
    }

    /* Faz uma leitura inicial para 'calibrar' o last_angle */
    struct sensor_value val;
    if(sensor_sample_fetch(dev_qdec) == 0)
    {
        sensor_channel_get(dev_qdec, SENSOR_CHAN_ROTATION, &val);
        last_angle = val.val1;
    }

    LOG_INF("Encoder inicializado com sucesso.");
    return 0;
}

int32_t encoder_get_angle(void)
{
    struct sensor_value val;
    int rc;

    /* 1. Fetch: Congela o valor do hardware */
    rc = sensor_sample_fetch(dev_qdec);
    if(rc != 0)
    {
        LOG_ERR("Erro fetch encoder: %d", rc);
        return -1;
    }

    /* 2. Get: Lê o valor processado (Graus) */
    rc = sensor_channel_get(dev_qdec, SENSOR_CHAN_ROTATION, &val);
    if(rc != 0)
    {
        LOG_ERR("Erro get encoder: %d", rc);
        return -1;
    }

    /* O driver STM32 retorna graus na parte inteira (val1) */
    return val.val1;
}

int32_t encoder_get_delta(void)
{
    int32_t current_angle = encoder_get_angle();

    /* Se houve erro na leitura, retorne 0 para não afetar o volume */
    if(current_angle < 0)
        return 0;

    int32_t delta = current_angle - last_angle;

    /* --- Lógica de Correção de Volta Completa (Wrap-around) --- */
    /* Se pulou de 350 para 10 (avançou), o delta matemático é -340.
       Mas o delta real é +20. */
    if(delta < -180)
    {
        delta += 360;
    }
    /* Se pulou de 10 para 350 (recuou), o delta matemático é +340.
       Mas o delta real é -20. */
    else if(delta > 180)
    {
        delta -= 360;
    }

    /* Atualiza a última posição para a próxima chamada */
    last_angle = current_angle;

    return delta;
}

#else /* !ARGUS_HAS_QDEC ------------------------------------------------ */

int encoder_init(void)
{
    LOG_INF("Encoder ausente neste target (sem alias qdec0) - stub ativo.");
    return 0;
}

int32_t encoder_get_angle(void)
{
    return 0;
}

int32_t encoder_get_delta(void)
{
    return 0;
}

#endif /* ARGUS_HAS_QDEC */
