#ifndef SPI_CONFIG_HPP
#define SPI_CONFIG_HPP
// ===========================================================================
// spi_config.hpp -- UNICA fonte da velocidade do barramento gateway <-> MCU
// ===========================================================================
// Antes a velocidade estava escrita em cinco lugares: 500 kHz no servico,
// no argus-bench e no argus-attack, 100 kHz no stm32-updater, e o texto
// "500000" fixo no run_campaign.sh. Mudar um e esquecer outro faria o bench
// medir um barramento diferente do que o produto usa -- e o environment.txt
// continuaria registrando 500 kHz, sem ninguem perceber.
//
// A 500 kHz, as duas transferencias de 64 bytes de cada transacao custam
// 2,05 ms: ~72% da mediana medida (2,83 ms, campanha C5). A latencia de
// transacao e portanto um parametro de configuracao, nao uma propriedade
// da arquitetura.
#include <cstdint>
#include <cstdlib>

namespace argus_spi
{
// Servico (infusion-pump), argus-bench e argus-attack. Todos os numeros da
// campanha C5 foram medidos neste valor.
constexpr uint32_t SPEED_HZ = 500000;

// stm32-updater (OTA do MCU). Mais lento de proposito: e o valor com que o
// OTA via MCUboot foi validado em bancada. Nao igualar a SPEED_HZ sem
// revalidar a atualizacao.
constexpr uint32_t OTA_SPEED_HZ = 100000;

// Faixa aceita por --spi-hz. O teto nao vem do silicio -- o device tree da
// Pi permite 125 MHz no spidev -- e sim dos jumpers da bancada, que acima
// de ~10 MHz sofrem com reflexao e diafonia. Subir o teto so com fiacao
// adequada.
constexpr uint32_t SPEED_MIN_HZ = 50000;
constexpr uint32_t SPEED_MAX_HZ = 10000000;

// Converte e valida um --spi-hz vindo da linha de comando.
inline bool parse_speed(const char* s, uint32_t* out)
{
    char* end = nullptr;
    const unsigned long v = std::strtoul(s, &end, 10);
    if(end == s || *end != '\0' || v < SPEED_MIN_HZ || v > SPEED_MAX_HZ)
        return false;
    *out = (uint32_t) v;
    return true;
}
}

#endif
