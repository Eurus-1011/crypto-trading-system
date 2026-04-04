#!/bin/sh

cd "$(dirname "$0")/.."

find . -path ./third_party -prune -o -type f -regex '.*\.\(cpp\|hpp\|c\|h\)$' -print |
  while IFS= read -r f; do git check-ignore -q "$f" || printf '%s\n' "$f"; done |
  xargs -r -I {} clang-format -i {}
