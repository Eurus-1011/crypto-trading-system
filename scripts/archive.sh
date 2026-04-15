#!/bin/bash

DIR="$(dirname "$(cd "$(dirname "$0")" && pwd)")"
LOG="$DIR/logs/system.log"

[ -f "$LOG" ] || { echo "No system.log found"; exit 1; }

first=$(head -1 "$LOG" | grep -oP '\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}')
last=$(tail -1 "$LOG" | grep -oP '\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}')
hours=$(( ($(date -d "$last" +%s) - $(date -d "$first" +%s)) / 3600 ))

mkdir -p "$DIR/logs/archived"

name="$(echo "${first%% *}" | tr -d '-')-${hours}h.log"
cp "$LOG" "$DIR/logs/archived/$name"
