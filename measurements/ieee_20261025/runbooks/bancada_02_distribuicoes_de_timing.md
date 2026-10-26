# Roteiro de bancada 02 — Distribuições de timing do laço de controle crítico

> *Item 2 do IEEE: "Report quantitative timing distributions of the critical
> control loop under nominal and stressed conditions."*

**Bancada:** a mesma da bancada 01 — Nucleo-U575ZI-Q (domínio crítico, Zephyr,
firmware v1.1.23) + Raspberry Pi 3B+ (gateway, Yocto) + analisador lógico
fx2lafw.
**Objetivo:** para cada grandeza de timing do laço crítico, gravar **um valor
por evento**, em condição nominal e sob cada estresse, e tirar daí histograma,
ECDF, percentis e um teste de igualdade de distribuições. O resumo de cinco
números do artigo passa a ser derivado desses dados brutos.
**Tempo estimado:** ≈ 3 h de bancada, mais 30 min de consolidação.

| | |
|---|---|
| Raspberry Pi 3B+ | `root@192.168.1.9` |
| Host (este PC) | `192.168.1.17` |
| Diretório de trabalho | `~/argus-bancada-timing` |
| Scripts | `plano_ieee/scripts/sr_timing.py`, `plano_ieee/scripts/distribuicoes.py` |
| Patch do marcador C | `plano_ieee/patches/marcador_c.patch` + `marcador_c.overlay` |

---

## 0. Leia antes de ligar nada

### 0.1 O que o artigo tem hoje e o que falta

O artigo traz resumos: `tab:critical_timing` (quadro→atuação, n = 668–677 por
condição, mediana/p95/p99/máx) e `tab:latency` (latência de transação). O
revisor pede **distribuições**. Dois cenários com a mesma mediana e o mesmo
máximo podem ter formas muito diferentes; só os dados por evento mostram isso.

> **O analisador lógico é o juiz** (regra da bancada 01). As métricas do MCU
> (`GET_METRICS`) são agregados (mín., máx., contagem, deadline misses), e a
> leitura zera os contadores. Servem para corroborar, não para montar
> distribuição.

### 0.2 Como o laço crítico funciona (e por que isso muda a medição)

O laço do `logic_engine` (`logic_engine.c`) é **orientado a eventos**:

```c
k_poll(events, 2, K_MSEC(100));   // fila de comandos OU fila de sensores
hub_metrics_loop_tick(poll_wait_us, poll_rc == -EAGAIN);
```

- Ele acorda quando chega um comando **ou** quando passam 100 ms sem nada.
  Período nominal `LOOP_PERIOD_US = 100000`, deadline `LOOP_DEADLINE_US =
  110000` (`hub.h:112-113`).
- A métrica do MCU só conta as voltas que terminam **por timeout**
  (`hub.c:155`). Uma volta acordada por comando mede o gateway, não o RTOS.
- Na Nucleo **não há ADC** (sem `io-channels` no overlay), então a fila de
  sensores fica vazia e não acorda o laço.
- Os comandos `GET_STATUS` são respondidos direto no `hub.c` e **não** passam
  pela fila. Só SET_CONFIG, RUN e PAUSE acordam o laço.

**Consequência:** com o tráfego `mix` da campanha publicada (3 de cada 8
quadros entram na fila, um a cada ~2 ms), o laço quase nunca expira. É por
isso que os `*_mcu.csv` de `resultados/bench-p3v2/` têm só **2 a 13 períodos
medidos** por execução. Para medir o período do laço, o tráfego precisa ser
**`--cmd status`**: o barramento SPI fica ocupado (o MCU atende interrupções e
responde quadros), mas o laço só acorda por timeout.

### 0.3 As quatro grandezas

| # | Grandeza | Definição | Tráfego | Instrumento | Amostragem | Meta de n por condição |
|---|---|---|---|---|---|---|
| **G1** | Período do laço | intervalo entre voltas por timeout | `status` | marcador C (D3), Bloco C; métrica do MCU como corroboração | 250 kHz (4 µs), 120 s | ≥ 1.000 períodos |
| **G2** | Quadro → atuação | subida de A → subida de B | `mix` | D5 (A), D6 (B) | 4 MHz (0,25 µs), 15 s | ≥ 5.000 (8 capturas) |
| **G3** | Período do STEP | subidas consecutivas do pulso do motor | `status` | D4 | 4 MHz, 15 s | ≥ 25.000 (2 capturas) |
| **G4** | Validação do quadro | largura do marcador A (quadro decodificado → SPI rearmado com a resposta pronta; os 23,3 µs do artigo) | `status` | D5 | 4 MHz, 15 s | ≥ 5.000 (2 capturas) |

**G1 é a lacuna principal**: hoje não existe distribuição nenhuma dele.
G2–G4 já têm instrumento; faltam densidade e repetições.

> **G4 precisa de tráfego `status`.** Em `mix`, a largura de A tem três modos
> (status ≈ 24 µs, RUN/PAUSE ≈ 87 µs, SET_CONFIG ≈ 150–250 µs). Sem decodificar
> o MOSI não há como separá-los, então G4 sai das capturas `status`. O script
> avisa se a captura estiver misturada.

### 0.4 Condições

Mesmos cenários da campanha publicada (`run_campaign.sh`), para que as
distribuições sejam comparáveis às tabelas do artigo:

| Condição | Carga no gateway | Comando |
|---|---|---|
| `ocioso` (só Bloco C) | nenhuma, **sem tráfego SPI** | — |
| **`baseline`** (nominal) | nenhuma | — |
| `cpu` | 4 núcleos | `stress-ng --cpu $(nproc)` |
| `io` | disco e memória | `stress-ng --io 4 --vm 2 --vm-bytes 64M` |
| `net` | rede | `iperf3 -c 192.168.1.17 -b 100M` |
| `combined` | as três juntas | **o cenário do artigo** |

- O `crcfault` fica de fora: ele perturba o enlace, não o laço (é da bancada
  01).
- **`iperf3` limitado a 100 Mbit/s**, como nas capturas publicadas. Sem
  limite, o host gasta tempo tratando o tráfego, atrasa o USB e o fx2lafw
  entrega menos amostras do que o pedido ("Device only sent N samples"). As
  capturas truncadas da primeira rodada foram todas com rede sem limite.
- **Prova de carga:** cada condição registra ocupação de CPU e load average do
  gateway, medidos durante a carga. O `run_campaign.sh` sobe os geradores sem
  conferir se eles existem; aqui, se faltar `stress-ng`, a função falha alto.
  (A imagem do repositório lista `stress`, não `stress-ng`; confira na Etapa 2.)

### 0.5 Prévia com os dados que já existem

Os scripts foram validados contra as capturas de setembro (fiação antiga:
STEP = D5, A = D6, B = D7). Eles reproduzem a `tab:critical_timing` publicada
exatamente, e já dão uma primeira distribuição:

| Grandeza | Condição | n | p50 | p95 | p99 | máx | KS vs baseline |
|---|---|---|---|---|---|---|---|
| G2 quadro→atuação (µs) | baseline | 677 | 123,75 | 131,25 | 136,25 | 136,50 | — |
| | cpu+io+vm | 676 | 121,75 | 131,06 | 136,25 | 136,50 | D = 0,051, p = 0,31 |
| | combined | 668 | 123,25 | 130,66 | 136,08 | 136,50 | D = 0,048, p = 0,41 |
| G3 período STEP (µs) | baseline | 12.656 | 1.180,00 | 1.180,00 | 1.180,00 | 1.180,25 | — |
| | combined | 12.711 | 1.180,00 | 1.180,00 | 1.180,00 | 1.180,25 | D = 0,001, p = 1 |
| G4 validação (µs) | baseline | 2.803 | 23,00 | 23,50 | 24,00 | 24,25 | — |
| | combined | 2.799 | 23,00 | 23,25 | 24,00 | 24,25 | D = 0,009, p = 1 |

Para regenerar: `capturas/atuacao_limpa2.sr`, `teste_sem_rede.sr`,
`atuacao_estresse3.sr` (G2, `--a D6 --b D7`); `pulso_limpo2.sr`,
`pulso_estresse2.sr` (G3 `--canal D5`, G4 `largura --canal D6`).

Isso já sustenta a tese, mas com uma captura por condição, sem `cpu`/`io`/`net`
separados e sem G1. A bancada 02 completa o quadro.

### 0.6 Só sinal

A bancada observa sinais: o STEP é medido no pino do MCU (848 Hz para
600 mL/h), sem atuador ligado. O Bloco C grava um firmware de bancada; a
Etapa 8 restaura o legítimo.

---

## 1. Ligações

Iguais à bancada 01 — **não refaça a fiação se ela ainda estiver montada.** O
fio de reset (GPIO 4 → NRST) fica desligado.

| Canal | Sinal | Pino | Uso |
|---|---|---|---|
| D0 | NRST | CN8-5 | reinícios do MCU |
| D1 | NSS | PA4 / CN7-9 | transações SPI |
| D2 | DATA READY | PB3 / CN7-15 | atividade do barramento |
| D3 | **Marcador C** | **PD15 (Zio D9, CN7-18)** | G1 (só Bloco C) |
| D4 | STEP | PA0 / CN10-29 | G3 |
| D5 | Marcador A | PA8 / CN10-31 | G2, G4 |
| D6 | Marcador B | PE0 / CN10-33 | G2 |

PD15 está livre neste projeto: não é LED, botão, I2C nem o SPI1 do overlay
(PA4–PA7). D7 não é usado. As pontas D1 e D2 dividem o pino com os fios
da RPi: o fio da RPi tem de continuar no lugar.

---

## 2. Preparação · 30 min

### Etapa 1 — Testes e analisador

```bash
cd ~/Documents/Argus_Artigo/Argus_MCPS_Blackpill_STM32F411_ZephyrRTOS
export ZEPHYR_BASE=~/zephyrproject/zephyr && source ~/zephyrproject/.venv/bin/activate
python3 -m pytest test/bancada01_estatico -q      # STEP(600)=848 Hz, teto 1.697 Hz
sigrok-cli --scan                                  # tem de listar fx2lafw
```

### Etapa 2 — Biblioteca da bancada 02 na RPi

Uma biblioteca própria, para não misturar com o `ameacas.csv` da bancada 01.
Abra `ssh root@192.168.1.9` e cole **uma vez**:

```sh
systemctl stop infusion-pump          # disputa o /dev/spidev0.1

DATA=$(date +%Y%m%d)
cat > /data/timing_lib.sh <<EOF_LIB
export OUT=/data/bench-timing-$DATA
EOF_LIB
cat >> /data/timing_lib.sh <<'EOF_LIB'
mkdir -p "$OUT" /data/stress-tmp
export PATH=/data/bancada01/bin:$PATH      # ferramentas do fonte atual
# O / da imagem e somente leitura: sem --temp-path gravavel o stress-ng
# aborta na hora ("temp-path '.' must be readable and writeable") e o
# cenario roda SEM carga. /data fica no cartao SD: o io e disco de verdade.
IPERF_HOST=192.168.1.17
IPERF_BW=100M

# Registro unico: grandeza,condicao,rep,metrica,valor
reg() { echo "$1,$2,$3,$4,$5" >> "$OUT/timing.csv"; }

# BusyBox nao tem pkill confiavel: mata pelo PID
matar() { for p in $(ps | grep -E "$1" | awk '{print $1}'); do kill -9 $p 2>/dev/null; done; }

pausar()         { argus-bench --run-rate 1 -n 1 --cmd status -o /dev/null >/dev/null 2>&1; }
zerar_metricas() { argus-bench -n 1 --cmd status -o /dev/null --metrics /dev/null >/dev/null 2>&1; }

# Carga no gateway, igual ao start_load do run_campaign.sh
carga_on() {
    case "$1" in
        ocioso|baseline) ;;
        cpu|io|combined)
            command -v stress-ng >/dev/null || { echo "ERRO: stress-ng ausente"; return 1; } ;;
    esac
    case "$1" in
        cpu)      nohup stress-ng --temp-path /data/stress-tmp --cpu "$(nproc)" --timeout 0 >/dev/null 2>&1 & ;;
        io)       nohup stress-ng --temp-path /data/stress-tmp --io 4 --vm 2 --vm-bytes 64M --timeout 0 >/dev/null 2>&1 & ;;
        net)      nohup iperf3 -s >/tmp/iperf_srv.log 2>&1 & ;;    # o HOST empurra; ver nota
        combined) nohup stress-ng --temp-path /data/stress-tmp --cpu "$(nproc)" --io 4 --vm 2 --vm-bytes 64M --timeout 0 >/dev/null 2>&1 &
                  nohup iperf3 -s >/tmp/iperf_srv.log 2>&1 & ;;
    esac
    return 0
}
carga_off() { matar '[s]tress-ng|[i]perf3'; }

# Ocupacao de CPU em 5 s, load average e geradores vivos -> timing.csv
prova_carga() {
    c=$1
    set -- $(head -n 1 /proc/stat); t1=$(($2+$3+$4+$5+$6+$7+$8)); i1=$5
    sleep 5
    set -- $(head -n 1 /proc/stat); t2=$(($2+$3+$4+$5+$6+$7+$8)); i2=$5
    occ=$((100 - 100*(i2-i1)/(t2-t1)))
    la=$(cut -d' ' -f1 /proc/loadavg)
    ger=$(ps | grep -cE '[s]tress-ng|[i]perf3 -c')
    reg carga "$c" 1 cpu_ocupada_pct "$occ"
    reg carga "$c" 1 loadavg_1min "$la"
    reg carga "$c" 1 geradores_vivos "$ger"
    echo "$c: CPU ${occ}%  load ${la}  geradores ${ger}"
}

# Bomba infundindo + trafego continuo; morto com -9 para nao pausar
trafego_on() {
    nohup argus-bench --run-rate 600 --cmd "$1" -n 10000000 --interval-us 2000 \
          -o /dev/null > "$OUT/trafego_$2_$1.log" 2>&1 &
    sleep 4
    grep -q "infusao continua" "$OUT/trafego_$2_$1.log" \
        && echo "trafego $1 no ar, STEP a 600 mL/h" \
        || { echo "ERRO: infusao nao iniciou"; return 1; }
}
trafego_off() { matar '[a]rgus-bench'; sleep 1; pausar; }

# Metricas do MCU de uma corrida G1 -> timing.csv
reg_g1() {
    for m in loop_count loop_period_min_us loop_period_max_us deadline_misses frames_seen; do
        reg G1mcu "$1" 1 "$m" "$(grep ",$m," "$OUT/G1_$1_mcu.csv" | cut -d, -f3)"
    done
    grep -E "loop_|deadline" "$OUT/G1_$1_mcu.csv"
}

# Bomba infundindo sem trafego: o bench e morto com -9 e nao pausa
infundir() {
    argus-bench --run-rate "${1:-600}" -n 1000000 --interval-us 2000 \
                --cmd status -o /dev/null >/dev/null 2>&1 &
    sleep 2; kill -9 $! 2>/dev/null; sleep 0.5
}

# Modo sem analisador: le GET_METRICS em janelas curtas por <seg> segundos.
#   janelas laco <condicao> [seg]     -> periodo do laco (G1)
#   janelas comando <condicao> [seg]  -> comando->atuacao, por ciclo mix (G5)
# A leitura zera os contadores; com janelas < 200 ms cada uma tem no maximo
# 2 voltas do laco, e min/max devolvem cada volta exatamente.
janelas() {
    modo=$1; cond=$2; dur=${3:-180}
    case $modo in
        laco)    args="-n 20 --cmd status --interval-us 0" ;;
        comando) args="-n 8 --cmd mix --interval-us 2000" ;;
        *) echo "uso: janelas laco|comando <condicao> [seg]"; return 2 ;;
    esac
    rt=""; command -v chrt >/dev/null 2>&1 && rt="chrt -f 50"
    arq="$OUT/janelas_${modo}_${cond}.csv"; tmp=/tmp/janela.csv
    echo "t_s,frames_seen,crc_errors,driver_errors,cmd_count,cmd_lat_max_us,cmd_lat_avg_us,loop_count,loop_period_min_us,loop_period_max_us,deadline_misses" > "$arq"
    zerar_metricas
    fim=$(( $(cut -d. -f1 /proc/uptime) + dur )); perdidas=0
    while [ "$(cut -d. -f1 /proc/uptime)" -lt "$fim" ]; do
        rm -f $tmp
        $rt argus-bench $args --warmup 10 --label "$cond" -o /dev/null --metrics $tmp >/dev/null 2>&1
        [ -s $tmp ] || { perdidas=$((perdidas+1)); continue; }
        l=$(cut -d' ' -f1 /proc/uptime)
        for m in frames_seen crc_errors driver_errors cmd_count cmd_lat_max_us cmd_lat_avg_us \
                 loop_count loop_period_min_us loop_period_max_us deadline_misses; do
            l="$l,$(grep ",$m," $tmp | cut -d, -f3)"
        done
        echo "$l" >> "$arq"
    done
    echo "$arq: $(($(wc -l < "$arq") - 1)) janelas, $perdidas leituras perdidas"
    reg janelas_$modo "$cond" 1 leituras_perdidas "$perdidas"
}
EOF_LIB

. /data/timing_lib.sh
echo "grandeza,condicao,rep,metrica,valor" > "$OUT/timing.csv"
{
  echo "data=$(date '+%Y-%m-%dT%H:%M:%S%z')"
  echo "gateway_modelo=$(tr -d '\0' < /proc/device-tree/model)  cpus=$(nproc)"
  echo "mcu_placa=nucleo_u575zi_q mcu_clock_hz=160000000"
  echo "mcu_firmware=v1.1.23 commit 5dd34d1 (bancada 01)"
  echo "ferramentas=$(md5sum /data/bancada01/bin/* | tr '\n' ' ')"
  echo "stress-ng=$(command -v stress-ng) iperf3=$(command -v iperf3) iperf_bw=$IPERF_BW"
  echo "kernel=$(uname -a)"
  argus-bench --show-config
} > "$OUT/environment.txt"
cat "$OUT/environment.txt"
exit
```

Confira na saída a linha `stress-ng=`. **Se vier vazia, pare**: os cenários
`cpu`, `io` e `combined` não teriam carga.

### Etapa 3 — Atalhos no host (terminal H1)

Em todo terminal novo:

```bash
export PI=root@192.168.1.9
export B2=~/argus-bancada-timing
export SCR=~/Documents/Argus_Artigo/plano_ieee/scripts
mkdir -p $B2/la $B2/csv
pi()  { ssh -o ConnectTimeout=8 "$PI" ". /data/timing_lib.sh; $*"; }
# Captura com os 8 canais (como as capturas publicadas): cap <nome> <taxa> <amostras>
cap() { sigrok-cli -d fx2lafw --config samplerate="$2" --samples "$3" -o "$B2/la/$1.sr"; }
```

Servidor de rede no **terminal H2**, durante toda a bancada:

```bash
iperf3 -s
```

### Etapa 4 — Erro de fundo do barramento

```bash
pi 'zerar_metricas; argus-bench -n 5000 --cmd mix --label fundo -o $OUT/fundo.csv --metrics $OUT/fundo_mcu.csv'
pi 'grep -E "crc_errors|driver_errors" $OUT/fundo_mcu.csv'
```

`crc_errors` tem de ser 0. Erros de driver isolados são a corrida conhecida do
DATA READY (bancada 01, seção 3.5); com `crc_errors > 0`, refaça a fiação.

---

## 2b. Modo sem analisador · ≈ 50 min

Sem o analisador, não há relógio externo. Mesmo assim, o período do laço
(G1) sai **uma amostra por volta**, com o firmware legítimo, pelas métricas do
próprio MCU:

- `GET_METRICS` é respondido no `hub.c` e não acorda o laço;
- a leitura zera os contadores (`hub.c:340`), então cada leitura cobre só o
  intervalo desde a anterior;
- lendo em janelas menores que 200 ms, cada janela tem 0, 1 ou 2 voltas, e o
  mín./máx. da janela são exatamente essas voltas. Janelas com 3 ou mais
  perdem as voltas do meio; o conversor conta quantas.

Cada janela também manda 20 quadros `status` sem intervalo, então o MCU está
atendendo o barramento enquanto o laço é medido. O leitor roda com `chrt -f 50`
(se existir) para que o estresse do gateway não alongue as janelas.

**Limites, para o texto do artigo:** o período é medido pelo relógio do próprio
MCU (o contador de ciclos de 160 MHz), não por um instrumento independente; e é
o tempo que o `k_poll` ficou bloqueado, que é praticamente toda a volta quando
não há comandos. G2–G4 continuam vindo das capturas de setembro (seção 0.5),
que são as únicas com relógio externo.

Pré-requisito: Etapas 2 a 4 (sem a parte do `sigrok-cli`). Para cada condição,
no H1:

```bash
sem_analisador() {
  local S=$1
  echo "===== $S ====="
  pi "carga_on $S" || return 1
  [ "$S" != baseline ] && sleep 5
  pi "prova_carga $S"
  pi "infundir 600; janelas laco $S 180"      # 3 min -> ~1.800 voltas
  pi "janelas comando $S 60; pausar"           # 1 min -> ~250 ciclos mix
  pi "carga_off"
}
for S in baseline cpu io net combined; do sem_analisador $S; done
```

No fim, traga e converta:

```bash
mkdir -p $B2/mcu && scp "$PI:/data/bench-timing-*/janelas_*.csv" "$PI:/data/bench-timing-*/timing.csv" $B2/mcu/
for f in $B2/mcu/janelas_*.csv; do python3 -I $SCR/janelas_para_amostras.py $f --out $B2/csv_mcu; done
python3 -I $SCR/distribuicoes.py $B2/csv_mcu --out $B2/distribuicoes_mcu
```

O que conferir na saída do conversor:

- "voltas" perto do esperado (180 s ÷ 100 ms ≈ 1.800);
- "perdidas" abaixo de ~5 %. Se passar disso, as janelas estão longas demais:
  troque `-n 20` por `-n 10` na função `janelas`;
- `deadline_misses = 0` e `crc_errors = 0`.

---

## 3. Blocos A e B — firmware legítimo, cinco condições · ≈ 1 h 30

O Bloco A é a condição nominal (`baseline`); o Bloco B são as quatro
estressadas. O procedimento é o mesmo, então fica numa função. Cole no H1:

```bash
condicao() {
  local S=$1 r
  echo "===== $S ====="
  pi "carga_on $S" || return 1
  [ "$S" != baseline ] && sleep 5          # deixa a carga estabilizar
  pi "prova_carga $S"

  # G3 + G4: trafego status, 2 capturas de 15 s
  pi "trafego_on status $S" || return 1
  for r in 1 2; do cap ${S}_status_r$r 4m 60M; done
  pi "trafego_off"

  # G2: trafego mix, 8 capturas de 15 s
  pi "trafego_on mix $S" || return 1
  for r in 1 2 3 4 5 6 7 8; do cap ${S}_mix_r$r 4m 60M; done
  pi "trafego_off"

  # G1 pelo MCU: ~120 s de trafego status, metricas no fim
  pi "zerar_metricas; argus-bench --run-rate 600 --cmd status -n 25000 --warmup 1000 --interval-us 2000 \
      --label $S -o \$OUT/G1_$S.csv --metrics \$OUT/G1_${S}_mcu.csv; reg_g1 $S"

  pi "carga_off"
}
```

Rode, uma condição por vez (≈ 15 min cada), olhando a saída:

```bash
condicao baseline
condicao cpu
condicao io
condicao net
condicao combined
```

**O que conferir em cada condição**

- `prova_carga`: em `baseline`, CPU baixa e 0 geradores; em `cpu` e
  `combined`, CPU perto de 100 % e load ≥ 4. Se `cpu` sair igual ao
  `baseline`, a carga não subiu: pare.
- `sigrok-cli` não pode dizer "Device only sent N samples". Se disser, refaça
  a captura (com rede, confirme o `-b 100M`).
- `reg_g1`: `loop_count` perto de 1.200 (120 s ÷ 100 ms). Se vier perto de
  zero, o tráfego não foi `status`. `deadline_misses` esperado: 0.

**Conferência rápida** de uma captura, logo após gravá-la:

```bash
python3 -I $SCR/sr_timing.py periodos $B2/la/baseline_status_r1.sr --canal D4 --out /dev/null
#  esperado: n ≈ 12.700, p50 = 1.180,0 µs (847,5 Hz)
python3 -I $SCR/sr_timing.py marcadores $B2/la/baseline_mix_r1.sr --a D5 --b D6 --out /dev/null
#  esperado: n ≈ 670, p50 ≈ 122 µs, máx ≈ 137 µs
```

---

## 4. Bloco C — período do laço com o marcador C · ≈ 45 min

G1 só vira distribuição com uma borda por volta do laço. O patch inverte o
nível de PD15 a cada volta que termina por timeout (a mesma condição da
métrica do MCU), e o intervalo entre bordas é o período do laço.

**O que o patch faz:** `markers.h` ganha `argus_marker_c_toggle()`, que só gera
código se existir o alias `la-marker-c`; `logic_engine.c` chama a função logo
depois de `hub_metrics_loop_tick()`, quando `poll_rc == -EAGAIN`. O alias vem
de um overlay usado **só** neste build. Verificado em 25/10/2026: sem o
overlay, o `zephyr.bin` sai byte a byte igual ao original; com ele, o código
cresce 324 bytes.

### Etapa 5 — Compilar e gravar o firmware de bancada

```bash
export ZEPHYR_BASE=~/zephyrproject/zephyr && source ~/zephyrproject/.venv/bin/activate
P=~/Documents/Argus_Artigo/plano_ieee/patches
rm -rf $B2/arvores/marcadorC && mkdir -p $B2/arvores
cp -r ~/Documents/Argus_Artigo/argus-amp-infusion-pump/control-domain $B2/arvores/marcadorC
rm -rf $B2/arvores/marcadorC/build*
(cd $B2/arvores/marcadorC && patch -p1 < $P/marcador_c.patch)

cd ~/zephyrproject      # o west precisa estar dentro do workspace
west build -p always -b nucleo_u575zi_q --sysbuild $B2/arvores/marcadorC \
  -d $B2/arvores/marcadorC/build -- -DEXTRA_DTC_OVERLAY_FILE=$P/marcador_c.overlay
grep -c la-marker-c $B2/arvores/marcadorC/build/marcadorC/zephyr/zephyr.dts   # tem de ser 1
west flash -d $B2/arvores/marcadorC/build
```

O firmware de bancada também se apresenta como v1.1.23. A única forma de
distingui-lo é o D3 alternando.

### Etapa 6 — Teste de 10 s

```bash
pi "trafego_on status teste"
cap C_teste 250k 2500000
pi "trafego_off"
python3 -I $SCR/sr_timing.py periodos $B2/la/C_teste.sr --canal D3 --bordas todas --out /dev/null
#  esperado: n ≈ 100, p50 ≈ 100.000 µs
```

Se n = 0, confira a ponta em PD15 (CN7-18) e se o `grep` da Etapa 5 deu 1.

### Etapa 7 — Seis condições, 120 s cada

```bash
condicao_c() {
  local S=$1
  echo "===== C $S ====="
  pi "carga_on $S" || return 1
  [ "$S" != baseline ] && [ "$S" != ocioso ] && sleep 5
  if [ "$S" = ocioso ]; then
    # STEP ativo, barramento parado: o laço sem nenhuma interferência
    pi "trafego_on status ocioso; sleep 1; matar '[a]rgus-bench'"
  else
    pi "trafego_on status C_$S"
  fi
  cap C_$S 250k 30M          # 120 s a 250 kHz (resolução de 4 µs)
  pi "trafego_off; carga_off"
}
for S in ocioso baseline cpu io net combined; do condicao_c $S; done
```

### Etapa 8 — Restaurar o firmware legítimo

```bash
cd ~/zephyrproject
west flash -d ~/Documents/Argus_Artigo/argus-amp-infusion-pump/control-domain/build-legitimo
```

Confira com uma captura curta que **D3 ficou parado**. Se o `build-legitimo`
não existir mais, refaça-o como na bancada 01, seção 3.1.

---

## 5. Extração e consolidação · 30 min

### Etapa 9 — Um CSV por captura

```bash
cd $B2
for S in baseline cpu io net combined; do
  for r in 1 2; do
    python3 -I $SCR/sr_timing.py periodos la/${S}_status_r$r.sr --canal D4 --out csv/G3_${S}_r$r.csv
    python3 -I $SCR/sr_timing.py largura  la/${S}_status_r$r.sr --canal D5 --out csv/G4_${S}_r$r.csv
  done
  for r in 1 2 3 4 5 6 7 8; do
    python3 -I $SCR/sr_timing.py marcadores la/${S}_mix_r$r.sr --a D5 --b D6 --out csv/G2_${S}_r$r.csv
  done
done
for S in ocioso baseline cpu io net combined; do
  python3 -I $SCR/sr_timing.py periodos la/C_$S.sr --canal D3 --bordas todas --out csv/G1_$S.csv
done
```

### Etapa 10 — Tabela, KS e ECDF

```bash
python3 -I $SCR/distribuicoes.py $B2/csv --out $B2/distribuicoes
```

Gera `tabela.md`/`tabela.csv` (n, mín., p50, p95, p99, p99,9, máx., média,
desvio, faixa das medianas entre repetições, e KS de cada condição contra
`baseline`) e `ecdf_G1.png` … `ecdf_G4.png` (largura de uma coluna IEEE, com o
deadline de 110 ms marcado em G1).

**Como ler o KS:** com milhares de amostras, p < 0,05 aparece até para
diferenças de fração de microssegundo. Por isso a tabela traz também D (a
maior distância entre as duas ECDFs) e a diferença de mediana em µs. Uma
diferença estatisticamente detectável e de 0,5 µs num orçamento de 100 ms não
muda a conclusão, mas deve ser reportada como é.

### Etapa 11 — Guardar

```bash
DEST=~/Documents/Argus_Artigo/resultados/bancada02/campanha-$(date +%Y%m%d)
mkdir -p $DEST
scp -r "$PI:/data/bench-timing-*" $DEST/gateway/
cp -r $B2/la $DEST/capturas
cp -r $B2/csv $B2/distribuicoes $DEST/
cp ~/Documents/Argus_Artigo/plano_ieee/patches/marcador_c.* $DEST/
```

Escreva um `resultados/bancada02/README.md` como índice, espelhando o da
bancada 01.

---

## 6. O produto e como ele volta ao artigo

### 6.1 Tabela a preencher

| Grandeza | Condição | n | p50 | p95 | p99 | máx | KS D (p) |
|---|---|---|---|---|---|---|---|
| G1 período do laço (µs) | ocioso | | | | | | — |
| | baseline | | | | | | |
| | combined | | | | | | |
| G2 quadro→atuação (µs) | baseline | | | | | | — |
| | combined | | | | | | |
| G3 período STEP (µs) | baseline | | | | | | — |
| | combined | | | | | | |
| G4 validação (µs) | baseline | | | | | | — |
| | combined | | | | | | |

(`cpu`, `io` e `net` ficam em `tabela.md`; no artigo, só se o espaço permitir.)

### 6.2 No texto

| Resultado | No artigo |
|---|---|
| G1–G4 sem diferença relevante entre nominal e estressado | uma figura de ECDF sobreposta (G2 ou G1) substitui ou complementa `fig:frame_to_actuation`; a legenda diz "distribution … under nominal and stressed conditions" |
| G1 com distribuição e 0 deadline miss | frase nova em `subsec:critical`: "across N loop iterations under combined stress, the loop period stayed within X µs of the 100 ms nominal (p99 = Y µs), with no deadline miss" |
| p95 e KS disponíveis | coluna p95 já existe em `tab:critical_timing`; acrescentar n total, número de capturas e o KS na legenda ou no texto |
| alguma distribuição muda sob estresse | reportar com o número; continua ordens de grandeza abaixo da janela de 100 ms, então não enfraquece o argumento de contenção |

**Uma ressalva que deve ir ao texto:** G1 mede o laço quando o barramento só
carrega `status`. Com comandos que atravessam a fila, o laço é acordado por
eles e o "período" deixa de existir como tal; o custo desses comandos para o
domínio crítico é o G2.

---

## 7. Ordem de execução

| Etapa | Conteúdo | Tempo |
|---|---|---|
| 1–4 | testes, biblioteca, atalhos, erro de fundo | 30 min |
| A+B | `condicao` × 5 | 1 h 15 |
| C (5–8) | build, flash, teste, 6 × 120 s, restaurar | 45 min |
| 9–11 | extração, tabela, figuras, guardar | 30 min |

**Se só houver tempo para uma parte, faça A+B.** Ela entrega G2–G4 densos e
repetidos nas cinco condições, mais G1 pelo MCU (mín., máx. e deadline misses
sobre ~1.200 voltas por condição, contra 2–13 na campanha publicada). O Bloco C
transforma G1 em distribuição.

---

## Apêndice A — Arquivos do marcador C

- `plano_ieee/patches/marcador_c.patch` — aplica com `patch -p1` dentro de uma
  cópia de `control-domain/`. Altera `include/markers.h` e
  `src/logic_engine.c`.
- `plano_ieee/patches/marcador_c.overlay` — alias `la-marker-c` em PD15.
- O patch não vai para o firmware de produção nem para o repositório: é
  instrumentação de bancada. Mesmo aplicado, sem o overlay ele não gera código.

## Apêndice B — Scripts

`plano_ieee/scripts/sr_timing.py` lê o `.sr` direto (zip com `metadata` e
blocos `logic-1-N`), sem decodificador do sigrok, e grava um valor por linha
em µs:

| Subcomando | Mede | Uso |
|---|---|---|
| `periodos --canal D4` | subida → subida | G3 |
| `periodos --canal D3 --bordas todas` | borda → borda | G1 (marcador C inverte o nível) |
| `marcadores --a D5 --b D6` | subida de A → subida de B seguinte | G2 |
| `largura --canal D5` | subida → descida de A | G4 (avisa se a captura for `mix`) |

`plano_ieee/scripts/distribuicoes.py` junta as repetições `_r<N>` de cada
condição e gera a tabela, o KS (scipy, com fórmula assintótica de reserva) e as
ECDFs (matplotlib). Paleta categórica validada para daltonismo, com estilo de
linha diferente por condição para impressão em tons de cinza.
