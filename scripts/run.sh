#!/bin/bash

TARGET_NAME=crypto-trading-system

cd "$(dirname "$0")/.."

> logs/system.log

nohup ./build/${TARGET_NAME} config/config.json >> logs/system.log 2>&1 &
