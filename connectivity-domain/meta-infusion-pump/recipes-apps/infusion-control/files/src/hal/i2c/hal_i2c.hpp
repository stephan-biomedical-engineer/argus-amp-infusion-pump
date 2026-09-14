#ifndef HAL_I2C_HPP
#define HAL_I2C_HPP

#include <cstdint>
#include <cstddef>

class HalI2C
{
public:
    HalI2C(const char* device_path, int slave_address);
    ~HalI2C();

    bool is_valid() const;

    bool write_byte(uint8_t reg, uint8_t data);
    bool read_byte(uint8_t reg, uint8_t& data);

    bool write_bytes(uint8_t reg, const uint8_t* data, size_t length);
    bool read_bytes(uint8_t reg, uint8_t* data, size_t length);

private:
    int _fd{-1};
};

#endif
