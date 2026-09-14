SUMMARY = "Boost.MQTT5 - C++17 MQTT client based on Boost.Asio"
HOMEPAGE = "https://github.com/boostorg/mqtt5"
LICENSE = "BSL-1.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSL-1.0;md5=65a7df9ad57aacf825fd252c4c33288c"

# Usando a branch que corresponde ao lançamento do Boost 1.90.0
SRC_URI = "git://github.com/boostorg/mqtt5.git;protocol=https;branch=develop"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

# allarch indica que os arquivos (headers) são iguais para qualquer CPU
inherit allarch

do_install() {
    install -d ${D}${includedir}
    # Copia o conteúdo da pasta include para o destino
    cp -rf ${S}/include/* ${D}${includedir}/
}

# Evita erro de pacote vazio já que não há binários .so
ALLOW_EMPTY:${PN} = "1"

# Indica que o pacote contém apenas arquivos de desenvolvimento (headers)
FILES:${PN}-dev = "${includedir}"
SYSROOT_DIRS += "${includedir}"