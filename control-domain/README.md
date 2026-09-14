# Argus — control domain (Zephyr RTOS)

## Targets suportados

O projeto compila para duas placas. O overlay de Device Tree e o
fragmento de Kconfig sao escolhidos pelo nome da board, em `boards/`.

| Target | Perfil | Perifericos |
|---|---|---|
| `blackpill_f411ce` | Completo (hardware original) | SPI+DMA, PWM, encoder, ADC, botoes, OTA/MCUboot |
| `nucleo_u575zi_q`  | Minimo (comunicacao) | SPI escravo, DATA READY, PWM |

```bash
west build -b blackpill_f411ce .      # perfil completo
west build -b nucleo_u575zi_q  .      # perfil minimo
```

O artefato sai como `argus_<board>_v<versao>.bin` na raiz.

### Perifericos opcionais

Encoder, ADC e **motor** nao precisam de flag de compilacao: os drivers
detectam a ausencia dos nos no Device Tree e compilam para stubs.

| Driver | Alias / no exigido | Sem ele |
|---|---|---|
| `src/encoder.c`     | `qdec0`                          | `encoder_get_delta()` devolve 0 |
| `src/adc_driver.c`  | `zephyr,user` com `io-channels`  | nenhum pacote de sensor publicado |
| `src/motor_driver.c`| `motor-pwm` + `motor-dir` + `motor-en` | comandos de motor viram no-op |

Para um build so de comunicacao na Nucleo, apague o bloco do motor do
overlay (esta delimitado por comentarios) junto com os tres aliases
`motor-*`. Nada mais precisa mudar.

### Pinos do perfil minimo (Nucleo-U575ZI-Q)

    SPI1 escravo   NSS PA4 / SCK PA5 / MISO PA6 / MOSI PA7   (AF5)
    DATA READY     PA2  -> GPIO 5 da Raspberry Pi (entrada)
    MOTOR PWM      PA0  (TIM2_CH1)
    MOTOR DIR/EN   PB13 / PB14

### Sobre o DATA READY em hardware

PA2 e o pino SPI1_RDY do U5. O IP de SPI da geracao nova sabe dirigir
esse sinal por conta propria (`SPI_CFG2.RDIOM`/`RDIOP`), sem software no
meio. Hoje **nao** usamos isso: PA2 e um GPIO comum, dirigido em
`hub.c`, igual a Blackpill.

Vale registrar por que isso interessa. O laco atual e:

```c
gpio_pin_set_dt(&ready_pin, 1);
int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
gpio_pin_set_dt(&ready_pin, 0);
```

O pino sobe **antes** de o periferico estar armado. Existe uma janela em
que o gateway ve READY=1 e o escravo ainda nao esta pronto -- e e
exatamente por isso que o lado da Raspberry tem um
`sleep_for(100us)` comentado como "Delay de Estabilizacao DMA
(Critico)" em `stm32_bridge.cpp`. O delay compensa essa corrida.

Com RDY em hardware a corrida deixa de existir: o proprio periferico
levanta e baixa o sinal nos instantes certos.

Nao esta implementado porque o driver `spi_stm32` do Zephyr nao expoe
`RDIOM`/`RDIOP` em devicetree nem em Kconfig, e reprograma `CFG2` no
caminho de transceive -- escrever o registrador na inicializacao seria
sobrescrito. Habilitar isso exige mexer no driver, o que nao cabe no
objetivo atual de subir a comunicacao.

Do lado da Raspberry nada muda: ela le um nivel no GPIO 5 e nao sabe se
quem o dirige e software ou hardware. A troca e transparente quando for
feita.

### UART de debug

Console e logs saem pela VCP do ST-Link. Sem fiacao extra e sem gastar
pino de header:

```bash
picocom -b 115200 /dev/ttyACM0
```

O perfil da Nucleo liga timestamp no log (para casar com as capturas do
analisador logico), nome de thread e stack trace em falha de kernel. O
log fica em modo **deferido** de proposito: em modo imediato a UART
injetaria latencia dentro do laco do SPI e contaminaria qualquer medida
de tempo.

Para subir a verbosidade no bring-up, ha uma linha
`CONFIG_LOG_DEFAULT_LEVEL=4` comentada em `boards/nucleo_u575zi_q.conf`,
e um bloco de shell interativo logo abaixo. Se preferir o log num
adaptador USB-serial separado, ha um bloco de console alternativo
comentado no fim do overlay.
