#!/bin/bash

dirs=(
    ../include/
    ../src/
    ../utl/
    ../test/
)

for dir in "${dirs[@]}"; do
    echo "Formatting files in $dir"
    find "$dir" -type f \( -name "*.c" -o -name "*.h" \) -exec clang-format -style=file -i {} +
done

