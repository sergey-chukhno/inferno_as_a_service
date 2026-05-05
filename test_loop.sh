#!/bin/bash
for i in {1..20}; do
    echo "Run $i"
    ./build/inferno_keylogger_interactive &
    PID=$!
    sleep 1
    kill -15 $PID
    wait $PID
    if [ $? -eq 133 ]; then
        echo "TRAPPED!"
        exit 1
    fi
done
