#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_DIR"
python3 Scrypts/uf2conv.py .pio/build/nicenano/firmware.hex --family 0xADA52840 --convert --output NiceTOTP-FromSource.uf2
