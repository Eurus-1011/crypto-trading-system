#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
CONFIG="$PROJECT_DIR/config/config.json"
LOG_DIR="$PROJECT_DIR/logs"
PID_DIR="$PROJECT_DIR/logs"

mkdir -p "$LOG_DIR"

if [ ! -f "$CONFIG" ]; then
    echo "ERROR: config/config.json not found. Copy from config.json.template and fill in your keys."
    exit 1
fi

echo "Starting market_engine..."
"$BUILD_DIR/market_engine" "$CONFIG" &
echo $! > "$PID_DIR/market_engine.pid"
sleep 1

echo "Starting strategy_engine..."
"$BUILD_DIR/strategy_engine" "$CONFIG" &
echo $! > "$PID_DIR/strategy_engine.pid"
sleep 1

echo "Starting trading_engine..."
"$BUILD_DIR/trading_engine" "$CONFIG" &
echo $! > "$PID_DIR/trading_engine.pid"

echo "All engines started."
echo "  market_engine   PID: $(cat "$PID_DIR/market_engine.pid")"
echo "  strategy_engine PID: $(cat "$PID_DIR/strategy_engine.pid")"
echo "  trading_engine  PID: $(cat "$PID_DIR/trading_engine.pid")"
