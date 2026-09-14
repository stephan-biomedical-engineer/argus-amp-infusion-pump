#!/usr/bin/env bash
# ===========================================================================
# crossbuild_bench.sh — compila argus-bench para a Raspberry Pi Zero 2W
# ===========================================================================
# Roda no HOST. Nao precisa de sudo nem de SDK do Yocto: usa um container
# Debian armv7 emulado, que e a mesma arquitetura da imagem
# (machine raspberrypi0-2w, KERNEL_IMAGETYPE=zImage, bootz -> ARM 32-bit).
#
# Linka ESTATICO de proposito. O trixie traz glibc mais nova que a do
# scarthgap (2.39) da imagem; em link dinamico o binario pediria simbolos
# que a Pi nao tem. Estatico, nao depende de nada de la.
#
# Por que trixie e nao bookworm/ubuntu: o hal_gpio.cpp usa a API v2 do
# libgpiod. bookworm e ubuntu 24.04 ainda trazem a 1.6 (API v1).
#
# Uso:
#   ./tools/crossbuild_bench.sh
#   ./tools/crossbuild_bench.sh --deploy root@192.168.1.13
# ===========================================================================
set -euo pipefail

SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${SRC_DIR}/argus-bench-armv7"
IMAGE="debian:trixie-slim"
DEPLOY=""

while [ $# -gt 0 ]; do
    case "$1" in
        --deploy) DEPLOY="$2"; shift 2 ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "argumento desconhecido: $1"; exit 2 ;;
    esac
done

echo "[cross] compilando em ${IMAGE} (armv7 emulado)..."

docker run --rm --platform linux/arm/v7 \
    -v "${SRC_DIR}:/src" -w /src \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    "${IMAGE}" bash -euc '
        export DEBIAN_FRONTEND=noninteractive
        apt-get -qq update
        apt-get -qq install -y --no-install-recommends \
            g++ gcc libc6-dev libgpiod-dev >/dev/null

        INC="-Ihal/gpio -Ihal/spi -Ihal/i2c -Idrivers -Iutl -Iservices -Isystem"
        CXXFLAGS="-Wall -O2 -std=c++17 -static ${INC}"
        CFLAGS="-Wall -O2 -std=c99 -static ${INC}"

        mkdir -p /tmp/o
        for f in tools/argus_bench.cpp drivers/stm32_bridge.cpp \
                 hal/spi/hal_spi.cpp hal/gpio/hal_gpio.cpp; do
            echo "  CXX $f"
            g++ ${CXXFLAGS} -c "$f" -o "/tmp/o/$(basename "${f%.*}").o"
        done
        for f in drivers/cmd.c utl/utl_io.c utl/utl_crc16.c; do
            echo "  CC  $f"
            gcc ${CFLAGS} -c "$f" -o "/tmp/o/$(basename "${f%.*}").o"
        done

        echo "  LD  argus-bench-armv7"
        g++ -static /tmp/o/*.o -o /src/argus-bench-armv7 -lgpiod
        strip /src/argus-bench-armv7

        # O container roda como root; sem isto o binario sai com dono
        # root no host e o usuario nao consegue sobrescrever no proximo
        # build.
        chown "${HOST_UID}:${HOST_GID}" /src/argus-bench-armv7
    '

echo "[cross] pronto:"
file "${OUT}"
ls -la "${OUT}"

if [ -n "${DEPLOY}" ]; then
    echo "[cross] enviando para ${DEPLOY}:/usr/bin/argus-bench"
    scp "${OUT}" "${DEPLOY}:/usr/bin/argus-bench"
    scp "${SRC_DIR}/tools/run_campaign.sh" "${DEPLOY}:/usr/bin/run_campaign.sh"
    ssh "${DEPLOY}" 'chmod +x /usr/bin/argus-bench /usr/bin/run_campaign.sh && /usr/bin/argus-bench -h | head -3'
fi
