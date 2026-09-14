#ifndef MARKERS_H
#define MARKERS_H

/* =========================================================================
 * Marcadores de hardware para o analisador logico
 * =========================================================================
 * Dois pinos que o firmware sobe e desce em pontos conhecidos, para que o
 * caminho quadro -> atuacao seja medido POR FORA, sem depender da
 * instrumentacao de software do proprio firmware -- que ja se mostrou capaz
 * de produzir numeros plausiveis e errados (ver REPORT.md, Parte IV).
 *
 *   Marcador A (PA8, Zio D33, CN10-31): sobe quando um quadro e decodificado
 *       com sucesso; desce quando o hub rearma o SPI para o proximo quadro.
 *   Marcador B (PE0, Zio D34, CN10-33): fica alto enquanto
 *       update_motor_hardware() executa -- o trecho que de fato mexe no motor.
 *
 * A medida de interesse e a SUBIDA DE A ate a SUBIDA DE B. Como o protocolo
 * e sincrono (o gateway so envia o proximo quadro depois de receber a
 * resposta), existe um unico A antes de cada B, e o pareamento e direto.
 *
 * O que cada borda NAO inclui: a subida de A ocorre depois de o driver SPI
 * ter entregue o quadro, entao o tempo de barramento fica de fora. Isso e
 * proposital: o barramento ja e visivel nos proprios canais SPI do
 * analisador.
 *
 * Placas cujo Device Tree nao define os aliases la-marker-a / la-marker-b
 * (a Blackpill, por exemplo) compilam sem nenhum codigo adicional.
 * ========================================================================= */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#if DT_NODE_EXISTS(DT_ALIAS(la_marker_a)) && DT_NODE_EXISTS(DT_ALIAS(la_marker_b))

#define ARGUS_MARKERS_PRESENT 1

static inline void argus_markers_init(void)
{
    static const struct gpio_dt_spec a = GPIO_DT_SPEC_GET(DT_ALIAS(la_marker_a), gpios);
    static const struct gpio_dt_spec b = GPIO_DT_SPEC_GET(DT_ALIAS(la_marker_b), gpios);

    if(device_is_ready(a.port))
        gpio_pin_configure_dt(&a, GPIO_OUTPUT_INACTIVE);
    if(device_is_ready(b.port))
        gpio_pin_configure_dt(&b, GPIO_OUTPUT_INACTIVE);
}

static inline void argus_marker_a_set(int value)
{
    static const struct gpio_dt_spec a = GPIO_DT_SPEC_GET(DT_ALIAS(la_marker_a), gpios);
    gpio_pin_set_dt(&a, value);
}

static inline void argus_marker_b_set(int value)
{
    static const struct gpio_dt_spec b = GPIO_DT_SPEC_GET(DT_ALIAS(la_marker_b), gpios);
    gpio_pin_set_dt(&b, value);
}

#else

#define ARGUS_MARKERS_PRESENT 0

static inline void argus_markers_init(void) {}
static inline void argus_marker_a_set(int value) { (void) value; }
static inline void argus_marker_b_set(int value) { (void) value; }

#endif

#endif /* MARKERS_H */
