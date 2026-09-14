#include <iostream>
#include <csignal>
#include <chrono>
#include <boost/asio.hpp>
#include <systemd/sd-daemon.h>

#include "hal_spi.hpp"
#include "spi_config.hpp"
#include "hal_gpio.hpp"
#include "stm32_bridge.hpp"
#include "infusion_manager.hpp"
#include "mqtt_client.hpp"

// Globais para Signal Handler
boost::asio::io_context* g_io = nullptr;
InfusionManager* g_manager = nullptr;

void signal_handler(int signal)
{
    std::cout << "\n[SYSTEM] Sinal " << signal << " recebido. Parando..." << std::endl;
    // Para o polling do hardware primeiro
    if(g_manager)
        g_manager->stop();
    // Para o loop de eventos (desconecta MQTT e cancela timers)
    if(g_io)
        g_io->stop();
}

// --- FUNÇÃO DO WATCHDOG (ASSÍNCRONA) ---
// Esta função é chamada periodicamente pelo io.run()
void watchdog_pulse(const boost::system::error_code& error, boost::asio::steady_timer* t)
{
    // Se o timer foi cancelado (shutdown), não faz nada
    if(error)
        return;

    // 1. "Chuta" o Watchdog do Systemd
    // Isso diz: "Ainda estou vivo e processando eventos!"
    sd_notify(0, "WATCHDOG=1");

    // (Opcional) Debug para ver no log se está vivo (remova em produção)
    // std::cout << "[WATCHDOG] Feeding..." << std::endl;

    // 2. Reagenda para daqui a 2 segundos
    // Usamos expires_after para evitar drift temporal
    t->expires_after(std::chrono::seconds(2));

    // 3. Coloca a si mesmo de volta na fila de execução
    t->async_wait([t](const boost::system::error_code& ec) { watchdog_pulse(ec, t); });
}

int main()
{
    // Permite logs imediatos no journalctl
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "--- BOMBA DE INFUSAO IOT (FINAL) ---" << std::endl;

    try
    {
        const char* node = "/dev/spidev0.1";
        const uint32_t spi_speed_hz = argus_spi::SPEED_HZ;   // ver spi_config.hpp
        // 1. Hardware Initialization
        HalSpi spi(node, spi_speed_hz);
        // Edge::Both, nao Rising: o handshake usa as DUAS bordas -- subida
        // significa "MCU pronto", descida significa "MCU consumiu o quadro".
        // Com so Rising, a espera pela descida cairia sempre em timeout.
        HalGpio ready_pin(5, HalGpio::Direction::Input, HalGpio::Edge::Both, false, "/dev/gpiochip0");
        HalGpio stm32_reset_pin(4, HalGpio::Direction::Output, HalGpio::Edge::None, false, "/dev/gpiochip0");
        stm32_reset_pin.set(true);

        // 2. Driver Layer
        Stm32Bridge bridge(spi, ready_pin);

        // 3. Service Layer (Manager)
        InfusionManager manager(bridge, stm32_reset_pin);
        g_manager = &manager;

        // 4. Server Layer (MQTT + IO Context)
        boost::asio::io_context io;
        g_io = &io;
        MqttClient mqtt(io, manager);

        // --- CONFIGURAÇÃO DO WATCHDOG ---
        // Cria um timer que dispara a cada 2 segundos.
        // Como o limite do systemd é 10s, 2s é seguro e responsivo.
        boost::asio::steady_timer watchdog_timer(io, std::chrono::seconds(2));

        // Inicia o ciclo do watchdog
        watchdog_timer.async_wait([&](const boost::system::error_code& ec) { watchdog_pulse(ec, &watchdog_timer); });
        // -------------------------------

        // Sinais do SO
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // Start
        manager.start(); // Inicia thread de polling do hardware (1Hz)
        mqtt.start();    // Conecta no Broker e inicia subs

        // Notifica Systemd que inicialização acabou
        sd_notify(0, "READY=1");
        std::cout << "[SYSTEM] Online. Watchdog ativo (2s). Aguardando comandos..." << std::endl;

        // Bloqueia aqui - O io.run() vai gerenciar:
        // 1. Mensagens MQTT (Rede)
        // 2. O Timer do Watchdog
        io.run();
    }
    catch(const std::exception& e)
    {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
