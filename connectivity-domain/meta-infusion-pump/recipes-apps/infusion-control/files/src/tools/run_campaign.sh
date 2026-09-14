#!/bin/sh
# ===========================================================================
# run_campaign.sh — campanha de medicao, roda NA Raspberry Pi
# ===========================================================================
# Responde ao pedido dos revisores por cenarios declarados e numero de
# repeticoes explicito. Cada cenario roda REPS vezes; cada repeticao gera
# um CSV proprio, para que a variacao ENTRE repeticoes seja mensuravel e
# nao fique escondida dentro de uma media unica.
#
# Cenarios:
#   baseline   gateway ocioso
#   cpu        stress-ng saturando as 4 CPUs
#   io         stress-ng saturando I/O
#   net        iperf3 saturando a rede (exige servidor iperf3 no host)
#   combined   cpu + io + net simultaneos  <- o cenario do artigo
#   crcfault   injecao de falha de CRC (E6) -- mede deteccao e recuperacao
#
# Uso:
#   ./run_campaign.sh                          # todos, 5 repeticoes
#   ./run_campaign.sh -r 10 -n 5000            # 10 reps de 5000 transacoes
#   ./run_campaign.sh -s baseline,combined     # so esses dois
#   IPERF_SERVER=192.168.1.10 ./run_campaign.sh
#   ./run_campaign.sh -f 2000000               # SPI a 2 MHz (default: o do binario)
# ===========================================================================

set -u

BENCH="${BENCH:-argus-bench}"
OUTDIR="${OUTDIR:-/data/bench-$(date +%Y%m%d-%H%M%S)}"
REPS="${REPS:-5}"
NITER="${NITER:-2000}"
SCENARIOS="${SCENARIOS:-baseline,cpu,io,net,combined,crcfault}"
IPERF_SERVER="${IPERF_SERVER:-}"
SETTLE="${SETTLE:-3}"
SPI_HZ="${SPI_HZ:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        -r) REPS="$2"; shift 2 ;;
        -n) NITER="$2"; shift 2 ;;
        -s) SCENARIOS="$2"; shift 2 ;;
        -o) OUTDIR="$2"; shift 2 ;;
        -f) SPI_HZ="$2"; shift 2 ;;
        -h) sed -n '2,30p' "$0"; exit 0 ;;
        *)  echo "argumento desconhecido: $1"; exit 2 ;;
    esac
done

# Sem -f, bench e ataques usam a velocidade compilada (a do servico).
SPI_ARG=""
[ -n "$SPI_HZ" ] && SPI_ARG="--spi-hz $SPI_HZ"

log() { echo "[campanha] $*"; }

# --- Pre-requisitos -------------------------------------------------------
command -v "$BENCH" >/dev/null 2>&1 || { echo "ERRO: $BENCH nao encontrado"; exit 1; }
# shellcheck disable=SC2086
"$BENCH" $SPI_ARG --show-config >/dev/null || { echo "ERRO: velocidade SPI invalida: $SPI_HZ"; exit 2; }

# O servico e o bench disputam /dev/spidev0.1. Sem parar, a medida vira
# ruido de contencao de mutex, nao latencia do barramento.
SERVICE_WAS_ACTIVE=0
if systemctl is-active --quiet infusion-pump 2>/dev/null; then
    SERVICE_WAS_ACTIVE=1
    log "parando infusion-pump (disputa o mesmo spidev)"
    systemctl stop infusion-pump
    sleep 1
fi

cleanup() {
    pkill -f stress-ng 2>/dev/null
    pkill -f "iperf3 -c" 2>/dev/null
    if [ "$SERVICE_WAS_ACTIVE" = "1" ]; then
        log "religando infusion-pump"
        systemctl start infusion-pump
    fi
}
trap cleanup EXIT INT TERM

mkdir -p "$OUTDIR"
log "saida em $OUTDIR"

# --- Zera os contadores do MCU antes de comecar ----------------------------
# A leitura de metricas e destrutiva (firmware >= v1.1.23), entao a PRIMEIRA
# leitura da campanha cobriria tudo que passou pelo barramento desde a
# ultima leitura da campanha anterior -- inclusive os ataques, que rodam
# DEPOIS dela. Foi assim que a bench-p3big registrou 10 erros de CRC no
# "baseline", um cenario sem injecao nenhuma: eram os 10 quadros truncados
# pelo E9 da campanha anterior. (A primeira hipotese, o servico
# infusion-pump, estava errada: ele estava parado.)
#
# Uma leitura descartavel aqui zera o residuo, qualquer que seja a origem.
# Se o firmware for antigo (leitura nao destrutiva) isto nao faz mal
# nenhum, apenas nao adianta.
log "descartando metricas acumuladas antes da campanha"
# shellcheck disable=SC2086
"$BENCH" $SPI_ARG -n 1 --cmd status -o /dev/null --metrics /dev/null >/dev/null 2>&1
sleep 1

# --- Contexto do ambiente, gravado junto -----------------------------------
# Sem isso, o CSV daqui a tres meses e um numero sem procedencia.
{
    # BusyBox date nao tem -Is. Formato explicito funciona nos dois.
    echo "data=$(date '+%Y-%m-%dT%H:%M:%S%z')"
    echo "host=$(hostname)"
    echo "kernel=$(uname -a)"
    echo "cmdline=$(cat /proc/cmdline 2>/dev/null)"
    echo "ota_version=$(cat /etc/ota_version.txt 2>/dev/null | tr '\n' ' ')"
    echo "cpus=$(nproc)"
    echo "reps=$REPS niter=$NITER scenarios=$SCENARIOS"
    # Vem do proprio binario que vai medir. Antes era o texto "500000" fixo
    # aqui, que continuaria mentindo se a velocidade mudasse no codigo.
    # shellcheck disable=SC2086
    "$BENCH" $SPI_ARG --show-config
    echo "--- rauc ---"; rauc status 2>&1 | head -n 20
    echo "--- mount / ---"; mount | grep ' / ' 2>/dev/null
} > "$OUTDIR/environment.txt" 2>&1

start_load() {
    case "$1" in
        baseline) ;;
        crcfault) ;;   # sem carga externa: a perturbacao e o proprio CRC
        cpu)  stress-ng --cpu "$(nproc)" --timeout 0 >/dev/null 2>&1 & ;;
        io)   stress-ng --io 4 --vm 2 --vm-bytes 64M --timeout 0 >/dev/null 2>&1 & ;;
        net)
            [ -z "$IPERF_SERVER" ] && { log "IPERF_SERVER nao definido, pulando 'net'"; return 1; }
            iperf3 -c "$IPERF_SERVER" -t 100000 >/dev/null 2>&1 & ;;
        combined)
            stress-ng --cpu "$(nproc)" --io 4 --vm 2 --vm-bytes 64M --timeout 0 >/dev/null 2>&1 &
            [ -n "$IPERF_SERVER" ] && iperf3 -c "$IPERF_SERVER" -t 100000 >/dev/null 2>&1 &
            ;;
    esac
    return 0
}

stop_load() {
    pkill -f stress-ng 2>/dev/null
    pkill -f "iperf3 -c" 2>/dev/null
    sleep 1
}

# --- Campanha -------------------------------------------------------------
OLD_IFS="$IFS"; IFS=','
for scen in $SCENARIOS; do
    IFS="$OLD_IFS"
    log "=== cenario: $scen ==="

    if ! start_load "$scen"; then continue; fi

    # Deixa a carga estabilizar antes de medir: os primeiros segundos de
    # stress-ng ainda estao subindo threads e nao representam o regime.
    [ "$scen" != "baseline" ] && sleep "$SETTLE"

    rep=1
    while [ "$rep" -le "$REPS" ]; do
        csv="$OUTDIR/${scen}_rep${rep}.csv"
        log "  repeticao $rep/$REPS -> $(basename "$csv")"

        # E6: no cenario crcfault, corrompe 1 quadro a cada 50.
        FAULT_ARG=""
        [ "$scen" = "crcfault" ] && FAULT_ARG="--fault-every 50"

        # Modo "mix" por padrao: intercala comandos que atravessam a fila
        # do logic_engine. Sem isso o WCET e o deadline miss do caminho
        # comando->atuacao saem zero, porque GET_STATUS e respondido
        # direto no hub.c.
        CMD_MODE="${CMD_MODE:-mix}"
        # shellcheck disable=SC2086
        "$BENCH" $SPI_ARG -n "$NITER" -o "$csv" --label "$scen" --cmd "$CMD_MODE" $FAULT_ARG \
                 --metrics "$OUTDIR/${scen}_rep${rep}_mcu.csv" \
                 2>>"$OUTDIR/bench.log"
        rep=$((rep + 1))
    done

    stop_load
    IFS=','
done
IFS="$OLD_IFS"

# --- Cenarios de ataque (E7..E14) -----------------------------------------
# Rodam uma vez so: sao veredictos, nao distribuicoes. Repetir 5 vezes um
# teste que responde "aceito ou rejeitado" nao acrescenta informacao.
if [ "${RUN_ATTACKS:-1}" = "1" ] && command -v argus-attack >/dev/null 2>&1; then
    log "=== cenarios de ataque (E7, E8, E9, E13) ==="
    # shellcheck disable=SC2086
    argus-attack $SPI_ARG -o "$OUTDIR/ataques.csv" 2>&1 | tee -a "$OUTDIR/ataques.log"

    if [ "${WITH_RESET:-0}" = "1" ]; then
        log "=== E14: reset em laco (GPIO 4 -> NRST) ==="
        # shellcheck disable=SC2086
        argus-attack $SPI_ARG --only E14 --with-reset -o "$OUTDIR/ataque_e14.csv" \
            2>&1 | tee -a "$OUTDIR/ataques.log"
    else
        log "E14 pulado. Para medir: WITH_RESET=1 (exige GPIO 4 ligado ao NRST)"
    fi
fi

log "campanha concluida: $OUTDIR"
ls -la "$OUTDIR"
