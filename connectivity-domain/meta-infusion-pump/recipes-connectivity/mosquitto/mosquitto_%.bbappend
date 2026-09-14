FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += " \
    file://mosquitto.conf \
    file://mosquitto.service.d/override.conf \
    file://acl \
    file://certs/ca.crt \
    file://certs/server.crt \
    file://certs/server.key \
"

do_install:append() {
    # Instala Config e ACL
    install -d ${D}${sysconfdir}/mosquitto
    install -m 0644 ${WORKDIR}/mosquitto.conf ${D}${sysconfdir}/mosquitto/mosquitto.conf
    install -m 0600 ${WORKDIR}/acl ${D}${sysconfdir}/mosquitto/acl

    # Instala Certificados (Modo 600/644 é crucial para segurança)
    install -d ${D}${sysconfdir}/mosquitto/certs
    install -m 0644 ${WORKDIR}/certs/ca.crt ${D}${sysconfdir}/mosquitto/certs/ca.crt
    install -m 0644 ${WORKDIR}/certs/server.crt ${D}${sysconfdir}/mosquitto/certs/server.crt
    install -m 0600 ${WORKDIR}/certs/server.key ${D}${sysconfdir}/mosquitto/certs/server.key

    # Pastas de sistema
    install -d ${D}/var/lib/mosquitto
    install -d ${D}${systemd_system_unitdir}/mosquitto.service.d
    install -m 0644 ${WORKDIR}/mosquitto.service.d/override.conf ${D}${systemd_system_unitdir}/mosquitto.service.d/override.conf
}

FILES:${PN} += " \
    /var/lib/mosquitto \
    ${systemd_system_unitdir}/mosquitto.service.d \
    ${sysconfdir}/mosquitto/certs \
    ${sysconfdir}/mosquitto/acl \
"
