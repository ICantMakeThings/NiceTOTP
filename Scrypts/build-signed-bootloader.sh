#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOTLOADER_DIR="${BOOTLOADER_DIR:-$PROJECT_DIR/build/Adafruit_nRF52_Bootloader}"
BOOTLOADER_BUILD_DIR="${BOOTLOADER_BUILD_DIR:-$PROJECT_DIR/build/nice_nano-signed-bootloader}"
PRIVATE_KEY="${DFU_PRIVATE_KEY:-$PROJECT_DIR/private.pem}"
VENV_DIR="$HOME/nrfutil-venv"

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

for tool in git cmake make arm-none-eabi-gcc python3; do
  require_command "$tool"
done

if [[ "$PRIVATE_KEY" != "$PROJECT_DIR/private.pem" && ! -f "$PRIVATE_KEY" ]]; then
  echo "Signing key not found: $PRIVATE_KEY" >&2
  exit 1
fi

if [[ ! -x "$VENV_DIR/bin/adafruit-nrfutil" || ! -f "$PRIVATE_KEY" ]]; then
  "$PROJECT_DIR/setup-nrfutil.sh"
fi

if [[ ! -f "$PRIVATE_KEY" ]]; then
  echo "Signing key not found: $PRIVATE_KEY" >&2
  exit 1
fi

source "$VENV_DIR/bin/activate"

if [[ -d "$BOOTLOADER_DIR/.git" ]]; then
  echo "Using existing bootloader checkout: $BOOTLOADER_DIR"
elif [[ -e "$BOOTLOADER_DIR" ]]; then
  echo "Bootloader path exists but is not a Git checkout: $BOOTLOADER_DIR" >&2
  exit 1
else
  mkdir -p "$(dirname "$BOOTLOADER_DIR")"
  git clone --recurse-submodules=1 https://github.com/adafruit/Adafruit_nRF52_Bootloader.git "$BOOTLOADER_DIR"
fi

git -C "$BOOTLOADER_DIR" submodule update --init

PUBLIC_KEY="$(python -c '
import sys
from ecdsa import SigningKey

with open(sys.argv[1], "r", encoding="ascii") as key_file:
    signing_key = SigningKey.from_pem(key_file.read())
public_key = signing_key.get_verifying_key().to_string()
for coordinate in (public_key[:32], public_key[32:]):
    print(", ".join(f"0x{byte:02x}" for byte in coordinate))
' "$PRIVATE_KEY")"
QX="${PUBLIC_KEY%%$'\n'*}"
QY="${PUBLIC_KEY#*$'\n'}"
if [[ -z "$QX" || -z "$QY" || "$QX" == "$QY" ]]; then
  echo "Could not derive signing public-key coordinates." >&2
  exit 1
fi

cmake -S "$BOOTLOADER_DIR" -B "$BOOTLOADER_BUILD_DIR" \
  -DBOARD=nice_nano \
  -DSIGNED_FW=ON \
  -DSIGNED_FW_QX="$QX" \
  -DSIGNED_FW_QY="$QY"
cmake --build "$BOOTLOADER_BUILD_DIR" --parallel

echo "Signed bootloader built: $BOOTLOADER_BUILD_DIR"
echo "YOU NEED SWD."