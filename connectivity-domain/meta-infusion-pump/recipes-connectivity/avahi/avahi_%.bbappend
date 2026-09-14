FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += " \
    file://avahi-daemon.conf \
    file://argus-mqtt-discovery.service \
"

do_install:append() {
    install -m 0644 ${WORKDIR}/avahi-daemon.conf ${D}${sysconfdir}/avahi/avahi-daemon.conf
    install -d ${D}${sysconfdir}/avahi/services
    install -m 0644 ${WORKDIR}/argus-mqtt-discovery.service ${D}${sysconfdir}/avahi/services/argus-mqtt-discovery.service
}
