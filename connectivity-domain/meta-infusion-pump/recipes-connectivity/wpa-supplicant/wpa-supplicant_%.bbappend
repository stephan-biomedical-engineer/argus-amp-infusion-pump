FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# O arquivo versionado e um GABARITO com placeholders. As credenciais reais
# ficam em files/wpa_supplicant.conf-local, coberto pelo .gitignore -- nenhuma
# senha de rede deve entrar no repositorio.
SRC_URI += "file://wpa_supplicant.conf-sane"

WPA_LOCAL_CONF ?= "${THISDIR}/files/wpa_supplicant.conf-local"

inherit systemd

do_install:append() {
    install -d ${D}${sysconfdir}/wpa_supplicant
    if [ -f "${WPA_LOCAL_CONF}" ]; then
        install -m 600 "${WPA_LOCAL_CONF}" \
            ${D}${sysconfdir}/wpa_supplicant/wpa_supplicant-wlan0.conf
    else
        bbwarn "wpa-supplicant: instalando o gabarito -- a placa nao vai associar. Crie files/wpa_supplicant.conf-local a partir de wpa_supplicant.conf-sane."
        install -m 600 ${WORKDIR}/wpa_supplicant.conf-sane \
            ${D}${sysconfdir}/wpa_supplicant/wpa_supplicant-wlan0.conf
    fi
}

# O conteudo do arquivo local nao entra na assinatura da tarefa: depois de
# edita-lo, rode 'bitbake -c cleansstate wpa-supplicant'.

SYSTEMD_SERVICE:${PN} = "wpa_supplicant@wlan0.service"
SYSTEMD_AUTO_ENABLE = "enable"
