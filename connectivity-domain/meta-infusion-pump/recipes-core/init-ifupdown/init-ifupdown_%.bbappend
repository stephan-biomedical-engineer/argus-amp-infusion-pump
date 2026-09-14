do_install:append () {
    # Aqui é seguro editar, pois estamos na receita dona do arquivo
    #echo "" >> ${D}${sysconfdir}/network/interfaces
    #echo "auto wlan0" >> ${D}${sysconfdir}/network/interfaces
    #echo "iface wlan0 inet dhcp" >> ${D}${sysconfdir}/network/interfaces
    #echo "    wpa-conf /etc/wpa_supplicant.conf" >> ${D}${sysconfdir}/network/interfaces
}
