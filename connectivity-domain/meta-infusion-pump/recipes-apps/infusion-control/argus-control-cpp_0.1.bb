SUMMARY = "Argus Medical Pump Control System Firmware (Final Architecture)"
DESCRIPTION = "Firmware C++ with MQTT, Boost.Asio, HAL and OTA Updater"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Dependencies
DEPENDS = "boost boost-mqtt5 systemd libgpiod openssl i2c-tools"
RDEPENDS:${PN} = "boost-system boost-thread libgpiod systemd"
TARGET_CC_ARCH += "${LDFLAGS}"

# Files source
SRC_URI = " \
    file://src \
    file://infusion-pump.service \
"

S = "${WORKDIR}/src"
EXTRA_OEMAKE += "STRIP_OPT=''"

# --- COMPILE ---
# Yocto exports environment variables CXX, LDFLAGS, etc.
# The oe_runmake exports them to Makefile.
do_compile() {
    # Limpa antes de compilar.
    #
    # SRC_URI usa "file://src", entao o bitbake copia o diretorio de
    # fontes inteiro para o WORKDIR -- inclusive binarios que tenham
    # sobrado de um "make" rodado no host com o SDK. O make veria os
    # alvos atualizados, nao recompilaria, e o do_package instalaria
    # binarios ja com strip:
    #
    #   QA Issue: File '/usr/bin/...' was already stripped
    #
    # (O EXTRA_OEMAKE STRIP_OPT='' nao salva desse caso: ele so tem
    # efeito sobre o que for de fato recompilado.)
    oe_runmake clean
    oe_runmake
}

# --- INSTALL ---
do_install() {
    # Install daemon
    install -d ${D}${bindir}
    install -m 0755 ${S}/infusion-pump-app ${D}${bindir}/

    # Install updater
    install -m 0755 ${S}/stm32-updater ${D}${bindir}/

    # Install bench e attack (instrumentos de medicao para o artigo)
    install -m 0755 ${S}/argus-bench ${D}${bindir}/
    install -m 0755 ${S}/argus-attack ${D}${bindir}/

    # Script de campanha: precisa estar na placa, nao so no host
    install -m 0755 ${S}/tools/run_campaign.sh ${D}${bindir}/

    # Install systemd
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/infusion-pump.service ${D}${systemd_system_unitdir}/infusion-pump.service
}

# Systemd config
inherit systemd
SYSTEMD_SERVICE:${PN} = "infusion-pump.service"
SYSTEMD_AUTO_ENABLE = "enable"
