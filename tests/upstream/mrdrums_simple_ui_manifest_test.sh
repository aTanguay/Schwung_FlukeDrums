#!/usr/bin/env bash
set -euo pipefail

file="src/module.json"

if rg -q '"ui"\s*:' "$file"; then
  echo "FAIL: module.json should not declare ui.js for simple hierarchy UI" >&2
  exit 1
fi

rg -q '"ui_pad_page"\s*:\s*"main"' "$file"
rg -q '"ui_hierarchy"\s*:' "$file"
rg -q '"pad_settings"' "$file"
rg -q '"visible_if"\s*:\s*\{\s*"param"\s*:\s*"ui_pad_page"' "$file"

echo "PASS: simple hierarchy UI is declared in module.json"
