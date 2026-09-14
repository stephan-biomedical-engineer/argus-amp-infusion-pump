#ifndef INFUSION_MANAGER_HPP
#define INFUSION_MANAGER_HPP

#include "stm32_bridge.hpp"
#include "cmd.h"
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <string>

// ============================================================
// Tipos
// ============================================================

// O HUB repassa exatamente o que o firmware respondeu
// O HUB apenas repassa o código bruto do firmware
using CommandStatus = uint8_t;

// Convenção local apenas para erro de transporte
static constexpr CommandStatus CMD_TRANSPORT_ERROR = 0xFF;

// Callback de status (JSON serializado)
using StatusCallback = std::function<void(std::string)>;

// ============================================================
// Classe
// ============================================================

class InfusionManager
{
public:
    InfusionManager(Stm32Bridge& bridge, HalGpio& reset_pin);
    ~InfusionManager();

    // Thread de monitoramento
    void start();
    void stop();

    // Status periódico do STM32
    void set_status_callback(StatusCallback cb);

    // --------------------------------------------------------
    // Comandos (retornam exatamente o status do firmware)
    // --------------------------------------------------------

    CommandStatus start_infusion();
    CommandStatus pause_infusion();
    CommandStatus stop_infusion(); // abort

    CommandStatus set_config(uint32_t volume_ml, uint32_t rate_ml_h);

    CommandStatus start_bolus(uint32_t volume_ml, uint32_t rate_ml_h);

    CommandStatus start_purge(uint32_t rate_ml_h);

    // --------------------------------------------------------
    // Manutenção
    // --------------------------------------------------------

    void start_ota_process(const std::string& filepath);
    void hard_reset_stm32();

private:
    // Hardware
    Stm32Bridge& _bridge;
    HalGpio& _reset_pin;

    // Proteção SPI
    std::mutex _spi_mutex;

    // Controle thread
    std::atomic<bool> _running{false};
    std::atomic<bool> _maintenance_mode{false};
    std::atomic<bool> _ota_running{false};
    std::atomic<bool> _waiting_mcu{false};

    std::thread _monitor_thread;

    // Callback status
    StatusCallback _status_cb;

    // Loop principal
    void monitor_loop();
};

#endif
