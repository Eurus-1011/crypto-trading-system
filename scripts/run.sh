#!/bin/bash

cd "$(dirname "$0")/.."

> logs/system.log

nohup ./build/crypto-trading-system config/config.json >> logs/system.log 2>&1 &
