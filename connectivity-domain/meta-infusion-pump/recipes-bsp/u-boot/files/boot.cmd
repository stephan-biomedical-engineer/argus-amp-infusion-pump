# ==========================================
# Script de Boot RAUC Inteligente
# Preserva configs da GPU e define Root A/B
# ==========================================

# 1. Defaults
test -n "${BOOT_ORDER}" || setenv BOOT_ORDER "A B"
test -n "${BOOT_A_LEFT}" || setenv BOOT_A_LEFT 3
test -n "${BOOT_B_LEFT}" || setenv BOOT_B_LEFT 3

# 2. Selecao de Slot (Define qual particao usar)
setenv boot_part_addr ""

for slot in "${BOOT_ORDER}"; do
    if test "x${boot_part_addr}" != "x"; then
        # Ja achou, pula
    else
        echo "Checking slot ${slot}..."
        if test "x${slot}" = "xA"; then
            if test ${BOOT_A_LEFT} -gt 0; then
                echo "Slot A valid -> /dev/mmcblk0p2"
                setenv rauc_slot "A"
                setenv root_part "/dev/mmcblk0p2"
                setenv boot_part_addr "found"
            fi
        fi
        if test "x${slot}" = "xB"; then
            if test ${BOOT_B_LEFT} -gt 0; then
                echo "Slot B valid -> /dev/mmcblk0p3"
                setenv rauc_slot "B"
                setenv root_part "/dev/mmcblk0p3"
                setenv boot_part_addr "found"
            fi
        fi
    fi
done

# 3. Fallback de Emergencia
if test "x${boot_part_addr}" = "x"; then
    echo "ERROR: No valid slot. Fallback to A."
    setenv rauc_slot "A"
    setenv root_part "/dev/mmcblk0p2"
fi

# 4. Salvar Estado
setenv rauc.slot ${rauc_slot}
saveenv

# 5. Carregar Kernel
echo "Loading Kernel..."
load mmc 0:1 ${kernel_addr_r} zImage

# 6. Preparar o Device Tree (DTB)
# Usamos o endereco da firmware (${fdt_addr}) para garantir config de RAM correta
fdt addr ${fdt_addr}

# 7. Resgatar os bootargs originais da GPU
# Lemos o que esta dentro de /chosen/bootargs no DTB e salvamos em 'orig_bootargs'
fdt get value orig_bootargs /chosen bootargs

# 8. Construir o novo bootargs
# Concatenamos: [Originais da GPU] + [Nossa escolha de Root] + [Configs Extras]
setenv bootargs "${orig_bootargs} root=${root_part} rauc.slot=${rauc_slot} rootfstype=ext4 rootwait panic=10"

echo "Booting with final args: ${bootargs}"

# 9. Boot usando Kernel do disco e DTB da Memoria
bootz ${kernel_addr_r} - ${fdt_addr}
