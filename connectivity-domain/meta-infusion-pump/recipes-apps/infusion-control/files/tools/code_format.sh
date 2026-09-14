#!/bin/bash

dirs=(
    ../src/drivers  
    ../src/hal/gpio
    ../src/hal/i2c
    ../src/hal/spi
    ../src/hal/pwm
    ../src/server_layer
    ../src/services
    ../src/system
    ../src/utl
    ../src/update_dir
)

for dir in "${dirs[@]}"; do
    echo "Formatting files in $dir"
    # ALTERAÇÃO AQUI: Adicionado -o -name "*.cpp" -o -name "*.hpp"
    find "$dir" -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -exec clang-format -style=file -i {} +
done
