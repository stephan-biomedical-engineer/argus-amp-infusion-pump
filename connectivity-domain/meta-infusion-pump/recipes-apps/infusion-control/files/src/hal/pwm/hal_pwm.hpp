#ifndef HAL_PWM_HPP
#define HAL_PWM_HPP

#include <string>
#include <cstdint> // Importante para int64_t e uint64_t

class HalPWM
{
public:
    // Agora aceita chip e canal (ex: chip=0, channel=1 para o GPIO 21)
    HalPWM(int chip = 0, int channel = 0);
    ~HalPWM();

    void apply_state(uint64_t period_ns, uint64_t duty_ns, bool enabled);
    void set_frequency(int hz);
    void set_duty_cycle(int percentage);

private:
    int _fd = -1;
    bool _is_cdev = false;
    int _chip;
    int _channel;
    uint64_t _current_period = 1000000; // 1kHz default

    std::string _base_path;
    std::string _duty_path;

    void configure_sysfs_initial();
};

#endif
