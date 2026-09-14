#include "hal_gpio.hpp"
#include <iostream>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string>

// Construtor: Apenas salva as configs e chama acquire()
HalGpio::HalGpio(unsigned int pin, Direction dir, Edge edge, bool active_low, const char* chip_name)
    : _pin(pin), _chip_path(chip_name), _dir(dir), _edge(edge), _active_low(active_low)
{
    // Tenta pegar o pino imediatamente
    if(!acquire())
    {
        std::cerr << "HAL GPIO: Falha inicial ao adquirir pino " << _pin << "\n";
    }
}

HalGpio::~HalGpio()
{
    release(); // Libera o request e buffer
    if(_chip)
    {
        gpiod_chip_close(_chip);
        _chip = nullptr;
    }
}

// --- LIBERA O PINO (Para o OTA usar) ---
void HalGpio::release()
{
    if(_req)
    {
        gpiod_line_request_release(_req);
        _req = nullptr;
    }
    if(_buffer)
    {
        gpiod_edge_event_buffer_free(_buffer);
        _buffer = nullptr;
    }
    // Nota: Mantemos o _chip aberto para ser mais rápido reabrir,
    // mas o pino em si foi liberado ao soltar o _req.
}

// --- PEGA O PINO DE VOLTA ---
bool HalGpio::acquire()
{
    if(_req)
        return true; // Já temos o pino

    // 1. Abre o chip se não estiver aberto
    if(!_chip)
    {
        _chip = gpiod_chip_open(_chip_path.c_str());
        if(!_chip)
        {
            std::cerr << "HAL GPIO: falha ao abrir gpiochip\n";
            return false;
        }
    }

    // 2. Prepara as configurações (Lógica movida do antigo construtor)
    struct gpiod_line_config* line_cfg = gpiod_line_config_new();
    struct gpiod_line_settings* settings = gpiod_line_settings_new();

    gpiod_line_settings_set_direction(settings, _dir == Direction::Input ? GPIOD_LINE_DIRECTION_INPUT
                                                                         : GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_active_low(settings, _active_low);

    if(_edge != Edge::None)
    {
        gpiod_line_settings_set_edge_detection(settings, _edge == Edge::Rising    ? GPIOD_LINE_EDGE_RISING
                                                         : _edge == Edge::Falling ? GPIOD_LINE_EDGE_FALLING
                                                                                  : GPIOD_LINE_EDGE_BOTH);

        // Aloca buffer apenas se tiver borda
        if(!_buffer)
        {
            _buffer = gpiod_edge_event_buffer_new(64); // Tamanho buffer aumentado um pouco por segurança
        }
    }

    gpiod_line_config_add_line_settings(line_cfg, &_pin, 1, settings);

    struct gpiod_request_config* req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "infusion-pump-hal");

    // 3. Faz o Request (É aqui que o Kernel trava o pino para nós)
    _req = gpiod_chip_request_lines(_chip, req_cfg, line_cfg);

    // 4. Limpeza das structs de config temporárias
    gpiod_line_settings_free(settings);
    gpiod_line_config_free(line_cfg);
    gpiod_request_config_free(req_cfg);

    if(!_req)
    {
        std::cerr << "HAL GPIO: Falha ao requisitar linha " << _pin << "\n";
        return false;
    }

    return true;
}

void HalGpio::set(bool high)
{
    if(!_req)
        return;
    gpiod_line_request_set_value(_req, _pin, high ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
}

bool HalGpio::get() const
{
    if(!_req)
        return false;
    int val = gpiod_line_request_get_value(_req, _pin);
    return val == GPIOD_LINE_VALUE_ACTIVE;
}

HalGpio::Edge HalGpio::wait_for_edge(int timeout_ns)
{
    if(!_req || !_buffer)
        return Edge::None;

    int ret = gpiod_line_request_wait_edge_events(_req, timeout_ns);
    if(ret <= 0)
        return Edge::None;

    ret = gpiod_line_request_read_edge_events(_req, _buffer, 1);
    if(ret <= 0)
        return Edge::None;

    const gpiod_edge_event* event = gpiod_edge_event_buffer_get_event(_buffer, 0);
    // Cast necessário pois a lib retorna ponteiro const, mas em C++ às vezes precisamos manipular
    // (embora aqui seja só leitura, está ok)

    auto* ev_ptr = const_cast<struct gpiod_edge_event*>(event);

    switch(gpiod_edge_event_get_event_type(ev_ptr))
    {
    case GPIOD_EDGE_EVENT_RISING_EDGE:
        return Edge::Rising;
    case GPIOD_EDGE_EVENT_FALLING_EDGE:
        return Edge::Falling;
    default:
        return Edge::None;
    }
}
