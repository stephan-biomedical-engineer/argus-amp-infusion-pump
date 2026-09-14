#!/usr/bin/env bash
# =============================================================================
# Argus MCPS — Script de Build via kas-container
# =============================================================================
# Uso:
#   ./build.sh image      Gera imagem do SD Card (infusion-image)
#   ./build.sh bundle     Gera pacote OTA (infusion-bundle)
#   ./build.sh all        Gera imagem + bundle
#   ./build.sh shell      Abre shell interativo no container Kas
#   ./build.sh clean      Remove artefatos de build
#   ./build.sh --help     Mostra ajuda completa
# =============================================================================
set -euo pipefail

# ---------------------------------------------------------------------------
# Constantes
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KAS_DIR="${SCRIPT_DIR}/kas"
LAYER_DIR="${SCRIPT_DIR}/meta-infusion-pump"

# Determinar o executável do Kas (usar local se existir)
KAS_BIN="kas-container"
if [ -f "${SCRIPT_DIR}/kas-container" ]; then
    KAS_BIN="${SCRIPT_DIR}/kas-container"
fi

# ---------------------------------------------------------------------------
# Cache persistente
# ---------------------------------------------------------------------------
# Onde a maquina guarda downloads/ e sstate-cache/ e propriedade da
# MAQUINA, nao do projeto -- pelo mesmo motivo que nenhuma layer nomeia
# placa. Por isso vem de local.env, que fica no .gitignore.
#
# Exemplo de local.env:
#     CACHE_MOUNT="$HOME/hd"
#     DL_DIR="$CACHE_MOUNT/yocto/downloads"
#     SSTATE_DIR="$CACHE_MOUNT/yocto/sstate-cache"
#
# Se local.env nao existir, tudo fica em build/ no disco interno.
if [ -f "${SCRIPT_DIR}/local.env" ]; then
    # shellcheck disable=SC1091
    . "${SCRIPT_DIR}/local.env"
fi

export DL_DIR="${DL_DIR:-${SCRIPT_DIR}/build/downloads}"
export SSTATE_DIR="${SSTATE_DIR:-${SCRIPT_DIR}/build/sstate-cache}"

# Cache de OUTRO projeto, reaproveitado somente para leitura.
# Montado com a flag :ro do Docker -- protecao de kernel, nao de
# convencao. Ver kas/shared-cache.yml.
SHARED_RO_DL="${SHARED_RO_DL:-}"
SHARED_RO_SSTATE="${SHARED_RO_SSTATE:-}"
KAS_EXTRA_RUNTIME_ARGS=""
KAS_EXTRA_CONFIGS=""

# Cores
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ---------------------------------------------------------------------------
# Funções de utilidade
# ---------------------------------------------------------------------------
log_ok()   { echo -e "${GREEN}[OK]${NC}    $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_err()  { echo -e "${RED}[ERRO]${NC}  $*"; }
log_info() { echo -e "${CYAN}[INFO]${NC}  $*"; }

banner() {
    echo -e "${CYAN}"
    echo "╔═══════════════════════════════════════════════════════════╗"
    echo "║  Argus MCPS — Bomba de Infusão IoT                      ║"
    echo "║  Build System (Kas/Yocto Scarthgap)                     ║"
    echo "╚═══════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

usage() {
    echo -e "${BOLD}Uso:${NC} $0 <comando> [opções]"
    echo ""
    echo -e "${BOLD}Comandos:${NC}"
    echo "  image       Compila a imagem do SD Card (infusion-image)"
    echo "  bundle      Compila o pacote de atualização OTA (infusion-bundle)"
    echo "  all         Compila imagem + bundle sequencialmente"
    echo "  shell       Abre um shell interativo dentro do container Kas"
    echo "  eject       Desmonta com seguranca o disco de caches (ver local.env)"
    echo "  verify-cache  Confere que o cache protegido de outro projeto esta intacto"
    echo "  sdk         Gera SDK (toolchain+sysroot) para compilar o C++ no host"
    echo "  esdk        Gera eSDK (SDK + devtool, bem maior)"
    echo "  clean       Remove o diretório build/"
    echo ""
    echo -e "${BOLD}Opções:${NC}"
    echo "  --no-certs  Pula a verificação/geração de certificados"
    echo "  --help, -h  Mostra esta mensagem"
    echo ""
    echo -e "${BOLD}Variáveis de Ambiente:${NC}"
    echo "  DL_DIR      Diretório de downloads (default: build/downloads)"
    echo "  SSTATE_DIR  Diretório de sstate-cache (default: build/sstate-cache)"
    echo "  CACHE_MOUNT Ponto de montagem exigido para os caches (opcional)"
    echo ""
    echo "  Defina em local.env (gitignored). Modelo: local.env.example"
    echo ""
    echo -e "${BOLD}Exemplos:${NC}"
    echo "  $0 image                       # Imagem SD Card"
    echo "  $0 bundle                      # Pacote OTA"
    echo "  $0 all                         # Tudo"
    echo "  $0 shell                       # Shell interativo"
    echo "  DL_DIR=/mnt/cache/dl $0 image  # Cache externo"
}

# ---------------------------------------------------------------------------
# Verificar submódulos antigos
# ---------------------------------------------------------------------------
check_legacy_submodules() {
    local legacy_dirs=("poky" "meta-raspberrypi" "meta-openembedded" "meta-rauc")
    local found_legacy=()
    for d in "${legacy_dirs[@]}"; do
        # Nao basta o diretorio existir: o proprio kas clona todos eles.
        # O que caracteriza submodulo e o ".git" ser um ARQUIVO (gitlink
        # com "gitdir: ..."); num clone comum ele e um DIRETORIO.
        #
        # Antes desta distincao o aviso aparecia em todo build e mandava
        # apagar clones bons, forcando re-download de varios GB.
        if [ -d "${SCRIPT_DIR}/$d" ] && [ -f "${SCRIPT_DIR}/$d/.git" ]; then
            found_legacy+=("$d")
        fi
    done

    if [ ${#found_legacy[@]} -gt 0 ]; then
        log_warn "Diretórios de submódulos antigos detectados: ${found_legacy[*]}"
        log_warn "Isso causará o erro 'Repo is dirty - no checkout' no Kas."
        log_warn "Para corrigir, execute o comando abaixo e rode ./build.sh novamente:"
        echo -e "    ${BOLD}rm -rf ${found_legacy[*]}${NC}"
        echo ""
    fi
}

# ---------------------------------------------------------------------------
# Pré-requisitos
# ---------------------------------------------------------------------------
check_prerequisites() {
    local ok=true

    check_legacy_submodules

    if [ -f "$KAS_BIN" ]; then
        log_ok "Kas: usando executável local ($KAS_BIN)"
    elif command -v kas-container &>/dev/null; then
        log_ok "Kas: encontrado no PATH (sistema)"
    else
        log_err "kas-container não encontrado no PATH nem localmente."
        echo ""
        echo "    Instale com:"
        echo "      pip3 install kas"
        echo ""
        echo "    Ou baixe localmente:"
        echo "      curl -fsSL https://raw.githubusercontent.com/siemens/kas/master/kas-container -o kas-container"
        echo "      chmod +x kas-container"
        ok=false
    fi

    # Verificar container engine (docker ou podman)
    if command -v docker &>/dev/null; then
        log_ok "Container engine: docker"
    elif command -v podman &>/dev/null; then
        log_ok "Container engine: podman"
    else
        log_err "Docker ou Podman não encontrado. kas-container precisa de um deles."
        ok=false
    fi

    if [ "$ok" = false ]; then
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Certificados
# ---------------------------------------------------------------------------
check_certificates() {
    local needs_gen=false

    # Arquivos críticos que devem existir e ter conteúdo real (>10 bytes)
    local cert_files=(
        "${LAYER_DIR}/recipes-connectivity/mosquitto/files/certs/ca.crt"
        "${LAYER_DIR}/recipes-connectivity/mosquitto/files/certs/server.crt"
        "${LAYER_DIR}/recipes-connectivity/mosquitto/files/certs/server.key"
        "${LAYER_DIR}/recipes-core/bundles/files/development-1.key.pem"
        "${LAYER_DIR}/recipes-core/bundles/files/development-1.cert.pem"
        "${LAYER_DIR}/recipes-support/rauc/files/keyring.pem"
    )

    for cert in "${cert_files[@]}"; do
        if [ ! -f "$cert" ] || [ "$(wc -c < "$cert" 2>/dev/null)" -le 10 ]; then
            needs_gen=true
            break
        fi
    done

    if [ "$needs_gen" = true ]; then
        log_warn "Certificados de desenvolvimento ausentes ou são placeholders."
        log_info "Gerando certificados de desenvolvimento..."
        echo ""
        bash "${LAYER_DIR}/generate_dev_certs.sh"
        echo ""
        log_ok "Certificados gerados com sucesso."
    else
        log_ok "Certificados de desenvolvimento encontrados."
    fi
}

# ---------------------------------------------------------------------------
# Preparação de cache
# ---------------------------------------------------------------------------
prepare_cache() {
    # -----------------------------------------------------------------
    # Guarda de disco externo
    # -----------------------------------------------------------------
    # O modo de falha sem esta checagem e silencioso e caro: com o HD
    # desconectado, ~/hd volta a ser um diretorio comum e VAZIO no disco
    # interno. O kas cria os dois caminhos, o bitbake nao acha cache
    # nenhum, o build NAO falha -- ele rebaixa tudo e recompila do zero,
    # enchendo justamente o disco que essa estrategia existe para
    # esvaziar.
    #
    # So vale quando local.env define CACHE_MOUNT. Sem ele, cache local,
    # nada a checar.
    if [ -n "${CACHE_MOUNT:-}" ]; then
        if ! mountpoint -q "${CACHE_MOUNT}"; then
            log_err "CACHE_MOUNT nao esta montado: ${CACHE_MOUNT}"
            echo ""
            echo "  local.env aponta os caches para esse caminho, mas ele e um"
            echo "  diretorio comum no disco interno agora -- o HD nao esta"
            echo "  conectado ou nao montou."
            echo ""
            echo "  Seguir assim recompilaria tudo do zero e encheria o disco."
            echo ""
            echo "  Conecte o disco, ou monte com:  sudo mount ${CACHE_MOUNT}"
            echo "  Para buildar sem ele, comente CACHE_MOUNT em local.env."
            echo ""
            exit 1
        fi
        log_ok "CACHE_MOUNT montado: ${CACHE_MOUNT}"
    fi

    # -----------------------------------------------------------------
    # Guarda: os caches de ESCRITA do Argus nunca podem coincidir com --
    # nem morar dentro de -- um cache que queremos preservar.
    # -----------------------------------------------------------------
    local ro
    for ro in "${SHARED_RO_DL}" "${SHARED_RO_SSTATE}"; do
        [ -z "$ro" ] && continue
        local ro_abs; ro_abs="$(readlink -f "$ro" 2>/dev/null || echo "$ro")"
        local d; local s
        d="$(readlink -f "${DL_DIR}" 2>/dev/null || echo "${DL_DIR}")"
        s="$(readlink -f "${SSTATE_DIR}" 2>/dev/null || echo "${SSTATE_DIR}")"
        case "$d/" in "$ro_abs"/*)
            log_err "DL_DIR aponta para dentro do cache protegido:"
            echo "         DL_DIR      = $d"
            echo "         protegido   = $ro_abs"
            echo ""
            echo "  Escrever ali misturaria os dois projetos. Corrija local.env."
            exit 1 ;;
        esac
        case "$s/" in "$ro_abs"/*)
            log_err "SSTATE_DIR aponta para dentro do cache protegido:"
            echo "         SSTATE_DIR  = $s"
            echo "         protegido   = $ro_abs"
            echo ""
            echo "  Escrever ali misturaria os dois projetos. Corrija local.env."
            exit 1 ;;
        esac
    done

    # -----------------------------------------------------------------
    # Monta o cache alheio SOMENTE LEITURA dentro do container.
    # -----------------------------------------------------------------
    if [ -n "${SHARED_RO_DL}" ] || [ -n "${SHARED_RO_SSTATE}" ]; then
        if [ -n "${SHARED_RO_DL}" ]; then
            if [ ! -d "${SHARED_RO_DL}" ]; then
                log_err "SHARED_RO_DL nao existe: ${SHARED_RO_DL}"; exit 1
            fi
            KAS_EXTRA_RUNTIME_ARGS="${KAS_EXTRA_RUNTIME_ARGS} -v ${SHARED_RO_DL}:/downloads-ro:ro"
            log_ok "RO downloads = ${SHARED_RO_DL}"
        fi
        if [ -n "${SHARED_RO_SSTATE}" ]; then
            if [ ! -d "${SHARED_RO_SSTATE}" ]; then
                log_err "SHARED_RO_SSTATE nao existe: ${SHARED_RO_SSTATE}"; exit 1
            fi
            KAS_EXTRA_RUNTIME_ARGS="${KAS_EXTRA_RUNTIME_ARGS} -v ${SHARED_RO_SSTATE}:/sstate-ro:ro"
            log_ok "RO sstate    = ${SHARED_RO_SSTATE}"
        fi
        KAS_EXTRA_CONFIGS=":${KAS_DIR}/shared-cache.yml"
        # remove o espaco inicial acumulado nas concatenacoes acima
        KAS_EXTRA_RUNTIME_ARGS="${KAS_EXTRA_RUNTIME_ARGS# }"
    fi

    mkdir -p "${DL_DIR}" "${SSTATE_DIR}"
    log_ok "DL_DIR     = ${DL_DIR}"
    log_ok "SSTATE_DIR = ${SSTATE_DIR}"
}

# ---------------------------------------------------------------------------
# Manifesto de integridade do cache protegido
# ---------------------------------------------------------------------------
cache_fingerprint() {
    local dir="$1"
    [ -d "$dir" ] || { echo "ausente"; return; }
    echo "$(find "$dir" -type f 2>/dev/null | wc -l) arquivos, $(du -sb "$dir" 2>/dev/null | cut -f1) bytes"
}

verify_cache() {
    if [ -z "${SHARED_RO_DL}${SHARED_RO_SSTATE}" ]; then
        log_warn "Nenhum cache protegido configurado em local.env."
        exit 0
    fi
    local stamp="${SCRIPT_DIR}/.cache-fingerprint"
    local now=""
    [ -n "${SHARED_RO_DL}" ]     && now="${now}dl:$(cache_fingerprint "${SHARED_RO_DL}")\n"
    [ -n "${SHARED_RO_SSTATE}" ] && now="${now}sstate:$(cache_fingerprint "${SHARED_RO_SSTATE}")\n"

    if [ -f "$stamp" ]; then
        if [ "$(printf "%b" "$now")" = "$(cat "$stamp")" ]; then
            log_ok "Cache protegido INTACTO (identico ao registrado)."
            printf "%b" "$now" | sed 's/^/         /'
        else
            log_err "Cache protegido MUDOU desde o ultimo registro!"
            echo "  --- registrado ---"; sed 's/^/    /' "$stamp"
            echo "  --- agora ---"; printf "%b" "$now" | sed 's/^/    /'
            exit 1
        fi
    else
        printf "%b" "$now" > "$stamp"
        log_ok "Estado do cache protegido registrado em .cache-fingerprint"
        printf "%b" "$now" | sed 's/^/         /'
    fi
}

# ---------------------------------------------------------------------------
# Ejetar o disco de caches
# ---------------------------------------------------------------------------
# Existe porque o HD de caches e o cartao SD aparecem como dispositivos
# vizinhos. Gravar a imagem no dispositivo errado destroi o cache inteiro,
# e e um erro de UMA tecla.
run_eject() {
    if [ -z "${CACHE_MOUNT:-}" ]; then
        log_warn "CACHE_MOUNT nao definido em local.env. Nada a ejetar."
        exit 0
    fi
    if ! mountpoint -q "${CACHE_MOUNT}"; then
        log_ok "${CACHE_MOUNT} ja esta desmontado."
        exit 0
    fi
    sync
    if sudo umount "${CACHE_MOUNT}"; then
        log_ok "Disco de caches desmontado: ${CACHE_MOUNT}"
        echo "  Agora e seguro identificar o cartao SD com lsblk."
    else
        log_err "Falha ao desmontar. Algum processo ainda usa o disco?"
        echo "  Verifique com: lsof +D ${CACHE_MOUNT}"
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Comandos de build
# ---------------------------------------------------------------------------
run_build() {
    local config="$1"
    local label="$2"

    echo ""
    log_info "Iniciando build: ${BOLD}${label}${NC}"
    log_info "Config: ${config}"
    echo ""

    # shellcheck disable=SC2086
    # ATENCAO a ordem: "kas-container [OPTIONS] build [KASOPTIONS] [KASFILE]".
    # --runtime-args e opcao do CONTAINER e precisa vir ANTES do
    # subcomando; depois dele, o kas-container o repassa ao kas Python e
    # o "-v" acaba interpretado como nome de arquivo de configuracao.
    if [ -n "${KAS_EXTRA_RUNTIME_ARGS}" ]; then
        "$KAS_BIN" --runtime-args "${KAS_EXTRA_RUNTIME_ARGS}" build "${config}${KAS_EXTRA_CONFIGS}"
    else
        "$KAS_BIN" build "${config}${KAS_EXTRA_CONFIGS}"
    fi

    echo ""
    log_ok "Build concluído: ${label}"
}

# ---------------------------------------------------------------------------
# SDK / eSDK
# ---------------------------------------------------------------------------
# Gera um toolchain com sysroot para compilar os binarios C++ do projeto
# no host, SEM refazer a imagem inteira.
#
#   sdk   -> populate_sdk      toolchain + sysroot. Compila o app. ~300 MB.
#   esdk  -> populate_sdk_ext  o anterior mais devtool, permite mexer em
#                              receitas e gerar pacotes. Alguns GB e bem
#                              mais lento de gerar.
#
# Para so recompilar infusion-pump-app / argus-bench, o "sdk" basta.
run_sdk() {
    local machine_yml="$1"
    local task="$2"
    local label="$3"

    echo ""
    log_info "Gerando ${BOLD}${label}${NC} (tarefa ${task})"
    log_warn "Isto leva bastante tempo na primeira vez."
    echo ""

    if [ -n "${KAS_EXTRA_RUNTIME_ARGS}" ]; then
        "$KAS_BIN" --runtime-args "${KAS_EXTRA_RUNTIME_ARGS}" build \
            --target infusion-image --task "${task}" \
            "${machine_yml}:${KAS_DIR}/image.yml${KAS_EXTRA_CONFIGS}"
    else
        "$KAS_BIN" build --target infusion-image --task "${task}" \
            "${machine_yml}:${KAS_DIR}/image.yml${KAS_EXTRA_CONFIGS}"
    fi

    echo ""
    log_ok "${label} gerado. Instalador em:"
    ls -1 "${SCRIPT_DIR}"/build/tmp/deploy/sdk/*.sh 2>/dev/null | sed 's/^/    /'
    echo ""
    echo "  Instale com:   <instalador>.sh -d ~/argus-sdk"
    echo "  Use com:       . ~/argus-sdk/environment-setup-*"
    echo "                 cd meta-infusion-pump/recipes-apps/infusion-control/files/src"
    echo "                 make"
}

run_shell() {
    echo ""
    log_info "Abrindo shell interativo no container Kas..."
    log_info "Use 'bitbake <target>' para builds manuais."
    echo ""

    "$KAS_BIN" shell "${KAS_DIR}/image.yml"
}

run_clean() {
    local build_dir="${SCRIPT_DIR}/build"
    if [ -d "$build_dir" ]; then
        log_warn "Removendo ${build_dir}/ ..."
        rm -rf "$build_dir"
        log_ok "Limpeza concluída."
    else
        log_ok "Nada para limpar (build/ não existe)."
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    local command=""
    local machine_target="raspberrypi0-2w" # default
    local skip_certs=false

    # Parse argumentos
    while [[ $# -gt 0 ]]; do
        case "$1" in
            image|bundle|all|shell|clean|eject|verify-cache|sdk|esdk)
                command="$1"
                shift
                ;;
            raspberrypi3|raspberrypi0-2w)
                machine_target="$1"
                shift
                ;;
            --no-certs)
                skip_certs=true
                shift
                ;;
            --help|-h)
                banner
                usage
                exit 0
                ;;
            *)
                log_err "Argumento desconhecido: $1"
                echo ""
                usage
                exit 1
                ;;
        esac
    done

    banner

    if [ -z "$command" ]; then
        usage
        exit 1
    fi

    # Clean e eject não precisam de pré-requisitos
    if [ "$command" = "clean" ]; then
        run_clean
        exit 0
    fi

    if [ "$command" = "eject" ]; then
        run_eject
        exit 0
    fi

    if [ "$command" = "verify-cache" ]; then
        verify_cache
        exit 0
    fi

    # Verificações
    check_prerequisites

    if [ "$skip_certs" = false ]; then
        check_certificates
    fi

    prepare_cache

    local machine_yml="${KAS_DIR}/machines/${machine_target}.yml"
    if [ ! -f "$machine_yml" ]; then
        log_err "Target de maquina invalido: $machine_target. Arquivo $machine_yml nao encontrado."
        exit 1
    fi

    # Execução
    case "$command" in
        image)
            run_build "${machine_yml}:${KAS_DIR}/image.yml" "Imagem SD Card (infusion-image) - ${machine_target}"
            ;;
        bundle)
            run_build "${machine_yml}:${KAS_DIR}/bundle.yml" "Bundle OTA (infusion-bundle) - ${machine_target}"
            ;;
        all)
            run_build "${machine_yml}:${KAS_DIR}/image.yml" "Imagem SD Card (infusion-image) - ${machine_target}"
            run_build "${machine_yml}:${KAS_DIR}/bundle.yml" "Bundle OTA (infusion-bundle) - ${machine_target}"
            ;;
        sdk)
            run_sdk "${machine_yml}" "populate_sdk" "SDK (toolchain + sysroot) - ${machine_target}"
            ;;
        esdk)
            run_sdk "${machine_yml}" "populate_sdk_ext" "eSDK (com devtool) - ${machine_target}"
            ;;
        shell)
            if [ -n "${KAS_EXTRA_RUNTIME_ARGS}" ]; then
                "$KAS_BIN" --runtime-args "${KAS_EXTRA_RUNTIME_ARGS}" shell \
                    "${machine_yml}:${KAS_DIR}/image.yml${KAS_EXTRA_CONFIGS}"
            else
                "$KAS_BIN" shell "${machine_yml}:${KAS_DIR}/image.yml${KAS_EXTRA_CONFIGS}"
            fi
            ;;
    esac

    echo ""
    echo -e "${GREEN}${BOLD}✔ Concluído com sucesso!${NC}"
}

main "$@"
