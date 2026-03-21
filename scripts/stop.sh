#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_DIR="$(dirname "$SCRIPT_DIR")/logs"

for engine in trading_engine strategy_engine market_engine; do
    pid_file="$PID_DIR/$engine.pid"
    if [ -f "$pid_file" ]; then
        pid=$(cat "$pid_file")
        if kill -0 "$pid" 2>/dev/null; then
            echo "Stopping $engine (PID $pid)..."
            kill -SIGTERM "$pid"
        fi
        rm -f "$pid_file"
    fi
done

echo "All engines stopped."
