# Bancada 02 — distribuições de timing · campanha de 25/10/2026

Item 2 do IEEE: *"Report quantitative timing distributions of the critical
control loop under nominal and stressed conditions."*

## Montagem

- MCU: Nucleo-U575ZI-Q, firmware **v1.1.23** (commit 5dd34d1), 160 MHz.
- Gateway: Raspberry Pi 3B+ (192.168.1.9), imagem Yocto, SPI a 500 kHz.
- Analisador: Saleae Logic (fx2lafw, clone), 8 canais, 4 MHz.
- Mapa de canais (25/10): D0 NRST, D1 NSS, D2 READY, D3 marcador C,
  **D4 STEP, D5 marcador A, D6 marcador B**.
- Sem atuador físico: só sinal.

## Condições

`baseline` (nominal) e quatro estressadas. A carga é a mesma do
`run_campaign.sh`, com dois ajustes obrigatórios nesta montagem:

- **`stress-ng --temp-path /data/stress-tmp`**: o `/` da imagem é somente
  leitura; sem um temp gravável o `stress-ng` aborta e o cenário roda SEM
  carga. (Na primeira tentativa isso passou despercebido e cpu/io/combined
  saíram iguais ao baseline — rodada descartada.)
- **Rede dirigida pelo host**: há um firewall (`ufw`) ativo no host que
  bloqueia conexões vindas do gateway, então o `iperf3 -c` do gateway para o
  host nunca conecta. Invertido: o gateway roda `iperf3 -s` e o host empurra
  (`iperf3 -c 192.168.1.9`), ~42 Mbit/s sobre WiFi. Satura o enlace do
  gateway sem mexer no firewall.

Carga confirmada por `prova_carga`: cpu/io/combined a 100 % de CPU; net com
load average 5,4 (softirq de RX). 0 capturas truncadas.

## Resultado

Os três sinais que são a saída do laço crítico **não mudam** com a carga:

| Grandeza | nominal | combined | muda? |
|---|---|---|---|
| G1 período do laço | 100,03–100,30 ms, 0 deadline miss | idem | não |
| G3 período do STEP | p50 1180,00 µs, máx 1180,25 | idêntico (KS p=0,46) | não |
| G4 validação (marcador A) | p50 23,0 µs | p50 23,0 µs | não |

**G2 (quadro → atuação) muda de forma, mas não piora.** A mediana cai sob
carga (nominal 123 µs → net 116 → io/combined 110 µs) e a massa se concentra
no piso de ~110 µs (84,7 % sob combined, contra 23 % no nominal). **O p99 e o
máximo não crescem** (~136 µs em todas). Mecanismo provável: sob carga o fluxo
de comandos SPI fica mais lento, então a thread de controle quase sempre já
está bloqueada no `k_poll` quando a atuação chega, e acorda com latência
mínima. Reforça a contenção, não a enfraquece. **Falta confirmar no nível da
forma de onda antes de ir ao artigo.**

### G1 como distribuição (métricas do MCU em janelas)

`baseline` (n=1468) e `cpu` (n=1244): p50 = 100 098 µs nas duas, máx 100 300 /
100 299 µs, 0 deadline miss. Distribuições indistinguíveis (a diferença de
mediana é 0,00 µs; o KS dá p pequeno só pelo n grande — ver aviso em
`distribuicoes.py`). `io`/`net`/`combined` por janelas falharam sob carga
pesada (as leituras curtas de métrica voltam vazias); para essas condições o
G1 autoritativo é o mín./máx./deadline da rodada A+B, em `gateway/timing.csv`.

## Arquivos

| Caminho | Conteúdo |
|---|---|
| `capturas/` | 50 `.sr`: `<cond>_status_r{1,2}` (G3,G4) e `<cond>_mix_r{1..8}` (G2) |
| `csv/` | um valor por evento, por captura (G2/G3/G4) |
| `distribuicoes/tabela.md`, `ecdf_G*.png` | tabela de percentis + KS, ECDFs |
| `G1_distribuicao_mcu/` | G1 por amostra, baseline e cpu |
| `janelas_mcu/` | janelas cruas de métrica do MCU (fonte do G1) |
| `gateway/timing.csv` | registros do gateway: carga, G1 mín/máx/deadline por condição |
| `marcador_c.patch`, `.overlay` | instrumentação do Bloco C (não executado) |

## Não executado

- **Bloco C** (marcador C para G1 por borda de hardware): exige gravar o
  firmware de bancada. O firmware `marcadorC_v77.signed.bin` já está compilado
  e em `/data/ota` na RPi, mas o flash não foi feito nesta campanha.
