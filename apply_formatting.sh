#!/bin/bash

TARGET_DIR="src"

if ! command -v clang-format &> /dev/null; then
    echo "clang-format is not installed"
    exit 1
fi

if [ ! -d "$TARGET_DIR" ]; then
    echo "Directory '$TARGET_DIR' does not exist"
    exit 1
fi

echo "Formatting files in '$TARGET_DIR'..."

find "$TARGET_DIR" -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.cc" -o -name "*.cxx" -o -name "*.h" -o -name "*.hpp" -o -name "*.hxx" \) -exec clang-format -i -style=file {} +

echo "Formatting complete"