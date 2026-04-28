#!/bin/sh

set -e

cd "$(dirname "$0")/.."

if [ ! -f .deploy.env ]; then
    echo "Error: .deploy.env not found"
    echo "Create it with:"
    echo "  echo STRATEGY_DIR=/path/to/strategy/repo > .deploy.env"
    exit 1
fi

. ./.deploy.env

if [ -z "$STRATEGY_DIR" ]; then
    echo "Error: STRATEGY_DIR not set in .deploy.env"
    exit 1
fi

if [ ! -d "$STRATEGY_DIR/build" ]; then
    echo "Error: $STRATEGY_DIR/build not found. Run 'make' there first."
    exit 1
fi

mkdir -p strategies
count=0
for so in "$STRATEGY_DIR"/build/*/src/*.so; do
    [ -f "$so" ] || continue
    cp "$so" strategies/
    echo "Deployed: $(basename "$so")"
    count=$((count + 1))
done

if [ "$count" -eq 0 ]; then
    echo "Error: no .so files found in $STRATEGY_DIR/build/*/src/"
    exit 1
fi

bash scripts/archive.sh

TARGET_NAME=crypto-trading-system
nohup ./build/${TARGET_NAME} config/config.json > /dev/null 2>&1 &
echo "Started: pid=$!"
