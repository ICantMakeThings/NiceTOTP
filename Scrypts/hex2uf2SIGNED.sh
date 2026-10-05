#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

FIRMWARE_HEX=".pio/build/nicenano/firmware.hex"
PRIVATE_KEY="${DFU_PRIVATE_KEY:-private.pem}"
SIGNER="${HOME}/nrfutil-venv/bin/adafruit-nrfutil"

if [[ ! -f "$FIRMWARE_HEX" ]]; then
	echo "Missing $FIRMWARE_HEX; build first with: pio run" >&2
	exit 1
fi
if [[ ! -f "$PRIVATE_KEY" ]]; then
	echo "Missing private key: $PRIVATE_KEY; run ./setup-nrfutil.sh" >&2
	exit 1
fi
if [[ ! -x "$SIGNER" ]]; then
	echo "Missing $SIGNER; run ./setup-nrfutil.sh" >&2
	exit 1
fi

python3 uf2/utils/uf2conv.py "$FIRMWARE_HEX" --family 0xADA52840 --convert --output NiceTOTP-FromSource.uf2
"$SIGNER" dfu genpkg --dev-type 0x0052 --application "$FIRMWARE_HEX" --key-file "$PRIVATE_KEY" firmwareSIGNED.zip

echo "Created NiceTOTP-FromSource.uf2 (unsigned UF2) and firmwareSIGNED.zip (signed DFU package)."
