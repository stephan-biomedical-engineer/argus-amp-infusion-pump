SUMMARY = "U-Boot boot script for Infusion Pump RAUC"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "u-boot-mkimage-native"

SRC_URI = "file://boot.cmd"

# Herda deploy para podermos copiar para a pasta de imagens
inherit deploy

do_compile() {
    mkimage -C none -A arm -T script -d ${WORKDIR}/boot.cmd ${WORKDIR}/boot.scr
}

# NAO instale no rootfs (/boot), pois nao sera usado de la
# do_install() {
#     install -d ${D}/boot
#     install -m 0644 ${WORKDIR}/boot.scr ${D}/boot/boot.scr
# }

# Copia para o deploy com um nome EXCLUSIVO para nao conflitar com o da RPi
do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${WORKDIR}/boot.scr ${DEPLOYDIR}/boot-rauc.scr
}

addtask deploy after do_compile before do_build
