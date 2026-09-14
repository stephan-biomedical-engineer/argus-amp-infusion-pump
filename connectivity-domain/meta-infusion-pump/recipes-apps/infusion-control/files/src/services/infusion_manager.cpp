#include "infusion_manager.hpp"
#include <iostream>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <sys/wait.h>
#include <boost/json.hpp>

// ============================================================
// Utils
// ============================================================

static std::string state_to_string(uint8_t state)
{
    switch(state)
    {
    case 0:
        return "POWER_ON";
    case 1:
        return "IDLE";
    case 2:
        return "RUNNING";
    case 3:
        return "BOLUS";
    case 4:
        return "PURGE";
    case 5:
        return "PAUSED";
    case 6:
        return "KVO";
    case 7:
        return "END";
    case 8:
    case 9:
    case 10:
        return "ALARM";
    case 11:
        return "OFF";
    default:
        return "UNKNOWN(" + std::to_string(state) + ")";
    }
}

// ============================================================
// Ciclo de vida
// ============================================================

InfusionManager::InfusionManager(Stm32Bridge& bridge, HalGpio& reset_pin) : _bridge(bridge), _reset_pin(reset_pin) {}

InfusionManager::~InfusionManager()
{
    stop();
}

void InfusionManager::start()
{
    if(_running)
        return;

    _running = true;
    _monitor_thread = std::thread(&InfusionManager::monitor_loop, this);
    std::cout << "[MANAGER] Monitor iniciado\n";
}

void InfusionManager::stop()
{
    _running = false;

    if(_monitor_thread.joinable())
        _monitor_thread.join();
}

void InfusionManager::set_status_callback(StatusCallback cb)
{
    std::lock_guard<std::mutex> lock(_spi_mutex);
    _status_cb = cb;
}

// ============================================================
// Monitoramento STM32
// ============================================================

void InfusionManager::monitor_loop()
{
    int consecutive_errors = 0;

    while(_running)
    {
        // ============================================
        // Aguardando STM32 voltar após OTA (Swap do MCUboot)
        // ============================================
        if (_waiting_mcu)
        {
            cmd_cmds_t req{}, res{};
            bool ok = false;

            {
                std::lock_guard<std::mutex> lock(_spi_mutex);
                
                // 1. Tenta abrir o hardware do zero (reconfigura ioctls)
                _bridge.resume_hardware(); 
                
                // 2. Tenta a comunicação
                ok = _bridge.send_command(CMD_GET_STATUS_REQ_ID, &req, &res);

                if (ok)
                {
                    auto state = res.status_res.status_data.current_state;
                    // POWER_ON (0) ou IDLE (1) indica que a app subiu
                    if (state == 0 || state == 1) 
                    {
                        std::cout << "[OTA] STM32 Boot Concluido e App Detectada!\n";
                        _waiting_mcu = false;
                        _ota_running = false;
                        _maintenance_mode = false;
                        // Mantemos o hardware aberto (resume_hardware já foi chamado acima)
                    }
                }
                else 
                {
                    // 3. FALHOU: O STM32 ainda está em swap ou o driver SPI "sujou".
                    // Fechamos o device para garantir limpeza absoluta para a próxima tentativa.
                    _bridge.suspend_hardware();
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        // ============================================
        // Manutenção normal ou Modo OTA de Transferência
        // ============================================
        if(_maintenance_mode)
        {
            // Enquanto o stm32-updater está rodando, não tocamos no SPI
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        // ============================================
        // Monitoramento normal (Operação 1Hz)
        // ============================================
        cmd_cmds_t req{}, res{};
        bool ok;

        {
            std::lock_guard<std::mutex> lock(_spi_mutex);
            ok = _bridge.send_command(CMD_GET_STATUS_REQ_ID, &req, &res);
        }

        if(ok)
        {
            consecutive_errors = 0;

            if(_status_cb)
            {
                auto& s = res.status_res.status_data;
                boost::json::object json;
                json["state"] = state_to_string(s.current_state);
                json["infused_volume_ml"] = s.volume;
                json["real_rate_ml_h"] = s.flow_rate_set;
                _status_cb(boost::json::serialize(json));
            }
        }
        else
        {
            consecutive_errors++;

            if(consecutive_errors >= 5)
            {
                std::cerr << "[MANAGER] Perda de sincronia persistente ou Deadlock DMA detectado.\n";
                std::cerr << "[MANAGER] Acionando Hardware Reset (Out-of-band) no STM32...\n";
                
                // O hard_reset_stm32() já cuida do _spi_mutex e do suspend/resume_hardware internamente
                hard_reset_stm32();
                
                consecutive_errors = 0;
            }
            else
            {
                std::cerr << "[MANAGER] Falha de comunicação (" << consecutive_errors << "/5)\n";
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ============================================================
// Comandos
// ============================================================

CommandStatus InfusionManager::start_infusion()
{
    cmd_cmds_t req{}, res{};

    std::lock_guard<std::mutex> lock(_spi_mutex);

    if(!_bridge.send_command(CMD_ACTION_RUN_REQ_ID, &req, &res))
        return CMD_TRANSPORT_ERROR;

    return static_cast<CommandStatus>(res.action_res.status);
}

CommandStatus InfusionManager::pause_infusion()
{
    cmd_cmds_t req{}, res{};

    std::lock_guard<std::mutex> lock(_spi_mutex);

    if(!_bridge.send_command(CMD_ACTION_PAUSE_REQ_ID, &req, &res))
        return CMD_TRANSPORT_ERROR;

    return static_cast<CommandStatus>(res.action_res.status);
}

CommandStatus InfusionManager::stop_infusion()
{
    cmd_cmds_t req{}, res{};

    std::lock_guard<std::mutex> lock(_spi_mutex);

    if(!_bridge.send_command(CMD_ACTION_ABORT_REQ_ID, &req, &res))
        return CMD_TRANSPORT_ERROR;

    return static_cast<CommandStatus>(res.action_res.status);
}

CommandStatus InfusionManager::set_config(uint32_t volume_ml, uint32_t rate_ml_h)
{
    cmd_cmds_t req{}, res{};

    req.config_req.config.volume = volume_ml;
    req.config_req.config.flow_rate = rate_ml_h;
    req.config_req.config.diameter = 20;

    std::lock_guard<std::mutex> lock(_spi_mutex);

    if(!_bridge.send_command(CMD_SET_CONFIG_REQ_ID, &req, &res))
        return CMD_TRANSPORT_ERROR;

    return static_cast<CommandStatus>(res.config_res.status);
}

CommandStatus InfusionManager::start_bolus(uint32_t volume_ml, uint32_t rate_ml_h)
{
    cmd_cmds_t req{}, res{};
    req.bolus_req.payload.bolus_volume = volume_ml;
    req.bolus_req.payload.bolus_rate = rate_ml_h;

    std::lock_guard<std::mutex> lock(_spi_mutex);

    if(!_bridge.send_command(CMD_ACTION_BOLUS_REQ_ID, &req, &res))
    {
        return CMD_TRANSPORT_ERROR;
    }

    return res.action_res.status;
}

CommandStatus InfusionManager::start_purge(uint32_t rate_ml_h)
{
    cmd_cmds_t req{}, res{};

    // req.config_req.config.volume    = 0;
    // req.config_req.config.flow_rate = rate_ml_h;
    // req.config_req.config.diameter  = 1;

    std::lock_guard<std::mutex> lock(_spi_mutex);

    // if(!_bridge.send_command(CMD_SET_CONFIG_REQ_ID, &req, &res))
    //     return CMD_TRANSPORT_ERROR;

    // if(res.config_res.status != CMD_OK)
    //     return res.config_res.status;

    if(!_bridge.send_command(CMD_ACTION_PURGE_REQ_ID, &req, &res))
    {
        return CMD_TRANSPORT_ERROR;
    }

    return res.action_res.status;
}

// ============================================================
// OTA
// ============================================================

void InfusionManager::start_ota_process(const std::string& filepath)
{
    if(_ota_running)
    {
        std::cout << "[OTA] Ignorado — OTA já em andamento\n";
        return;
    }

    _ota_running = true;
    _maintenance_mode = true;

    // Criamos a thread para não bloquear o io.run() (Main Loop)
    std::thread([this, filepath]() {
        {
            // 1. LIBERAÇÃO TOTAL: Fecha o File Descriptor e libera o pino GPIO
            // Isso evita que o stm32-updater dispute o device /dev/spidev0.1
            std::lock_guard<std::mutex> lock(_spi_mutex);
            std::cout << "[OTA] Suspendendo hardware para o updater...\n";
            _bridge.suspend_hardware();
        }

        // 2. Executa o updater como processo externo
        std::string cmd = "/usr/bin/stm32-updater " + filepath;
        int ret = std::system(cmd.c_str());

        if(WEXITSTATUS(ret) == 0)
        {
            std::cout << "[OTA] Upload concluído com sucesso.\n";
            std::cout << "[OTA] Aguardando MCUboot realizar o Swap (Pode levar até 90s)...\n";
            
            // Ativamos a flag de espera. 
            // O monitor_loop() tentará dar resume_hardware() quando o STM32 responder.
            _waiting_mcu = true; 
            
            // Importante: Não damos resume_hardware() aqui. 
            // Deixamos o monitor_loop tentar falar com o STM32 em IDLE.
        }
        else
        {
            std::cerr << "[OTA] Processo de upload falhou (Retorno: " << WEXITSTATUS(ret) << ")\n";
            
            // Se falhou, tentamos retomar o hardware para a bomba não ficar "morta"
            std::lock_guard<std::mutex> lock(_spi_mutex);
            _bridge.resume_hardware();
            
            _ota_running = false;
            _maintenance_mode = false;
        }
    }).detach();
}

// ============================================================
// Reset físico STM32
// ============================================================

void InfusionManager::hard_reset_stm32()
{
    {
        std::lock_guard<std::mutex> lock(_spi_mutex);
        _bridge.suspend_hardware();
    }

    _reset_pin.set(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    _reset_pin.set(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    {
        std::lock_guard<std::mutex> lock(_spi_mutex);
        _bridge.resume_hardware();
    }

    std::cout << "[MANAGER] STM32 resetado\n";
}
