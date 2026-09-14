#ifndef MQTT_CLIENT_HPP
#define MQTT_CLIENT_HPP

#include <boost/asio.hpp>
#include <boost/mqtt5/mqtt_client.hpp>
#include <boost/mqtt5/types.hpp>
#include <boost/mqtt5/reason_codes.hpp>
#include <boost/json.hpp>
#include <iostream>
#include <string>
#include <vector>

#include "infusion_manager.hpp"

// ============================================================
// Configurações MQTT
// ============================================================

const std::string BROKER_ADDR = "127.0.0.1";
const uint16_t BROKER_PORT = 1883;
const std::string CLIENT_ID = "infusion_pump_rpi";

const std::string TOPIC_CMD = "bomba/comando";
const std::string TOPIC_STATUS = "bomba/status";

const uint32_t MAX_PURGE_RATE = 1200; // ml/h

// ============================================================
// Classe
// ============================================================

class MqttClient
{
public:
    using client_type = boost::mqtt5::mqtt_client<boost::asio::ip::tcp::socket>;

    MqttClient(boost::asio::io_context& io, InfusionManager& manager)
        : _io(io), _client(io), _manager(manager), _retry_timer(io)
    {
        setup_manager_callbacks();
    }

    void start()
    {
        _client.brokers(BROKER_ADDR, BROKER_PORT).credentials(CLIENT_ID).async_run(boost::asio::detached);

        subscribe_topics();
        receive_loop();
    }

private:
    boost::asio::io_context& _io;
    client_type _client;
    InfusionManager& _manager;
    boost::asio::steady_timer _retry_timer;

    // ========================================================
    // MQTT
    // ========================================================

    void subscribe_topics()
    {
        _client.async_subscribe(boost::mqtt5::subscribe_topic{TOPIC_CMD, boost::mqtt5::qos_e::at_least_once},
                                boost::mqtt5::subscribe_props{},
                                [this](boost::mqtt5::error_code ec, std::vector<boost::mqtt5::reason_code>, auto) {
                                    if(!ec)
                                        std::cout << "[MQTT] Inscrito em " << TOPIC_CMD << "\n";
                                    else
                                    {
                                        std::cerr << "[MQTT] Falha inscrição: " << ec.message() << " — retry em 5s\n";
                                        schedule_subscribe_retry();
                                    }
                                });
    }

    void schedule_subscribe_retry()
    {
        _retry_timer.expires_after(std::chrono::seconds(5));
        _retry_timer.async_wait([this](boost::system::error_code ec) {
            if(!ec)
                subscribe_topics();
        });
    }

    void receive_loop()
    {
        _client.async_receive([this](boost::mqtt5::error_code ec, std::string, std::string payload, auto) {
            if(!ec)
            {
                process_command(payload);
                receive_loop();
                return;
            }

            if(ec == boost::mqtt5::client::error::session_expired)
            {
                std::cout << "[MQTT] Sessão expirada — reinscrevendo\n";
                subscribe_topics();
            }

            if(ec != boost::asio::error::operation_aborted)
                receive_loop();
        });
    }

    // ========================================================
    // Tradução JSON → Firmware
    // ========================================================

    void process_command(const std::string& json_str)
    {
        try
        {
            boost::system::error_code ec;
            auto json_val = boost::json::parse(json_str, ec);

            if(ec)
            {
                std::cerr << "[MQTT] JSON inválido\n";
                return;
            }

            auto json = json_val.as_object();

            if(!json.contains("action"))
                return;

            std::string action = boost::json::value_to<std::string>(json.at("action"));

            std::cout << "[MQTT] Ação: " << action << "\n";

            CommandStatus status = CMD_TRANSPORT_ERROR;

            // ----------------------------------------------------
            // Comandos diretos
            // ----------------------------------------------------

            if(action == "start")
                status = _manager.start_infusion();

            else if(action == "pause")
                status = _manager.pause_infusion();

            else if(action == "stop" || action == "abort")
                status = _manager.stop_infusion();

            // ----------------------------------------------------
            // Configuração contínua
            // ----------------------------------------------------

            else if(action == "config")
            {
                if(json.contains("volume") && json.contains("rate"))
                {
                    uint32_t vol = json.at("volume").as_int64();
                    uint32_t rate = json.at("rate").as_int64();

                    status = _manager.set_config(vol, rate);
                }
                else
                {
                    std::cerr << "[MQTT] Config incompleta\n";
                    return;
                }
            }

            // ----------------------------------------------------
            // PURGE
            // ----------------------------------------------------

            else if(action == "purge")
            {
                uint32_t rate = json.contains("rate") ? json.at("rate").as_int64() : MAX_PURGE_RATE;

                status = _manager.start_purge(rate);
            }

            // ----------------------------------------------------
            // BOLUS
            // ----------------------------------------------------

            else if(action == "bolus")
            {
                uint32_t vol = json.contains("volume") ? json.at("volume").as_int64() : 5;

                uint32_t rate = json.contains("rate") ? json.at("rate").as_int64() : 600;

                status = _manager.start_bolus(vol, rate);
            }

            // ----------------------------------------------------
            // OTA
            // ----------------------------------------------------

            else if(action == "update_firmware")
            {
                if(json.contains("file_path"))
                {
                    std::string path = boost::json::value_to<std::string>(json.at("file_path"));

                    std::cout << "[OTA] Iniciando: " << path << "\n";
                    _manager.start_ota_process(path);
                    return;
                }
                else
                {
                    std::cerr << "[MQTT] OTA sem file_path\n";
                    return;
                }
            }

            // ----------------------------------------------------
            // Reset físico
            // ----------------------------------------------------

            else if(action == "reset_mcu")
            {
                _manager.hard_reset_stm32();
                return;
            }

            // ----------------------------------------------------
            // Resultado
            // ----------------------------------------------------

            if(status == CMD_OK)
                std::cout << "-> ACK\n";
            else
                std::cerr << "-> NACK: " << int(status) << "\n";
        }
        catch(const std::exception& e)
        {
            std::cerr << "[MQTT] Erro lógico: " << e.what() << "\n";
        }
    }

    // ========================================================
    // Callback de status
    // ========================================================

    void setup_manager_callbacks()
    {
        _manager.set_status_callback([this](std::string json_payload) {
            boost::asio::post(_io, [this, json_payload]() {
                _client.async_publish<boost::mqtt5::qos_e::at_most_once>(
                    TOPIC_STATUS, json_payload, boost::mqtt5::retain_e::no, boost::mqtt5::publish_props{},
                    [](boost::system::error_code ec) {
                        if(ec)
                            std::cerr << "[MQTT] Erro publish: " << ec.message() << "\n";
                    });
            });
        });
    }
};

#endif
