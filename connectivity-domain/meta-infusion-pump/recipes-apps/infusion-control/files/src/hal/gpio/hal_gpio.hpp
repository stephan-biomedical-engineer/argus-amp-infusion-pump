#ifndef HAL_GPIO_HPP
#define HAL_GPIO_HPP

#include <gpiod.h>
#include <string> // Necessário para guardar o caminho do chip

class HalGpio
{
public:
    enum class Direction
    {
        Input,
        Output
    };

    enum class Edge
    {
        None,
        Rising,
        Falling,
        Both
    };

    HalGpio(unsigned int pin, Direction dir, Edge edge = Edge::None, bool active_low = false,
            const char* chip = "/dev/gpiochip0");

    ~HalGpio();

    void set(bool high);
    bool get() const;

    // Métodos para OTA
    void release();
    bool acquire();

    Edge wait_for_edge(int timeout_ns);

private:
    // Configurações salvas (para poder fazer o acquire de volta)
    unsigned int _pin;
    std::string _chip_path;
    Direction _dir;
    Edge _edge;
    bool _active_low;

    // Handles da libgpiod
    gpiod_chip* _chip = nullptr;
    gpiod_line_request* _req = nullptr;
    struct gpiod_edge_event_buffer* _buffer = nullptr;
};

#endif
