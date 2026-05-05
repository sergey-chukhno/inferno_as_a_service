#!/bin/bash
for i in {1..50}; do
    echo "Run $i"
    lldb --batch -o "run" -o "bt" -o "quit" ./build/inferno_keylogger_interactive > crash_log.txt 2>&1
    grep "stopped" crash_log.txt > /dev/null
    if [ $? -eq 0 ]; then
        echo "CAUGHT IT"
        cat crash_log.txt
        exit 1
    fi
done
