#include "hal_spi.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <cstring>
#include <iostream>

HalSpi::HalSpi(const char* device_path, uint32_t speed_hz) : _speed(speed_hz), _stored_path(device_path)
{
    open_device();
}

HalSpi::~HalSpi()
{
    close_device();
}

void HalSpi::close_device()
{
    if(_fd >= 0)
    {
        ::close(_fd);
        _fd = -1;
        std::cout << "[SPI] Device fechado." << std::endl;
    }
}

bool HalSpi::open_device()
{
    // 1. Garantia de estado limpo: se houver um FD antigo, fecha.
    if(_fd >= 0) {
        close_device();
    }

    _fd = ::open(_stored_path.c_str(), O_RDWR);

    if(_fd < 0)
    {
        // Silenciamos o erro para não poluir o log durante o Swap do MCUboot
        // std::cerr << "[SPI] Falha ao abrir: " << _stored_path << std::endl;
        return false;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;

    // 2. Aplicação forçada de IOCTLs (Crucial após o stm32-updater fechar)
    if(ioctl(_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
       ioctl(_fd, SPI_IOC_RD_MODE, &mode) < 0) // Seta e valida
    {
        close_device();
        return false;
    }

    if(ioctl(_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0)
    {
        close_device();
        return false;
    }

    if(ioctl(_fd, SPI_IOC_WR_MAX_SPEED_HZ, &_speed) < 0)
    {
        close_device();
        return false;
    }

    std::cout << "[SPI] Barramento reinicializado: " << _stored_path << std::endl;
    return true;
}

bool HalSpi::transfer(const uint8_t* tx_buf, uint8_t* rx_buf, size_t len)
{
    if(_fd < 0)
        return false;

    struct spi_ioc_transfer tr;
    std::memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long) tx_buf;
    tr.rx_buf = (unsigned long) rx_buf;
    tr.len = (uint32_t) len;
    tr.speed_hz = _speed;
    tr.bits_per_word = 8;

    return ioctl(_fd, SPI_IOC_MESSAGE(1), &tr) >= 1;
}
