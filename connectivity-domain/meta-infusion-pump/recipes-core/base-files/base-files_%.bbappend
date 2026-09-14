FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Adiciona o diretório /data na lista de pastas padrão do sistema
dirs755 += "/data"

# Garante que o /data seja criado fisicamente na imagem
do_install:append() {
    install -d ${D}/data
}
