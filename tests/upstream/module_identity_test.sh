#!/usr/bin/env bash
set -euo pipefail

file="src/module.json"

rg -q '"id"\s*:\s*"mrdrums"' "$file"
rg -q '"name"\s*:\s*"MrDrums"' "$file"
rg -q '"abbrev"\s*:\s*"MRD"' "$file"
