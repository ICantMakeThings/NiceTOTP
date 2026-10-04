# Build Signed from source

*Note: Thease are Linux specific instructions. On Windows, use WSL 2*

**Note 2: Only follow this if you can keep private key SAFE, It's more recommened to use Pre-Signed FW if you dont fully know what your doing.**

****

This guide builds the signed Bootloader and Firmware from source.

The bootloader is signed. **Keep `private.pem` private and backed up;** do not commit or share it. **Losing it means you cannot update the device.**

## 1. Get the source and tools

Get prerequisites:

Install: Git, Python 3, CMake, Make, the ARM GNU toolchain (`arm-none-eabi-gcc`), PlatformIO Core, Nordic nRF Command Line Tools (`nrfjprog`), and SEGGER J-Link. Add nice!nano support to PlatformIO using [this guide](https://github.com/ICantMakeThings/Nicenano-NRF52-Supermini-PlatformIO-Support).

Clone this repository:

```sh
git clone https://github.com/ICantMakeThings/NiceTOTP.git
```

open its root directory:
```sh
cd NiceTOTP
```

## 2. Create the signing key

Run this **once**:

```sh
./setup-nrfutil.sh
```

This creates `~/nrfutil-venv` and `private.pem` **(WHICH YOU DO NOT SHARE, KEEP SAFE, YOU WILL NEED FOR FUTURE FW UPDATES)** if they do not already exist. The key is local to this checkout by default. Back it up securely, and use the same key for the bootloader and firmware.

## 3. Build and flash the signed bootloader

Build the bootloader:

```sh
./build-signed-bootloader.sh
```

The script clones Adafruit's bootloader source into ignored `build/` and builds it with the public key corresponding to `private.pem`. 

It does not flash the device yet. You need a J-Link debugger like ST-LINK, flash the SoftDevice and bootloader from the repository root:

```sh
cmake --build build/nice_nano-signed-bootloader --target flash-all
```

This target uses `nrfjprog` to program and verify the files. Do not run APPROTECT-enabled firmware until the signed bootloader is verified.

## 4. Build and install signed firmware

For the initial firmware flash, keep `enableApprotect();` commented out, and leave both related lines:
```ini
;extra_scripts = post:approtect.py
;build_flags = -DAPPROTECT_ENABLED=1
```
commented out in `platformio.ini`. 

Build the firmware:

```sh
pio run
```

With the board still connected over SWD, flash the firmware:

```sh
nrfjprog --program .pio/build/nicenano/firmware.hex --verify --sectorerase -f nrf52 --reset
```

## 5. Enable APPROTECT and install signed firmware

After verifying that the firmware BOOTs, enable protection for the final firmware by uncommenting:

1. `enableApprotect();` in `setup()` in `src/main.cpp`
 
2. `extra_scripts = post:approtect.py` and `build_flags = -DAPPROTECT_ENABLED=1` in `platformio.ini`, it will look like:

```ini
extra_scripts = post:approtect.py
build_flags = -DAPPROTECT_ENABLED=1
```

Build and upload the final signed firmware over **USB** DFU:

```sh
pio run
pio run -t upload --upload-port /dev/ttyACM0
```

Replace `/dev/ttyACM0` with the device's serial port. 

The upload script signs the firmware with the local key and uses `adafruit-nrfutil`. APPROTECT takes effect on the first boot of this final firmware; later uploads must use the signed DFU path. The signed bootloader does not support UF2 drag-and-drop.

## 6. Build the configurator

Install Python 3 and the app's dependencies, then package the Qt Quick configurator with PyInstaller:

```sh
python -m pip install -r requirements.txt
pyinstaller --noconfirm NiceTOTP-Configurator.spec
```

The packaged application is written to `dist/`. QR image import uses ZBar; install its shared library before running the app:

```sh
sudo apt-get install libzbar0
```

On macOS, install ZBar with `brew install zbar`. On Windows, use the Python packages above in a Windows Python environment.