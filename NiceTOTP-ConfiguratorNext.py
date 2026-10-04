import base64
import json
import os
import queue
import re
import shutil
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Protocol

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None
from PySide6.QtCore import QObject, Property, QProcess, QSettings, QStorageInfo, QThread, QTimer, QUrl, Signal, Slot
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine


class DeviceTransport(Protocol):
    @property
    def connected(self): ...

    def find_device(self): ...

    def connect(self, device): ...

    def send(self, command): ...

    def read_lines(self): ...

    def close(self): ...


class UsbSerialTransport:
    """USB serial adapter."""

    def __init__(self):
        self._serial = None
        self._buffer = bytearray()
        self._device_id = None

    def find_device(self):
        if list_ports is None:
            return None
        ports = list(list_ports.comports())
        identified = []
        usb_serial = []

        for port in ports:
            identity = " ".join(
                str(value or "")
                for value in (port.product, port.manufacturer, port.description)
            ).lower()
            if "nicetotp" in identity or "icantmakethings" in identity:
                identified.append(port.device)
                continue

            device = port.device.lower()
            hardware_id = str(port.hwid or "").lower()
            is_usb_serial = (
                port.vid is not None
                or "usb" in hardware_id
                or device.startswith(("/dev/ttyacm", "/dev/cu.usb", "/dev/tty.usb"))
            )
            if is_usb_serial:
                usb_serial.append(port.device)

        if len(identified) == 1:
            return identified[0]
        if not identified and len(usb_serial) == 1:
            return usb_serial[0]
        return None

    @property
    def connected(self):
        return self._serial is not None and self._serial.is_open

    @property
    def device_id(self):
        return self._device_id

    def connect(self, port):
        if serial is None:
            raise OSError("USB serial transport is unavailable")
        self.close()
        self._serial = serial.Serial(port, baudrate=115200, timeout=0, write_timeout=0.5)
        self._device_id = port
        self._buffer.clear()

    def send(self, command):
        if not self.connected:
            return False
        self._serial.write((command + "\n").encode("utf-8"))
        return True

    def read_lines(self):
        if not self.connected:
            return []
        waiting = self._serial.in_waiting
        if waiting:
            self._buffer.extend(self._serial.read(waiting))
        lines = []
        while b"\n" in self._buffer:
            line, _, rest = self._buffer.partition(b"\n")
            self._buffer = bytearray(rest)
            decoded = line.decode("utf-8", errors="replace").strip("\r \t")
            if decoded:
                lines.append(decoded)
        return lines

    def close(self):
        if self._serial is not None:
            try:
                self._serial.close()
            except OSError:
                pass
        self._serial = None
        self._device_id = None
        self._buffer.clear()


class Uf2UpdateWorker(QThread):
    completed = Signal(bool, str)

    def __init__(self, firmware_path):
        super().__init__()
        self._firmware_path = Path(firmware_path)

    def run(self):
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline and not self.isInterruptionRequested():
            for volume in QStorageInfo.mountedVolumes():
                if not volume.isValid() or not volume.isReady():
                    continue
                mount = Path(volume.rootPath())
                if not (mount / "INFO_UF2.TXT").is_file():
                    continue
                try:
                    shutil.copy2(self._firmware_path, mount / self._firmware_path.name)
                    self.completed.emit(True, f"Firmware copied to {mount}.")
                    return
                except OSError as error:
                    self.completed.emit(False, f"Could not copy firmware: {error}")
                    return
            time.sleep(0.5)
        message = "UF2 update cancelled." if self.isInterruptionRequested() else "UF2 boot drive was not detected within 60 seconds."
        self.completed.emit(False, message)


class ReleaseListWorker(QThread):
    completed = Signal("QVariantList", str)

    def run(self):
        url = "https://api.github.com/repos/ICantMakeThings/NiceTOTP/releases?per_page=100"
        assets = []
        try:
            while url:
                request = urllib.request.Request(
                    url,
                    headers={"Accept": "application/vnd.github+json", "User-Agent": "NiceTOTP-Configurator-Next"},
                )
                with urllib.request.urlopen(request, timeout=20) as response:
                    releases = json.loads(response.read().decode("utf-8"))
                    next_link = response.headers.get("Link", "")
                for release in releases:
                    for asset in release.get("assets", []):
                        if asset.get("name", "").lower().endswith(".uf2"):
                            assets.append({
                                "name": f"{release.get('tag_name', 'Release')} · {asset['name']}",
                                "tag": release.get("tag_name", ""),
                                "asset": asset["name"],
                                "downloadUrl": asset["browser_download_url"],
                            })
                match = re.search(r'<([^>]+)>;\s*rel="next"', next_link)
                url = match.group(1) if match else ""
            self.completed.emit(assets, "" if assets else "No UF2 assets found in GitHub releases.")
        except Exception as error:
            self.completed.emit([], f"Could not load GitHub releases: {error}")


class FirmwareDownloadWorker(QThread):
    completed = Signal(bool, str)

    def __init__(self, url, filename, parent=None):
        super().__init__(parent)
        self._url = url
        self._filename = filename

    def run(self):
        temporary_path = ""
        try:
            request = urllib.request.Request(
                self._url,
                headers={"User-Agent": "NiceTOTP-Configurator-Next"},
            )
            with urllib.request.urlopen(request, timeout=30) as response:
                with tempfile.NamedTemporaryFile(
                    prefix="nicetotp-", suffix=".uf2", delete=False
                ) as firmware_file:
                    temporary_path = firmware_file.name
                    shutil.copyfileobj(response, firmware_file)
            self.completed.emit(True, temporary_path)
        except Exception as error:
            if temporary_path:
                Path(temporary_path).unlink(missing_ok=True)
            self.completed.emit(False, f"Firmware download failed: {error}")


class QrScanWorker(QThread):
    completed = Signal("QVariantList", str)

    def __init__(self, source="camera", parent=None):
        super().__init__(parent)
        self._source = source

    def run(self):
        capture = None
        stop_reader = threading.Event()
        try:
            import cv2
            from pyzbar.pyzbar import decode

            if self._source == "camera":
                if sys.platform.startswith("linux"):
                    camera_sources = [
                        os.fspath(path)
                        for path in sorted(Path("/dev").glob("video[0-9]*"))
                        if path.exists()
                    ]
                else:
                    camera_sources = list(range(4))
                if not camera_sources:
                    raise RuntimeError("No camera device was found.")

                for source in camera_sources:
                    if isinstance(source, str) and sys.platform.startswith("linux"):
                        candidate = cv2.VideoCapture(source, cv2.CAP_V4L2)
                    else:
                        candidate = cv2.VideoCapture(source)
                    if candidate.isOpened():
                        capture = candidate
                        break
                    candidate.release()
                if capture is None:
                    raise RuntimeError("Could not open camera, Check camera permissions.")

                frames = queue.Queue(maxsize=1)

                def read_frames():
                    while not stop_reader.is_set():
                        success, frame = capture.read()
                        frame = frame if success and frame is not None else None
                        while not stop_reader.is_set():
                            try:
                                frames.put(frame, timeout=0.1)
                                break
                            except queue.Full:
                                try:
                                    frames.get_nowait()
                                except queue.Empty:
                                    pass
                        if frame is None:
                            return

                threading.Thread(target=read_frames, daemon=True).start()
                try:
                    frame = frames.get(timeout=5)
                except queue.Empty:
                    raise RuntimeError("Camera opened but no frames... Start the camera stream?")
                if frame is None:
                    raise RuntimeError("The camera stopped, Check the connection.")

                result = []
                cancelled = False
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline and not self.isInterruptionRequested():
                    cv2.imshow("Scan authenticator QR code · press Q to cancel", frame)
                    if cv2.waitKey(1) & 0xFF == ord("q"):
                        cancelled = True
                        self.requestInterruption()
                        break
                    decoded = decode(frame)
                    if decoded:
                        result = [entry.data.decode("utf-8") for entry in decoded]
                        break
                    try:
                        frame = frames.get(timeout=0.2)
                    except queue.Empty:
                        continue
                    if frame is None:
                        raise RuntimeError("The camera stopped, Check the connection.")
                if cancelled or self.isInterruptionRequested():
                    raise RuntimeError("QR scan cancelled.")
                if not result:
                    raise RuntimeError("No QR code scanned before time-out.")
                self.completed.emit(result, "")
                return

            image_path = QUrl(self._source).toLocalFile() or self._source
            image = cv2.imread(image_path)
            if image is None:
                raise ValueError("Could not open image.")
            decoded = decode(image)
            if not decoded:
                raise ValueError("No QR code found in image.")
            self.completed.emit([entry.data.decode("utf-8") for entry in decoded], "")
        except Exception as error:
            self.completed.emit([], str(error))
        finally:
            stop_reader.set()
            if capture is not None:
                capture.release()
                try:
                    import cv2

                    cv2.destroyAllWindows()
                except Exception:
                    pass


class DeviceController(QObject):
    stateChanged = Signal()
    accountsChanged = Signal()
    qrImportCompleted = Signal("QVariantList", str)
    notificationRequested = Signal(str)
    settingsChanged = Signal()
    updateChanged = Signal()
    releasesChanged = Signal()

    def __init__(self, transport: DeviceTransport | None = None):
        super().__init__()
        self._transport = transport or UsbSerialTransport()
        self._settings = QSettings()
        self._connected = False
        self._unlocked = False
        self._status = "Searching for NiceTOTP"
        self._message = ""
        self._accounts = []
        saved_theme = self._settings.value("app/theme", "gagan-green")
        self._app_theme = {
            "dark": "gagan-green",
            "forest": "gagan-green",
            "light": "classic-gray",
        }.get(saved_theme, saved_theme)
        if self._app_theme not in {
            "gagan-green", "classic-gray", "pollution-gray",
            "island-blue", "blood-red", "problem-purple",
        }:
            self._app_theme = "gagan-green"
        self._calibration = "Not loaded"
        self._offset = 0
        self._baseline = "Not set"
        self._calibration_locked = False
        self._last_scan = 0.0
        self._last_heartbeat = 0.0
        self._last_response = 0.0
        self._last_code_tick = 0.0
        self._reading_accounts = False
        self._expected_accounts = 0
        self._reading_codes = False
        self._expected_codes = 0
        self._add_queue = []
        self._copied_code = ""
        self._clipboard_expiry = 0.0
        self._updating = False
        self._update_busy = False
        self._update_status = ""
        self._uf2_worker = None
        self._download_worker = None
        self._release_worker = None
        self._qr_worker = None
        self._release_assets = []
        self._temporary_firmware = ""
        self._nrfutil_process = None
        self._nrfutil_venv_python = ""
        self._pending_nrfutil_package = ""
        self._pending_nrfutil_device = ""

        self._timer = QTimer(self)
        self._timer.setInterval(100)
        self._timer.timeout.connect(self._poll)
        self._timer.start()
        QTimer.singleShot(800, self.loadFirmwareReleases)

    @Property(bool, notify=stateChanged)
    def connected(self):
        return self._connected

    @Property(bool, notify=stateChanged)
    def unlocked(self):
        return self._unlocked

    @Property(str, notify=stateChanged)
    def status(self):
        return self._status

    @Property(str, notify=stateChanged)
    def message(self):
        return self._message

    @Property("QVariantList", notify=accountsChanged)
    def accounts(self):
        return self._accounts

    @Property(str, notify=stateChanged)
    def calibration(self):
        return self._calibration

    @Property(int, notify=stateChanged)
    def calibrationOffset(self):
        return self._offset

    @Property(str, notify=stateChanged)
    def calibrationBaseline(self):
        return self._baseline

    @Property(bool, notify=stateChanged)
    def calibrationLocked(self):
        return self._calibration_locked

    @Property(str, notify=settingsChanged)
    def appTheme(self):
        return self._app_theme

    @Property(str, notify=stateChanged)
    def connectionTarget(self):
        return getattr(self._transport, "device_id", None) or ""

    @Property(bool, notify=updateChanged)
    def updateBusy(self):
        return self._update_busy

    @Property(str, notify=updateChanged)
    def updateStatus(self):
        return self._update_status

    @Property("QVariantList", notify=releasesChanged)
    def releaseAssets(self):
        return self._release_assets

    @Property(bool, notify=releasesChanged)
    def releaseListLoading(self):
        return self._release_worker is not None and self._release_worker.isRunning()

    def _set_state(self, *, connected=None, unlocked=None, status=None, message=None):
        changed = False
        for key, value in (
            ("_connected", connected),
            ("_unlocked", unlocked),
            ("_status", status),
            ("_message", message),
        ):
            if value is not None and getattr(self, key) != value:
                setattr(self, key, value)
                changed = True
        if changed:
            self.stateChanged.emit()

    def _publish_accounts(self, accounts):
        self._accounts = accounts
        self.accountsChanged.emit()

    def _show_message(self, message):
        self._set_state(message=message)
        self.notificationRequested.emit(message)

    def _set_update(self, *, busy=None, status=None):
        changed = False
        if busy is not None and self._update_busy != busy:
            self._update_busy = busy
            changed = True
        if status is not None and self._update_status != status:
            self._update_status = status
            changed = True
        if changed:
            self.updateChanged.emit()

    def _clear_copied_code(self):
        if not self._copied_code:
            return
        clipboard = QGuiApplication.clipboard()
        if clipboard.text() == self._copied_code:
            clipboard.clear()
        self._copied_code = ""
        self._clipboard_expiry = 0.0

    def _clear_account_codes(self):
        changed = False
        for account in self._accounts:
            if account.get("code") or account.get("remaining"):
                changed = True
                account["code"] = ""
            account["remaining"] = 0
        if changed:
            self.accountsChanged.emit()

    def _request_codes(self):
        if not self._unlocked or not self._accounts:
            return
        self._reading_codes = True
        self._expected_codes = len(self._accounts)
        self._clear_account_codes()
        self._send("codes")

    @staticmethod
    def _valid_secret(secret):
        if not secret or len(secret) > 127 or not re.fullmatch(r"[A-Z2-7]+=*", secret):
            return False
        padded = secret + "=" * (-len(secret) % 8)
        try:
            return bool(base64.b32decode(padded, casefold=True))
        except (ValueError, base64.binascii.Error):
            return False

    @Slot(str)
    def showMessage(self, message):
        self._show_message(message)

    def _send(self, command):
        if not self._transport.connected:
            self._show_message("Connect and unlock your NiceTOTP first.")
            return False
        try:
            return self._transport.send(command)
        except OSError as error:
            self._disconnect(f"USB connection lost: {error}")
            return False

    def _connect(self, port):
        try:
            self._transport.connect(port)
        except OSError as error:
            self._set_state(status=f"Could not open device: {error}")
            return

        now = time.monotonic()
        self._set_state(connected=True, unlocked=False, status="Checking device lock")
        self._last_heartbeat = now
        self._last_response = now
        self._send("configurator")
        self._request_accounts()

    def _disconnect(self, message="Waiting for NiceTOTP"):
        self._clear_copied_code()
        self._transport.close()
        self._reading_accounts = False
        self._reading_codes = False
        self._add_queue.clear()
        self._publish_accounts([])
        self._set_state(connected=False, unlocked=False, status=message)

    def _poll(self):
        now = time.monotonic()
        if self._copied_code and now >= self._clipboard_expiry:
            self._clear_copied_code()
        if self._updating:
            return
        if not self._transport.connected:
            if now - self._last_scan >= 1.0:
                self._last_scan = now
                device = self._transport.find_device()
                if device:
                    self._connect(device)
            return

        try:
            lines = self._transport.read_lines()
        except OSError as error:
            self._disconnect(f"USB connection lost: {error}")
            return

        for line in lines:
            self._handle_line(line)

        if now - self._last_heartbeat >= 2.0:
            if self._send("configurator"):
                self._last_heartbeat = now

        if self._unlocked and self._accounts and now - self._last_code_tick >= 1.0:
            self._last_code_tick = now
            expired = False
            for account in self._accounts:
                remaining = max(0, int(account.get("remaining", 0)) - 1)
                account["remaining"] = remaining
                if remaining == 0 and account.get("code"):
                    account["code"] = ""
                    expired = True
            self.accountsChanged.emit()
            if expired:
                self._request_codes()

        if not self._unlocked and now - self._last_response > 3.0:
            self._set_state(status="Device locked or waiting for unlock")

    def _request_accounts(self):
        self._reading_accounts = True
        self._expected_accounts = 0
        self._reading_codes = False
        self._send("list")

    def _handle_line(self, line):
        self._last_response = time.monotonic()
        if line == "Device is locked. Unlock first.":
            self._clear_copied_code()
            self._reading_accounts = False
            self._reading_codes = False
            self._publish_accounts([])
            self._set_state(unlocked=False, status="Device locked · unlock on device")
            return

        header = re.match(r"^Keys \((\d+)\):$", line)
        if header:
            self._reading_accounts = True
            self._expected_accounts = int(header.group(1))
            self._publish_accounts([
                {"id": index, "username": "", "code": "", "remaining": 0}
                for index in range(self._expected_accounts)
            ])
            self._set_state(connected=True, unlocked=True, status="NiceTOTP unlocked")
            if self._expected_accounts == 0:
                self._reading_accounts = False
            return

        account = re.match(r"^(\d+):\s(.*)$", line)
        if self._reading_accounts and account:
            account_id = int(account.group(1))
            if 0 <= account_id < len(self._accounts):
                self._accounts[account_id]["username"] = account.group(2)
            self.accountsChanged.emit()
            if len([item for item in self._accounts if item.get("username")]) >= self._expected_accounts:
                self._reading_accounts = False
                QTimer.singleShot(100, self._request_codes)
            return

        codes_header = re.match(r"^TOTP codes \((\d+)\):$", line)
        if codes_header:
            self._reading_accounts = False
            self._reading_codes = True
            self._expected_codes = int(codes_header.group(1))
            if self._expected_codes == 0:
                self._reading_codes = False
            return

        code_line = re.match(r"^(\d+): ([0-9]{6}|------):(\d+)$", line)
        if self._reading_codes and code_line:
            account_id = int(code_line.group(1))
            if 0 <= account_id < len(self._accounts):
                account = self._accounts[account_id]
                account["code"] = code_line.group(2) if code_line.group(2) != "------" else ""
                account["remaining"] = int(code_line.group(3))
                self.accountsChanged.emit()
            if sum(bool(item.get("remaining")) for item in self._accounts) >= self._expected_codes:
                self._reading_codes = False
            return

        if self._reading_accounts:
            self._reading_accounts = False
        if self._reading_codes:
            self._reading_codes = False

        offset = re.match(r"^RTC aging offset: (-?\d+)$", line)
        if offset:
            self._offset = int(offset.group(1))
        baseline = re.match(r"^Calibration baseline: (.*)$", line)
        if baseline:
            self._baseline = baseline.group(1)
        locked = re.match(r"^Calibration locked: (YES|NO)$", line)
        if locked:
            self._calibration_locked = locked.group(1) == "YES"
            self._calibration = (
                f"Aging offset {self._offset:+d} · baseline {self._baseline} · "
                f"automatic calibration {'locked' if self._calibration_locked else 'enabled'}"
            )
            self.stateChanged.emit()

        if line.startswith("Added key: "):
            self._finish_queued_add(True, line.removeprefix("Added key: "))
            QTimer.singleShot(250, self._request_accounts)
        elif line in (
            "Max keys reached",
            "Duplicate key, not added.",
            "Failed to save encrypted vault",
            "Invalid add command format",
        ) or line.startswith("Error:"):
            if self._add_queue:
                self._finish_queued_add(False, line)
            else:
                self._show_message(line)
        elif line == "Key deleted":
            self._show_message("Account deleted.")
            QTimer.singleShot(250, self._request_accounts)
        elif line == "Keys cleared":
            self._show_message("All accounts cleared.")
            QTimer.singleShot(250, self._request_accounts)
        elif line.startswith(("RTC ", "Manual RTC ", "Calibration ")) and not line.startswith((
            "Calibration baseline:",
            "Calibration locked:",
        )):
            self._show_message(line.strip())

    def _finish_queued_add(self, success, result):
        if self._add_queue:
            username, _ = self._add_queue.pop(0)
            self._show_message(
                f"Added {username}." if success else f"Could not add {username}: {result}"
            )
            if self._add_queue:
                QTimer.singleShot(300, self._send_next_add)

    def _send_next_add(self):
        if self._add_queue and self._unlocked:
            username, secret = self._add_queue[0]
            self._send(f"add {username} {secret}")

    @Slot(str, str, result=bool)
    def addAccount(self, username, secret):
        if not self._unlocked:
            self._show_message("Unlock your NiceTOTP on the device before adding accounts.")
            return False
        username = username.strip()
        secret = "".join(secret.split()).upper()
        if not username or not secret:
            self._show_message("Account name and Base32 secret are required.")
            return False
        if len(username.encode("utf-8")) > 31 or "\n" in username or "\r" in username:
            self._show_message("Account name must be 31 bytes or fewer.")
            return False
        if not self._valid_secret(secret):
            self._show_message("Enter a valid Base32 secret.")
            return False
        self.addAccounts([{"username": username, "secret": secret}])
        return True

    @Slot("QVariantList")
    def addAccounts(self, entries):
        if not self._unlocked:
            self._show_message("Unlock your NiceTOTP on the device before adding accounts.")
            return
        accepted = []
        for entry in entries:
            username = str(entry.get("username", "")).strip()
            secret = "".join(str(entry.get("secret", "")).split()).upper()
            if (
                username
                and secret
                and len(username.encode("utf-8")) <= 31
                and "\n" not in username
                and "\r" not in username
                and self._valid_secret(secret)
            ):
                accepted.append((username, secret))
        if not accepted:
            self._show_message("No valid accounts selected.")
            return
        self._add_queue.extend(accepted)
        self._show_message(f"Adding {len(accepted)} account(s)…")
        if len(self._add_queue) == len(accepted):
            self._send_next_add()

    @Slot(int)
    def deleteAccount(self, account_id):
        if self._unlocked:
            self._send(f"del {account_id}")

    @Slot()
    def clearAccounts(self):
        if self._unlocked:
            self._send("clear")

    @Slot()
    def refreshAccounts(self):
        if self._unlocked:
            self._request_accounts()

    @Slot()
    def refreshCodes(self):
        if self._unlocked:
            self._request_codes()

    @Slot(int)
    def copyCode(self, account_id):
        if not self._unlocked or not 0 <= account_id < len(self._accounts):
            return
        account = self._accounts[account_id]
        code = account.get("code", "")
        remaining = int(account.get("remaining", 0))
        if not re.fullmatch(r"\d{6}", code) or remaining <= 0:
            self._show_message("That code has expired. Waiting for the next code.")
            return
        QGuiApplication.clipboard().setText(code)
        self._copied_code = code
        self._clipboard_expiry = time.monotonic() + remaining
        self.notificationRequested.emit("Copied to clipboard")

    @Slot(str)
    def setAppTheme(self, theme):
        if theme not in {
            "gagan-green", "classic-gray", "pollution-gray",
            "island-blue", "blood-red", "problem-purple",
        }:
            return
        self._app_theme = theme
        self._settings.setValue("app/theme", theme)
        self.settingsChanged.emit()

    @Slot()
    def loadFirmwareReleases(self):
        if self._release_worker is not None and self._release_worker.isRunning():
            return
        self._release_worker = ReleaseListWorker(self)
        self._release_worker.completed.connect(self._on_releases_loaded)
        self._release_worker.start()
        self.releasesChanged.emit()

    @Slot("QVariantList", str)
    def _on_releases_loaded(self, assets, error):
        self._release_assets = assets
        worker = self._release_worker
        self._release_worker = None
        if error:
            self._set_update(status=error)
        self.releasesChanged.emit()
        if worker is not None:
            worker.wait()

    @Slot(int)
    def downloadFirmwareAsset(self, index):
        if not self._unlocked:
            self._set_update(status="Unlock the device before starting a UF2 update.")
            return
        if not 0 <= index < len(self._release_assets):
            self._set_update(status="Select a release firmware asset first.")
            return
        asset = self._release_assets[index]
        self._set_update(busy=True, status=f"Downloading {asset['asset']}…")
        self._download_worker = FirmwareDownloadWorker(asset["downloadUrl"], asset["asset"], self)
        self._download_worker.completed.connect(self._on_firmware_downloaded)
        self._download_worker.start()

    @Slot(bool, str)
    def _on_firmware_downloaded(self, success, result):
        worker = self._download_worker
        self._download_worker = None
        if worker is not None:
            worker.wait()
        if not success:
            self._set_update(busy=False, status=result)
            return
        self._temporary_firmware = result
        self._set_update(busy=False, status="Download complete. Entering UF2 mode…")
        self.startUf2Update(QUrl.fromLocalFile(result).toString())

    @Slot(str)
    def selectLocalUf2(self, source):
        self.startUf2Update(source)

    @Slot()
    def setDeviceTime(self):
        if self._unlocked and self._send(f"setunixtime {int(time.time())}"):
            self._show_message("Device time updated.")

    @Slot(str)
    def setManualCalibration(self, value):
        try:
            offset = int(value)
        except ValueError:
            self._show_message("Calibration offset must be an integer from -128 to 127.")
            return
        if -128 <= offset <= 127:
            if self._send(f"manualcalibration {offset}"):
                QTimer.singleShot(250, self.getCalibration)
        else:
            self._show_message("Calibration offset must be between -128 and 127.")

    @Slot()
    def getCalibration(self):
        if self._unlocked:
            self._send("getcalibration")

    @Slot()
    def clearCalibration(self):
        if self._unlocked:
            if self._send("clearcalibration"):
                QTimer.singleShot(250, self.getCalibration)

    @Slot(bool)
    def setCalibrationLocked(self, locked):
        if self._unlocked:
            if self._send("lockcalibration" if locked else "unlockcalibration"):
                QTimer.singleShot(250, self.getCalibration)

    @Slot()
    def setupPasscode(self):
        if self._unlocked and self._send("pinsetup"):
            self._show_message("Follow the prompts on your NiceTOTP to change its passcode.")

    @Slot()
    def lockDevice(self):
        if self._unlocked:
            self._send("lock")
            self._clear_copied_code()
            self._publish_accounts([])
            self._set_state(unlocked=False, status="Locking device")

    @Slot()
    def factoryReset(self):
        if self._unlocked:
            self._send("factoryreset")
            self._clear_copied_code()
            self._publish_accounts([])
            self._set_state(unlocked=False)
            self._show_message("Factory reset requested. The device will reboot.")

    @Slot(str)
    def startUf2Update(self, source):
        firmware_path = QUrl(source).toLocalFile() or source
        if not firmware_path.lower().endswith(".uf2") or not Path(firmware_path).is_file():
            self._set_update(status="Choose a valid UF2 firmware file first.")
            return
        if not self._unlocked:
            self._set_update(status="Unlock the device before starting a UF2 update.")
            return
        if not self._send("dfu"):
            return

        self._updating = True
        self._clear_copied_code()
        self._publish_accounts([])
        self._set_state(unlocked=False, status="Entering UF2 bootloader")
        self._set_update(busy=True, status="Waiting for the UF2 boot drive…")
        self._uf2_worker = Uf2UpdateWorker(firmware_path)
        self._uf2_worker.completed.connect(self._on_uf2_update_completed)
        self._uf2_worker.start()

    @Slot(bool, str)
    def _on_uf2_update_completed(self, success, message):
        if self._uf2_worker is not None:
            self._uf2_worker.wait()
        self._uf2_worker = None
        self._updating = False
        self._set_update(busy=False, status=message)
        self._set_state(connected=False, unlocked=False, status="Searching for NiceTOTP")
        if self._temporary_firmware:
            Path(self._temporary_firmware).unlink(missing_ok=True)
            self._temporary_firmware = ""

    def _find_nrfutil(self):
        found = shutil.which("adafruit-nrfutil")
        if found:
            return found
        executable = "adafruit-nrfutil.exe" if sys.platform.startswith("win") else "adafruit-nrfutil"
        scripts = "Scripts" if sys.platform.startswith("win") else "bin"
        candidates = [
            Path.home() / "nrfutil-venv" / scripts / executable,
            Path.home() / ".local" / "share" / "NiceTOTP" / "nrfutil-venv" / scripts / executable,
            Path.home() / ".local" / "bin" / executable,
        ]
        if sys.platform.startswith("win"):
            user_scripts = Path(os.environ.get("APPDATA", Path.home())) / "Python" / f"Python{sys.version_info.major}{sys.version_info.minor}" / "Scripts" / executable
            candidates.append(user_scripts)
        return next((os.fspath(candidate) for candidate in candidates if candidate.is_file()), "")

    def _start_nrfutil_install(self):
        python = shutil.which("python3") or shutil.which("python")
        if not python:
            self._updating = False
            self._set_update(busy=False, status="Python is required to install adafruit-nrfutil automatically.")
            return
        venv_dir = Path.home() / ".local" / "share" / "NiceTOTP" / "nrfutil-venv"
        scripts_dir = "Scripts" if sys.platform.startswith("win") else "bin"
        python_name = "python.exe" if sys.platform.startswith("win") else "python"
        self._nrfutil_venv_python = os.fspath(venv_dir / scripts_dir / python_name)
        if Path(self._nrfutil_venv_python).is_file():
            self._install_nrfutil_package()
            return
        process = self._new_nrfutil_process(self._nrfutil_venv_created)
        process.setProgram(python)
        process.setArguments(["-m", "venv", os.fspath(venv_dir)])
        self._set_update(busy=True, status="Preparing an isolated nrfutil environment…")
        process.start()

    def _new_nrfutil_process(self, finished_handler):
        process = QProcess(self)
        process.readyReadStandardOutput.connect(self._read_nrfutil_output)
        process.readyReadStandardError.connect(self._read_nrfutil_output)
        process.finished.connect(finished_handler)
        process.errorOccurred.connect(self._nrfutil_error)
        self._nrfutil_process = process
        return process

    @Slot(int, QProcess.ExitStatus)
    def _nrfutil_venv_created(self, exit_code, _exit_status):
        if exit_code != 0:
            self._nrfutil_install_failed("Could not create the nrfutil virtual environment.")
            return
        self._install_nrfutil_package()

    def _install_nrfutil_package(self):
        process = self._new_nrfutil_process(self._nrfutil_install_finished)
        process.setProgram(self._nrfutil_venv_python)
        process.setArguments(["-m", "pip", "install", "adafruit-nrfutil"])
        self._set_update(busy=True, status="Downloading adafruit-nrfutil into the app environment…")
        process.start()

    @staticmethod
    def _nrfutil_compatibility_script():
        return (
            "import importlib, pathlib; "
            "signing=importlib.import_module('nordicsemi.dfu.signing'); "
            "path=pathlib.Path(signing.__file__); text=path.read_text(); "
            "text=text.replace(\"sk_hex = \\\"\\\".join(c.encode('hex') for c in self.sk.to_string())\", \"sk_hex = binascii.hexlify(self.sk.to_string()).decode('ascii')\"); "
            "path.write_text(text); "
            "manifest=importlib.import_module('nordicsemi.dfu.manifest'); "
            "path=pathlib.Path(manifest.__file__); text=path.read_text(); "
            "text=text.replace('init_packet_ecds = binascii.hexlify(field)', \"init_packet_ecds = binascii.hexlify(field).decode('ascii')\"); "
            "path.write_text(text)"
        )

    @Slot(int, QProcess.ExitStatus)
    def _nrfutil_install_finished(self, exit_code, _exit_status):
        if exit_code != 0:
            self._nrfutil_install_failed("Could not install adafruit-nrfutil. Check network access.")
            return
        process = self._new_nrfutil_process(self._nrfutil_compatibility_patched)
        process.setProgram(self._nrfutil_venv_python)
        process.setArguments(["-c", self._nrfutil_compatibility_script()])
        self._set_update(busy=True, status="Preparing nrfutil for this Python version…")
        process.start()

    @Slot(int, QProcess.ExitStatus)
    def _nrfutil_compatibility_patched(self, exit_code, _exit_status):
        if exit_code != 0:
            self._nrfutil_install_failed("Could not prepare the nrfutil Python compatibility patch.")
            return
        executable = self._find_nrfutil()
        if not executable:
            venv_dir = Path(self._nrfutil_venv_python).parent
            command = "adafruit-nrfutil.exe" if sys.platform.startswith("win") else "adafruit-nrfutil"
            candidate = venv_dir / command
            executable = os.fspath(candidate) if candidate.is_file() else ""
        if not executable:
            self._nrfutil_install_failed("Installation finished, but the nrfutil command was not found.")
            return
        self._nrfutil_process = None
        self._launch_nrfutil(executable, self._pending_nrfutil_package, self._pending_nrfutil_device)

    def _nrfutil_install_failed(self, message):
        self._updating = False
        self._nrfutil_process = None
        self._set_update(busy=False, status=message)

    def _launch_nrfutil(self, executable, package, device_id):
        process = QProcess(self)
        process.setProgram(executable)
        process.setArguments([
            "dfu", "serial", "--package", package,
            "--port", device_id, "--baudrate", "115200"
        ])
        process.readyReadStandardOutput.connect(self._read_nrfutil_output)
        process.readyReadStandardError.connect(self._read_nrfutil_output)
        process.errorOccurred.connect(self._nrfutil_error)
        process.finished.connect(self._nrfutil_finished)
        self._nrfutil_process = process
        self._set_update(busy=True, status="Starting serial DFU…")
        process.start()

    @Slot(str)
    def startNrfutilUpdate(self, source):
        package = QUrl(source).toLocalFile() or source
        if not package.lower().endswith(".zip") or not Path(package).is_file():
            self._set_update(status="Choose a signed DFU .zip package first.")
            return
        device_id = getattr(self._transport, "device_id", None)
        if not device_id:
            self._set_update(status="Connect to the device's serial DFU port first.")
            return
        self._updating = True
        self._clear_copied_code()
        self._publish_accounts([])
        self._transport.close()
        self._set_state(connected=False, unlocked=False, status="Serial DFU update in progress")
        self._pending_nrfutil_package = package
        self._pending_nrfutil_device = device_id
        executable = self._find_nrfutil()
        if executable:
            self._launch_nrfutil(executable, package, device_id)
        else:
            self._start_nrfutil_install()

    @Slot()
    def _read_nrfutil_output(self):
        process = self._nrfutil_process
        if process is None:
            return
        output = bytes(process.readAllStandardOutput() + process.readAllStandardError())
        lines = [line.strip() for line in output.decode(errors="replace").splitlines() if line.strip()]
        if lines:
            self._set_update(status=lines[-1][-240:])

    @Slot(object)
    def _nrfutil_error(self, error):
        self._updating = False
        self._set_update(busy=False, status=f"Could not start adafruit-nrfutil: {error}")
        self._nrfutil_process = None

    @Slot(int, QProcess.ExitStatus)
    def _nrfutil_finished(self, exit_code, _exit_status):
        self._updating = False
        message = "nrfutil update completed." if exit_code == 0 else f"nrfutil exited with code {exit_code}."
        self._set_update(busy=False, status=message)
        self._set_state(status="Searching for NiceTOTP")
        self._nrfutil_process = None

    def _start_qr_scan(self, source):
        if self._qr_worker is not None and self._qr_worker.isRunning():
            return
        self._qr_worker = QrScanWorker(source, self)
        self._qr_worker.completed.connect(self._on_qr_scan_completed)
        self._qr_worker.start()

    @Slot(str)
    def scanQrImage(self, source):
        self._start_qr_scan(source)

    @Slot()
    def scanQrCamera(self):
        self._start_qr_scan("camera")

    @Slot()
    def cancelQrScan(self):
        if self._qr_worker is not None and self._qr_worker.isRunning():
            self._qr_worker.requestInterruption()

    @Slot("QVariantList", str)
    def _on_qr_scan_completed(self, values, error):
        worker = self._qr_worker
        self._qr_worker = None
        if worker is not None:
            worker.wait()
        if error:
            self.qrImportCompleted.emit([], error)
            return
        try:
            entries = []
            for value in values:
                entries.extend(self._decode_qr_data(value))
            if not entries:
                raise ValueError("The QR code contains no TOTP accounts.")
            self.qrImportCompleted.emit(entries, "")
        except Exception as decode_error:
            self.qrImportCompleted.emit([], str(decode_error))

    @staticmethod
    def _decode_qr_data(value):
        if value.startswith("otpauth://"):
            parsed = urllib.parse.urlparse(value)
            if parsed.netloc.lower() != "totp":
                return []
            label = urllib.parse.unquote(parsed.path.lstrip("/"))
            params = urllib.parse.parse_qs(parsed.query)
            secret = params.get("secret", [""])[0].replace(" ", "").upper()
            issuer = params.get("issuer", [""])[0]
            if issuer and ":" not in label:
                label = f"{issuer}:{label}"
            return [{"username": label, "secret": secret}] if label and secret else []

        if not value.startswith("otpauth-migration://"):
            return []
        import migration_payload_pb2

        params = urllib.parse.parse_qs(urllib.parse.urlparse(value).query)
        encoded = params.get("data", [""])[0].replace(" ", "+")
        if not encoded:
            raise ValueError("Google Authenticator QR has no migration data.")
        encoded += "=" * (-len(encoded) % 4)
        payload = migration_payload_pb2.MigrationPayload()
        payload.ParseFromString(base64.urlsafe_b64decode(encoded))
        accounts = []
        for otp in payload.otp_parameters:
            if otp.type != migration_payload_pb2.MigrationPayload.OTP_TOTP:
                continue
            secret = base64.b32encode(otp.secret).decode("ascii").rstrip("=")
            name = otp.name or otp.issuer
            if otp.issuer and otp.name and otp.issuer not in otp.name:
                name = f"{otp.issuer}:{otp.name}"
            if name and secret:
                accounts.append({"username": name, "secret": secret})
        return accounts

    @Slot()
    def shutdown(self):
        self._timer.stop()
        self._clear_copied_code()
        for worker in (self._uf2_worker, self._download_worker, self._release_worker, self._qr_worker):
            if worker is not None and worker.isRunning():
                worker.requestInterruption()
                worker.wait(1500)
        if self._nrfutil_process is not None and self._nrfutil_process.state() != QProcess.NotRunning:
            self._nrfutil_process.terminate()
            if not self._nrfutil_process.waitForFinished(1500):
                self._nrfutil_process.kill()
        self._transport.close()


def main():
    app = QGuiApplication(sys.argv)
    app.setOrganizationName("ICantMakeThings")
    app.setOrganizationDomain("icmt.cc")
    app.setApplicationName("NiceTOTP Configurator")
    engine = QQmlApplicationEngine()
    controller = DeviceController()
    engine.rootContext().setContextProperty("device", controller)
    qml_dir = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
    engine.load(QUrl.fromLocalFile(os.fspath(qml_dir / "NiceTOTP-ConfiguratorNext.qml")))
    if not engine.rootObjects():
        controller.shutdown()
        return 1
    app.aboutToQuit.connect(controller.shutdown)
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
