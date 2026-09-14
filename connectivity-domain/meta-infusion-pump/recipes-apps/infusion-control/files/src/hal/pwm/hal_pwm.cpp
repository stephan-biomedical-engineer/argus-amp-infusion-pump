
#include "hal_pwm.hpp"
#include <iostream>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string>

HalPWM::HalPWM(int chip, int channel) : _chip(chip), _channel(channel)
{
    // 1. Tenta Interface CDEV (Moderna)
    // No CDEV, o device costuma ser /dev/pwmchipX
    std::string device = "/dev/pwmchip" + std::to_string(_chip);
    _fd = open(device.c_str(), O_RDWR);

    if(_fd >= 0)
    {
        _is_cdev = true;
        // Nota: No CDEV, selecionamos o canal dentro da struct pwm_state no ioctl
    }
    else
    {
        // 2. Fallback para Sysfs
        _is_cdev = false;
        _base_path = "/sys/class/pwm/pwmchip" + std::to_string(_chip) + "/pwm" + std::to_string(_channel);
        _duty_path = _base_path + "/duty_cycle";

        _fd = open(_duty_path.c_str(), O_WRONLY);
        if(_fd < 0)
        {
            // Tenta exportar o canal se não existir
            std::string export_path = "/sys/class/pwm/pwmchip" + std::to_string(_chip) + "/export";
            int exp_fd = open(export_path.c_str(), O_WRONLY);
            if(exp_fd >= 0)
            {
                std::string s_chan = std::to_string(_channel);
                if(write(exp_fd, s_chan.c_str(), s_chan.size()) < 0)
                {
                    std::cerr << "HAL PWM: falha ao exportar canal PWM\n";
                }

                close(exp_fd);
                usleep(150000); // Aguarda o Kernel criar os arquivos
                _fd = open(_duty_path.c_str(), O_WRONLY);
            }
        }
        if(_fd >= 0)
            configure_sysfs_initial();
    }
}

void HalPWM::configure_sysfs_initial()
{
    auto set_sys = [](const std::string& path, const std::string& val) {
        int fd = open(path.c_str(), O_WRONLY);
        if(fd >= 0)
        {
            if(write(fd, val.c_str(), val.size()) < 0)
            {
                std::cerr << "HAL PWM: falha ao configurar " << path << "\n";
            }
            close(fd);
        }
    };
    set_sys(_base_path + "/period", std::to_string(_current_period));
    set_sys(_base_path + "/enable", "1");
}

void HalPWM::apply_state(uint64_t period_ns, uint64_t duty_ns, bool enabled)
{
    if(_fd < 0)
        return;

    if(!_is_cdev)
    {
        auto write_sys = [](const std::string& path, uint64_t val) {
            int fd = open(path.c_str(), O_WRONLY);
            if(fd >= 0)
            {
                std::string s = std::to_string(val);
                if(write(fd, s.c_str(), s.size()) < 0)
                {
                    std::cerr << "HAL PWM: falha ao escrever em " << path << "\n";
                }
                close(fd);
            }
        };

        write_sys(_base_path + "/duty_cycle", duty_ns);
        write_sys(_base_path + "/enable", enabled ? 1 : 0);
    }
}

void HalPWM::set_frequency(int hz)
{
    if(hz <= 0)
        return;
    _current_period = 1000000000ULL / hz;

    if(!_is_cdev)
    {
        int fd = open((_base_path + "/period").c_str(), O_WRONLY);
        if(fd >= 0)
        {
            std::string s_per = std::to_string(_current_period);
            if(write(fd, s_per.c_str(), s_per.size()) < 0)
            {
                std::cerr << "HAL PWM: falha ao escrever em " << (_base_path + "/period") << "\n";
            }
            close(fd);
        }
    }
    // Aplica com duty 0 para evitar erros de kernel (period < duty)
    apply_state(_current_period, 0, true);
}

void HalPWM::set_duty_cycle(int percentage)
{
    uint64_t duty_ns = (_current_period * (uint64_t) percentage) / 100;
    apply_state(_current_period, duty_ns, percentage > 0);
}

HalPWM::~HalPWM()
{
    if(_fd >= 0)
        close(_fd);
}
