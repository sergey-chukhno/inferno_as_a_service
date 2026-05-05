#!/bin/bash
# Modify interactive test to sleep 0.1s instead of 10s
sed -i '' 's/10/1/g' tests/keylogger_interactive_test.cpp
cmake -B build -S . -G Ninja > /dev/null && ninja -C build
for i in {1..50}; do
    ./build/inferno_keylogger_interactive > /dev/null
    if [ $? -eq 133 ]; then
        echo "TRAPPED at $i"
        exit 1
    fi
done
echo "DONE without trap"
