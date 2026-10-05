Import("env")
import os
import subprocess
from pathlib import Path

project_dir = Path(env.subst("$PROJECT_DIR"))
build_dir = Path(env.subst("$BUILD_DIR"))
firmware_hex = build_dir / f"{env.subst('$PROGNAME')}.hex"
signed_package = project_dir / "firmwareSIGNED.zip"
private_key = Path(os.environ.get("DFU_PRIVATE_KEY", project_dir / "private.pem"))
signer = Path.home() / "nrfutil-venv" / "bin" / "adafruit-nrfutil"


def create_signed_package(source, target, env):
	if not signer.is_file():
		raise RuntimeError(f"{signer} is missing; run ./Scrypts/setup-nrfutil.sh")
	if not private_key.is_file():
		raise RuntimeError(f"{private_key} is missing; run ./Scrypts/setup-nrfutil.sh")

	subprocess.run(
		[
			str(signer),
			"dfu",
			"genpkg",
			"--dev-type",
			"0x0052",
			"--application",
			str(firmware_hex),
			"--key-file",
			str(private_key),
			str(signed_package),
		],
		check=True,
	)


def upload_signed_package(source, target, env):
	upload_port = env.GetProjectOption("upload_port", None)
	if not upload_port:
		upload_port = os.environ.get("PLATFORMIO_UPLOAD_PORT")
	if not upload_port:
		raise RuntimeError(
			"Set upload_port in platformio.ini or pass --upload-port /dev/ttyACM0. "
			"Enter the bootloader's USB CDC DFU mode before uploading."
		)
	if not signed_package.is_file():
		create_signed_package(source, target, env)

	subprocess.run(
		[
			str(signer),
			"dfu",
			"serial",
			"--package",
			str(signed_package),
			"--port",
			str(upload_port),
			"--baud-rate",
			"115200",
		],
		check=True,
	)


env.AddPostAction(str(firmware_hex), create_signed_package)
env.Replace(UPLOADCMD=upload_signed_package)