#!/bin/bash

TARGET_NAME=crypto-trading-system

cd "$(dirname "$0")/.."

bash scripts/archive.sh

nohup ./build/${TARGET_NAME} config/config.json > /dev/null 2>&1 &
