#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <zephyr/drivers/flash.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>
#endif
#include <errno.h>
#include "ota_handler.h"

LOG_MODULE_REGISTER(ota_handler, LOG_LEVEL_INF);

/*
 * OTA depende do MCUboot. Nos targets sem bootloader (perfil minimo da
 * Nucleo) as funcoes viram stubs: ota_write_chunk() devolve erro e o
 * hub.c traduz isso em NACK para o gateway, em vez de fingir gravacao.
 */
#if !defined(CONFIG_BOOTLOADER_MCUBOOT)

void ota_start(uint32_t total_size)
{
    ARG_UNUSED(total_size);
    LOG_WRN("OTA solicitado, mas este build nao tem MCUboot. Ignorado.");
}

int ota_write_chunk(uint8_t* data, size_t len)
{
    ARG_UNUSED(data);
    ARG_UNUSED(len);
    return -ENOTSUP;
}

void ota_finish(void)
{
    LOG_WRN("OTA finish sem MCUboot. Nada a aplicar.");
}

#else /* CONFIG_BOOTLOADER_MCUBOOT ------------------------------------- */

/* * Tempo suficiente para o Linux receber o ACK (500ms é seguro).
 * Se for muito curto, o STM32 reseta antes do bit sair pelo SPI.
 */
#define REBOOT_DELAY_MS 2000 

static struct flash_img_context ctx;

/* Item de trabalho do Kernel */
static struct k_work_delayable reboot_work;

/* Esta função roda no contexto do Sistema, fora da thread do SPI */
static void ota_reboot_worker(struct k_work *item)
{
    LOG_WRN(">>> OTA: Aplicando Update e Reiniciando (MCUboot Swap) <<<");
    
    // Pequeno delay para garantir que o log saia pela UART
    k_busy_wait(1000); 
    
    sys_reboot(SYS_REBOOT_COLD);
}

void ota_start(uint32_t total_size)
{
    LOG_INF("Iniciando OTA. Tamanho: %d bytes", total_size);
    flash_img_init(&ctx);
    
    // Inicializa a workqueue (seguro chamar várias vezes)
    k_work_init_delayable(&reboot_work, ota_reboot_worker);
}

int ota_write_chunk(uint8_t* data, size_t len)
{
    int ret = flash_img_buffered_write(&ctx, data, len, false);
    if(ret < 0)
        LOG_ERR("Erro gravando flash: %d", ret);
    return ret;
}

void ota_finish(void)
{
    // 1. Descarrega buffer restante na flash
    flash_img_buffered_write(&ctx, NULL, 0, true);

    LOG_INF("Download completo. Verificando e Agendando Swap...");

    // 2. Marca a imagem para teste
    if(boot_request_upgrade(BOOT_UPGRADE_TEST) == 0)
    {
        LOG_INF("Update valido! Agendando reboot para daqui a %d ms...", REBOOT_DELAY_MS);
        
        // 3. Agenda o reboot para o futuro.
        // O código continua rodando aqui, retorna para o hub.c, 
        // o hub.c envia o ACK pelo SPI, e SÓ DEPOIS o reboot acontece.
        k_work_schedule(&reboot_work, K_MSEC(REBOOT_DELAY_MS));
    }
    else
    {
        LOG_ERR("Falha ao agendar update no MCUboot!");
    }
}

#endif /* CONFIG_BOOTLOADER_MCUBOOT */
