#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

/**
 * @brief Inicializa o driver do encoder KY-040
 * @return 0 em caso de sucesso, código de erro negativo caso contrário
 */
int encoder_init(void);

/**
 * @brief Lê o ângulo absoluto atual (0 a 360 graus)
 * Útil para saber a posição exata em uma volta.
 */
int32_t encoder_get_angle(void);

/**
 * @brief Calcula o deslocamento (delta) desde a última leitura.
 * * Esta função é CRUCIAL para a bomba. Ela lida com o "wrap-around"
 * (passagem de 359 para 0 graus).
 * * @return Variação em graus (ex: +10, -5).
 */
int32_t encoder_get_delta(void);

#endif /* ENCODER_H */
