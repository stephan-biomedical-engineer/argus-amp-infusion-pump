#include "hal_i2c.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <i2c/smbus.h>
#include <cstdio>

HalI2C::HalI2C(const char* device_path, int slave_address)
{
    _fd = open(device_path, O_RDWR);
    if(_fd < 0)
    {
        perror("HAL I2C: falha ao abrir dispositivo I2C");
        return;
    }

    if(ioctl(_fd, I2C_SLAVE, slave_address) < 0)
    {
        perror("HAL I2C: falha ao configurar endereco do escravo");
        close(_fd);
        _fd = -1;
    }
};

HalI2C::~HalI2C()
{
    if(_fd >= 0)
    {
        close(_fd);
    }
};

bool HalI2C::is_valid() const
{
    return _fd >= 0;
}

bool HalI2C::write_byte(uint8_t reg, uint8_t data)
{
    if(!is_valid())
        return false;
    return i2c_smbus_write_byte_data(_fd, reg, data) >= 0;
}

bool HalI2C::read_byte(uint8_t reg, uint8_t& data)
{
    if(!is_valid())
        return false;

    int ret = i2c_smbus_read_byte_data(_fd, reg);
    if(ret < 0)
        return false;

    data = static_cast<uint8_t>(ret);
    return true;
}

bool HalI2C::write_bytes(uint8_t reg, const uint8_t* data, size_t length)
{
    if(!is_valid())
        return false;

    if(length == 0 || length > 32 || data == nullptr)
        return false;

    return i2c_smbus_write_i2c_block_data(_fd, reg, length, data) >= 0;
}

bool HalI2C::read_bytes(uint8_t reg, uint8_t* data, size_t length)
{
    if(!is_valid())
        return false;

    if(length == 0 || length > 32 || data == nullptr)
        return false;

    return i2c_smbus_read_i2c_block_data(_fd, reg, length, data) >= 0;
}
