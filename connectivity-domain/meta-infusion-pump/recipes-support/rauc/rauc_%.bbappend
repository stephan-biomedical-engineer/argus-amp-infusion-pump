FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += " \
    file://system.conf \
    file://keyring.pem \
    file://fw_env.config \
"

do_install:append() {
    install -d ${D}${sysconfdir}/rauc
    install -m 0644 ${WORKDIR}/system.conf ${D}${sysconfdir}/rauc/system.conf
    install -m 0644 ${WORKDIR}/keyring.pem ${D}${sysconfdir}/rauc/keyring.pem
    
    install -d ${D}${sysconfdir}
    install -m 0644 ${WORKDIR}/fw_env.config ${D}${sysconfdir}/fw_env.config
}
