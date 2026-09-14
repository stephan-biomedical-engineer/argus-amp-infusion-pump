#!/usr/bin/env bash
# ===========================================================================
# captura_bancada.sh — uma captura do analisador lógico, com condição fixa
# ===========================================================================
# Roda no HOST (onde está o analisador). Coordena a Raspberry Pi por ssh.
#
#   ./captura_bancada.sh <rotulo> <status|mix> <sem-carga|com-carga> [segundos]
#
# Exemplos:
#   ./captura_bancada.sh pulso_limpo   status sem-carga
#   ./captura_bancada.sh pulso_estress status com-carga
#   ./captura_bancada.sh atuacao_limpa mix    sem-carga
#
# O que ele garante, e é por isso que existe:
#   - a bomba está infundindo (senão o pino do pulso fica estático);
#   - a carga externa está no ar ANTES de a captura começar;
#   - a taxa e a duração são as mesmas em todas as capturas, para que a
#     comparação entre condições seja legítima;
#   - o bench e o estresse são encerrados no fim, mesmo se algo falhar.
# ===========================================================================

set -u

PI="${PI:-root@192.168.1.2}"
BENCH="${BENCH:-/data/bin/argus-bench}"
IPERF_HOST="${IPERF_HOST:-192.168.1.17}"
# Limite padrao da carga de rede. O iperf3 roda contra ESTE host, que e o
# mesmo que recebe as amostras pela USB; sem limite, o host atrasa o
# servico da USB e o fx2lafw entrega menos amostras do que o pedido.
IPERF_BW="${IPERF_BW:-100M}"
TAXA="${TAXA:-4m}"            # 4 MHz -> 0,25 us de resolução
AMOSTRAS="${AMOSTRAS:-60000000}"   # 60 M -> 15 s (120 M trunca no fx2lafw)
VAZAO="${VAZAO:-600}"         # mL/h da infusão durante a captura
SAIDA="${SAIDA:-capturas}"

rotulo="${1:-}"; modo="${2:-status}"; carga="${3:-sem-carga}"
[ -z "$rotulo" ] && { sed -n '4,20p' "$0"; exit 2; }

ssh_pi() { ssh -o StrictHostKeyChecking=no -o ConnectTimeout=8 "$PI" "$@"; }

# BusyBox não tem pkill: mata pelo PID.
matar_na_pi() {
    ssh_pi 'for p in $(ps 2>/dev/null | grep -E "[a]rgus-bench|[s]tress-ng|[i]perf3 -c" | awk "{print \$1}"); do
              kill -9 $p 2>/dev/null; done' >/dev/null 2>&1
}

limpar() {
    matar_na_pi
    [ "$carga" = "com-carga" ] && docker stop iperf3srv >/dev/null 2>&1
    ssh_pi 'systemctl start infusion-pump' >/dev/null 2>&1
}
trap limpar EXIT INT TERM

mkdir -p "$SAIDA"
arq="$SAIDA/${rotulo}.sr"

echo "== captura '$rotulo' | comandos=$modo | $carga | $TAXA por $((AMOSTRAS/4000000)) s"

command -v sigrok-cli >/dev/null || { echo "ERRO: sigrok-cli ausente"; exit 1; }
sigrok-cli --scan 2>/dev/null | grep -qi fx2lafw || {
    echo "ERRO: analisador nao encontrado. Conferir USB."; exit 1; }
ssh_pi 'echo ok' >/dev/null 2>&1 || { echo "ERRO: sem ssh para $PI"; exit 1; }

# --- serviço fora do caminho: ele disputa o mesmo spidev ---
ssh_pi 'systemctl stop infusion-pump' >/dev/null 2>&1
matar_na_pi
sleep 1

# --- carga externa, no ar antes da medição ---
if [ "$carga" = "com-carga" ]; then
    docker ps --format '{{.Names}}' | grep -q '^iperf3srv$' || \
        docker run -d --rm --name iperf3srv --network host networkstatic/iperf3 -s >/dev/null 2>&1
    ssh_pi "nohup stress-ng --cpu 4 --io 4 --vm 2 --vm-bytes 64M --timeout 300s >/dev/null 2>&1 &" >/dev/null 2>&1
    # A carga de rede vai CONTRA ESTE HOST, que e o mesmo que recebe as
    # amostras do analisador pela USB. Sem limite, o host se ocupa tratando
    # o trafego e atrasa o servico da USB -- o fx2lafw entao entrega menos
    # amostras do que o pedido ("Device only sent N samples"). Todas as
    # capturas truncadas ate agora foram com rede; nenhuma sem.
    #   SEM_REDE=1   -> so CPU, disco e memoria
    #   IPERF_BW=100M -> limita a banda (vazio = sem limite)
    if [ "${SEM_REDE:-0}" != "1" ]; then
        lim=""; [ -n "${IPERF_BW:-}" ] && lim="-b ${IPERF_BW}"
        # shellcheck disable=SC2086
        ssh_pi "nohup iperf3 -c $IPERF_HOST -t 300 $lim >/dev/null 2>&1 &" >/dev/null 2>&1
    fi
    echo "   carga externa no ar; 5 s para estabilizar"
    sleep 5
fi

# --- bomba infundindo + tráfego no barramento ---
ssh_pi "nohup sh -c '$BENCH --run-rate $VAZAO --cmd $modo -n 200000 --interval-us 2000 \
        -o /data/sessao_${rotulo}.csv > /data/sessao_${rotulo}.log 2>&1' >/dev/null 2>&1 &" >/dev/null 2>&1
sleep 4
if ! ssh_pi "grep -q 'infusao continua' /data/sessao_${rotulo}.log" 2>/dev/null; then
    echo "ERRO: a infusao nao iniciou; captura abortada"; exit 1
fi
echo "   bomba a ${VAZAO} mL/h, trafego em modo $modo"

# --- captura ---
sigrok-cli -d fx2lafw --config samplerate="$TAXA" --samples "$AMOSTRAS" -o "$arq" 2>&1 | tail -1
ls -la "$arq" | awk '{printf "   arquivo: %s (%.1f KB)\n", $9, $5/1024}'

# --- encerra e colhe o que o gateway viu ---
# O bench e encerrado antes de imprimir o resumo, entao a contagem vem das
# linhas de erro do proprio log.
erros=$(ssh_pi "grep -c 'Erro de Checksum' /data/sessao_${rotulo}.log" 2>/dev/null | tr -d '\r')
echo "   gateway: ${erros:-?} quadros recusados durante a captura"
matar_na_pi
sleep 1
echo "== fim"
