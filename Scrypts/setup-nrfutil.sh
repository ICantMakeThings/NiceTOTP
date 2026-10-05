#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

VENV_DIR="${HOME}/nrfutil-venv"
if [[ ! -x "$VENV_DIR/bin/python" ]]; then
  python3 -m venv "$VENV_DIR"
fi

source "$VENV_DIR/bin/activate"
python -m pip install --upgrade pip
python -m pip install adafruit-nrfutil

SIGNING_MODULE="$(python -c 'import nordicsemi.dfu.signing as signing; print(signing.__file__)')"
if grep -q "c.encode('hex')" "$SIGNING_MODULE"; then
  sed -i "s/sk_hex = \"\".join(c.encode('hex') for c in self.sk.to_string())/sk_hex = binascii.hexlify(self.sk.to_string()).decode('ascii')/" "$SIGNING_MODULE"
fi
MANIFEST_MODULE="$(python -c 'import nordicsemi.dfu.manifest as manifest; print(manifest.__file__)')"
if grep -q "init_packet_ecds = binascii.hexlify(field)$" "$MANIFEST_MODULE"; then
  sed -i "s/binascii.hexlify(field)$/binascii.hexlify(field).decode('ascii')/" "$MANIFEST_MODULE"
fi

if [[ ! -f private.pem ]]; then
  adafruit-nrfutil keys --gen-key private.pem
  chmod 600 private.pem
  echo "Generated private.pem. Keep it private and make a secure backup."
else
  echo "Keeping existing private.pem."
fi

echo "Setup complete. Activate: source ~/nrfutil-venv/bin/activate"