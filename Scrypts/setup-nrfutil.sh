#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_DIR"

VENV_DIR="${HOME}/nrfutil-venv"
if [[ ! -x "$VENV_DIR/bin/python" ]]; then
  if ! python3 -m venv "$VENV_DIR"; then
    echo "Could not create $VENV_DIR. On Debian/Ubuntu install python3-venv and python3-pip, then retry." >&2
    exit 1
  fi
fi

if [[ ! -x "$VENV_DIR/bin/pip" ]]; then
  echo "The virtual environment has no pip. On Debian/Ubuntu install python3-venv and python3-pip, remove the incomplete $VENV_DIR, then retry." >&2
  exit 1
fi

"$VENV_DIR/bin/python" -m pip install --upgrade pip
"$VENV_DIR/bin/python" -m pip install --upgrade adafruit-nrfutil

SIGNING_MODULE="$("$VENV_DIR/bin/python" -c 'import nordicsemi.dfu.signing as signing; print(signing.__file__)')"
if grep -q "c.encode('hex')" "$SIGNING_MODULE"; then
  sed -i "s/sk_hex = \"\".join(c.encode('hex') for c in self.sk.to_string())/sk_hex = binascii.hexlify(self.sk.to_string()).decode('ascii')/" "$SIGNING_MODULE"
fi
MANIFEST_MODULE="$("$VENV_DIR/bin/python" -c 'import nordicsemi.dfu.manifest as manifest; print(manifest.__file__)')"
if grep -q "init_packet_ecds = binascii.hexlify(field)$" "$MANIFEST_MODULE"; then
  sed -i "s/binascii.hexlify(field)$/binascii.hexlify(field).decode('ascii')/" "$MANIFEST_MODULE"
fi

if [[ ! -s private.pem ]]; then
  KEY_TMP="$(mktemp "${PROJECT_DIR}/.private.pem.XXXXXX")"
  trap 'rm -f "$KEY_TMP"' EXIT
  "$VENV_DIR/bin/python" - "$KEY_TMP" <<'PY'
import sys
from ecdsa import NIST256p, SigningKey

with open(sys.argv[1], "wb") as key_file:
    key_file.write(SigningKey.generate(curve=NIST256p).to_pem(format="pkcs8"))
PY
  chmod 600 "$KEY_TMP"
  mv "$KEY_TMP" private.pem
  trap - EXIT
  echo "Generated private.pem. Keep it   P R I V A T E   and make a secure backup."
else
  "$VENV_DIR/bin/python" - private.pem <<'PY'
import sys
from ecdsa import SigningKey

with open(sys.argv[1], "rb") as key_file:
    SigningKey.from_pem(key_file.read())
PY
  chmod 600 private.pem
  echo "Keeping existing valid private.pem."
fi

echo "Setup complete. Signing tools are in $VENV_DIR/bin."
