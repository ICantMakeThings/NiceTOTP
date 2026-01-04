# Import all needed stuff
import customtkinter as ctk
import serial
import serial.tools.list_ports
import threading
import time
import sys
from tkinter import messagebox
import os
import shutil
import requests
import subprocess
import platform
import tempfile
from tkinter import filedialog
import tkinter as tk
import migration_payload_pb2
import cv2
from pyzbar.pyzbar import decode
import base64
import urllib.parse
import hmac
import hashlib
import struct

# Set theme
ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("green")

ser = None
connected = False
read_thread = None
stop_read = threading.Event()
custom_fw_path = None

# Pull data from latest releases + file browser
def fetch_latest_firmware_assets():
    api_url = "https://api.github.com/repos/ICantMakeThings/NiceTOTP/releases/latest"
    try:
        response = requests.get(api_url, timeout=10)
        response.raise_for_status()
        data = response.json()
        assets = data.get("assets", [])
        fw_dict = {}
        for asset in assets:
            name = asset["name"]
            if name.endswith(".uf2"):
                fw_dict[name] = asset["browser_download_url"]
        return fw_dict
    except Exception as e:
        append_text(f"[Error fetching firmware list] {e}\n")
        return {}

# Start base window, size & name
app = ctk.CTk()
app.title("NiceTOTP Configurator")
app.geometry("1010x490")

# Overdone tooltip overlay
class ToolTip:
    def __init__(self, widget, text):
        self.widget = widget
        self.text = text
        self.tipwindow = None
        self.id = None
        self.x = self.y = 0
        widget.bind("<Enter>", self.show_tip)
        widget.bind("<Leave>", self.hide_tip)

    def show_tip(self, event=None):
        if self.tipwindow or not self.text:
            return
        x = self.widget.winfo_rootx() + 20
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 5
        self.tipwindow = tw = tk.Toplevel(self.widget)
        tw.wm_overrideredirect(True)
        tw.wm_geometry(f"+{x}+{y}")
        label = tk.Label(tw, text=self.text, justify=tk.LEFT,
                         background="#FFFFFF", relief=tk.SOLID, borderwidth=1,
                         font=("tahoma", "8", "normal"))
        label.pack(ipadx=5, ipady=2)

    def hide_tip(self, event=None):
        tw = self.tipwindow
        self.tipwindow = None
        if tw:
            tw.destroy()


def scan_google_qr_camera():
    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        raise RuntimeError("Could not open camera")

    append_text("Camera opened. Show Google Authenticator QR Code.\n")

    qr_data = None

    while True:
        ret, frame = cap.read()
        if not ret:
            continue

        decoded = decode(frame)
        for d in decoded:
            qr_data = d.data.decode()
            break

        cv2.imshow("Scan Google Authenticator QR Code (press Q to cancel)", frame)

        if qr_data:
            break

        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    cap.release()
    cv2.destroyAllWindows()

    if not qr_data:
        raise RuntimeError("No QR code scanned")

    if not qr_data.startswith("otpauth-migration://"):
        raise RuntimeError("Not a Google Authenticator export QR")

    return qr_data

def decode_google_migration_url(url):
    parsed = urllib.parse.urlparse(url)
    params = urllib.parse.parse_qs(parsed.query)

    data_b64 = params.get("data", [None])[0]
    if not data_b64:
        raise ValueError("No data parameter")

    payload_bytes = base64.b64decode(data_b64)

    payload = migration_payload_pb2.MigrationPayload()
    payload.ParseFromString(payload_bytes)

    accounts = []

    for otp in payload.otp_parameters:
        if otp.type != migration_payload_pb2.MigrationPayload.OTP_TOTP:
            continue

        secret_b32 = base64.b32encode(otp.secret).decode().replace("=", "")

        accounts.append({
            "issuer": otp.issuer or "",
            "name": otp.name or "",
            "secret": secret_b32,
            "digits": otp.digits
        })

    return accounts

def generate_totp(secret_b32, interval=30, digits=6):
    try:
        missing_padding = len(secret_b32) % 8
        if missing_padding:
            secret_b32 += "=" * (8 - missing_padding)

        key = base64.b32decode(secret_b32.upper(), casefold=True)
    except Exception:
        return "------"

    counter = int(time.time()) // interval
    msg = struct.pack(">Q", counter)

    h = hmac.new(key, msg, hashlib.sha1).digest()
    o = h[-1] & 0x0F
    code = (struct.unpack(">I", h[o:o+4])[0] & 0x7fffffff) % (10 ** digits)

    return str(code).zfill(digits)


# Account Import UI
def confirm_and_import(accounts):
    win = ctk.CTkToplevel(app)
    win.title("Confirm Google Authenticator Import")
    win.geometry("950x450")
    win.grab_set()

    canvas = tk.Canvas(win)
    scroll = ctk.CTkScrollbar(win, orientation="vertical", command=canvas.yview)
    frame = ctk.CTkFrame(canvas)

    canvas.configure(yscrollcommand=scroll.set)
    canvas.pack(side="left", fill="both", expand=True)
    scroll.pack(side="right", fill="y")
    canvas.create_window((0, 0), window=frame, anchor="nw")

    headers = ["", "Issuer", "Account", "Secret", "Code", "Show", "Copy"]
    for col, h in enumerate(headers):
        ctk.CTkLabel(frame, text=h, font=("Arial", 10, "bold")).grid(row=0, column=col, padx=5, pady=3)

    rows = []

    for i, acc in enumerate(accounts, start=1):
        enabled = tk.BooleanVar(value=True)

        issuer = ctk.CTkEntry(frame, width=150)
        name = ctk.CTkEntry(frame, width=150)
        secret_entry = ctk.CTkEntry(frame, width=220, show="*")
        code_label = ctk.CTkLabel(frame, width=80, font=("Courier", 16))

        issuer.insert(0, acc["issuer"])
        name.insert(0, acc["name"])
        secret_entry.insert(0, acc["secret"])

        ctk.CTkCheckBox(frame, text="", variable=enabled).grid(row=i, column=0)
        issuer.grid(row=i, column=1, padx=5, pady=3)
        name.grid(row=i, column=2, padx=5, pady=3)
        secret_entry.grid(row=i, column=3, padx=5, pady=3)
        code_label.grid(row=i, column=4, padx=5, pady=3)

        def toggle_secret(e=secret_entry):
            if e.cget("show") == "":
                e.configure(show="*")
            else:
                e.configure(show="")
        eye_btn = ctk.CTkButton(frame, text="👁", width=25, command=toggle_secret)
        eye_btn.grid(row=i, column=5, padx=5)

        def copy_code(c_label=code_label):
            app.clipboard_clear()
            app.clipboard_append(c_label.cget("text"))
            append_text(f"Copied code {c_label.cget('text')} to clipboard.\n")
        copy_btn = ctk.CTkButton(frame, text="📋", width=25, command=copy_code)
        copy_btn.grid(row=i, column=6, padx=5)

        rows.append((enabled, issuer, name, secret_entry, code_label))

    def update_codes():
        for _, _, _, secret_entry, code_label in rows:
            secret = secret_entry.get().strip()
            if secret:
                code_label.configure(text=generate_totp(secret))
            else:
                code_label.configure(text="------")
        if win.winfo_exists():
            win.after(1000, update_codes)

    update_codes()

    def do_import():
        import_data = []
        for enabled, issuer, name, secret_entry, _ in rows:
            if not enabled.get():
                continue
            user = issuer.get().strip() or name.get().strip()
            if issuer.get().strip() and name.get().strip():
                user = f"{issuer.get().strip()}:{name.get().strip()}"
            secret = secret_entry.get().strip()
            if user and secret:
                import_data.append((user, secret))
        win.destroy()

        def worker():
            for user, secret in import_data:
                send_command(f"add {user} {secret}")
                time.sleep(0.5)
            append_text("Google Authenticator import complete.\n")
        threading.Thread(target=worker, daemon=True).start()

    ctk.CTkButton(win, text="Import Selected", command=do_import).pack(pady=10)


# Looks for the COM port
def list_serial_ports():
    ports = serial.tools.list_ports.comports()
    filtered = []
# Filter out all ports except USB
    for port in ports:
        if sys.platform.startswith("linux"):
            if "ttyACM" in port.device:
                filtered.append(port.device)
        else:
            filtered.append(port.device)

    return filtered

# Refresh the dropdown menu 
def refresh_ports():
    ports = list_serial_ports()
    if ports:
        port_option.configure(values=ports)
        port_option.set(ports[0])
    else:
        port_option.configure(values=["No ports found"])
        port_option.set("No ports found")


# Definitions for commands
def get_unix_time():
    return int(time.time())

def on_set_time():
    ts = get_unix_time()
    send_command(f"setunixtime {ts}")

def on_add_user():
    u = entry_user.get().strip()
    s = entry_secret.get().strip()
    if not u or not s:
        append_text("Username and secret required.\n")
        return
    send_command(f"add {u} {s}")

def on_list():
    append_text("Listing users...\n")
    send_command("list")

def on_delete():
    idx = entry_delete.get().strip()
    if not idx.isdigit():
        append_text("Enter numeric ID to delete.\n")
        return
    send_command(f"del {idx}")

def on_factory_reset():
    if messagebox.askyesno("Confirm Factory Reset", "Are you SUREE you want to factory reset?\nThis CANNOT be undone!"):
        send_command("factoryreset")

def connect_to_port():
    global ser, connected, read_thread, stop_read

    port = port_option.get()
    if not port or "No ports" in port:
        append_text("No valid port selected.\n")
        return

    if ser and ser.is_open:
        stop_read.set()
        try:
            ser.close()
        except Exception:
            pass
        ser = None
        connected = False

    stop_read = threading.Event()

    try:
        ser = serial.Serial(port, 115200, timeout=1)
        connected = True
        status_label.configure(text=f"Connected to {port}", text_color="green")
        append_text(f"Connected to {port}\n")

        read_thread = threading.Thread(target=read_serial, daemon=True)
        read_thread.start()

    except serial.SerialException as e:
        append_text(f"Failed to connect: {e}\n")
        status_label.configure(text="Connection failed", text_color="red")

# Send commands via serial,
def send_command(cmd):
    global ser, connected
    if ser and ser.is_open:
        try:
            ser.write((cmd + "\n").encode())
            append_text(f">>> {cmd}\n")
        except (serial.SerialException, OSError) as e:
            append_text(f"[Error] Serial write failed: {e}\n")
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            connected = False
            status_label.configure(text="Disconnected", text_color="red")
    else:
        append_text("Not connected.\n")


# Show Serial in that box that shows serial
def read_serial():
    global ser, stop_read, connected
    while not stop_read.is_set():
        try:
            if ser and ser.in_waiting:
                line = ser.readline().decode(errors="ignore").rstrip('\r\n \t')
                if line:
                    append_text(line + '\n')
            else:
                time.sleep(0.01)
        except (OSError, serial.SerialException) as e:
            append_text(f"Did you unplug your NiceTOTP? It Disconnected.\n")
            try:
                if ser:
                    ser.close()
            except Exception:
                pass
            ser = None
            connected = False
            app.after(0, lambda: status_label.configure(text="Not connected", text_color="red"))
            break

# Let the code add MORE!! (more lines of serial that is)
def append_text(text):
    console.configure(state="normal")
    console.insert("end", text)
    console.see("end")
    console.update_idletasks()
    console.configure(state="disabled")

# The thing that asks for the custom .U2F
def on_tracker_change(choice):
    global custom_fw_path
    if choice == "Custom…":
        path = filedialog.askopenfilename(title="Select firmware (.uf2)", filetypes=[("UF2 files", "*.uf2")])
        if path:
            custom_fw_path = path
            send_button.configure(text=f"Flash: {os.path.basename(path)}")
        else:
            tracker_select.set(tracker_names[0])
    else:
        custom_fw_path = None
        send_button.configure(text="Flash Firmware")


# Top UI | Yk the serial buttons
top_frame = ctk.CTkFrame(app)
top_frame.pack(pady=5, padx=10, fill="x")

initial_ports = list_serial_ports()
if not initial_ports:
    initial_ports = ["No ports found"]

port_option = ctk.CTkOptionMenu(top_frame, values=initial_ports)
port_option.set(initial_ports[0])
port_option.pack(side="left", padx=5)
ToolTip(port_option, "Select the port for your device")

btn_refresh = ctk.CTkButton(top_frame, text="↻", width=10, command=refresh_ports)
btn_refresh.pack(side="left", padx=5)
ToolTip(btn_refresh, "Refresh serial port")

btn_connect = ctk.CTkButton(top_frame, text="Connect", command=connect_to_port)
btn_connect.pack(side="left", padx=5)
ToolTip(btn_connect, "Connect to the selected serial port")


firmware_urls = {"Custom (User provided .uf2)": None}

# Fill the dropdown menu with latest releases
def populate_firmware_menu():
    global firmware_urls
    auto_fw = fetch_latest_firmware_assets()
    if auto_fw:
        firmware_urls = {**auto_fw, "Custom (User provided .uf2)": None}
        firmware_choice.configure(values=list(firmware_urls.keys()))
        firmware_choice.set("Select Firmware")
    else:
        firmware_urls = {"Custom (User provided .uf2)": None}
        firmware_choice.configure(values=["Custom (User provided .uf2)"])
        firmware_choice.set("Custom (User provided .uf2)")

firmware_choice = ctk.CTkOptionMenu(top_frame, values=["Loading..."])
firmware_choice.set("Loading...")
firmware_choice.pack(side="left", padx=5)
ToolTip(firmware_choice, "Select the tracker to update firmware")

app.after(100, populate_firmware_menu)

def on_import_google_camera():
    # Disable the button immediately
    btn_google_import.configure(state="disabled")

    def worker():
        qr_url = None
        accounts = None
        error = None

        try:
            qr_url = scan_google_qr_camera()
            accounts = decode_google_migration_url(qr_url)
        except Exception as e:
            error = e

        def finalize():
            if error:
                messagebox.showerror("Import Error", str(error))
            elif accounts:
                confirm_and_import(accounts)
            btn_google_import.configure(state="normal")

        app.after(0, finalize)

    threading.Thread(target=worker, daemon=True).start()





# Download the firmware once user selected and pressed the Firmware button,
# and also the actual logic for flashing (Resets, puts into DFU, waits for drive to appear, moves the .U2F to the drive)
def download_firmware():
    selection = firmware_choice.get()

    if selection == "Select Firmware":
        append_text("Please select a firmware option.\n")
        return

    if selection == "Custom (User provided .uf2)":
        file_path = filedialog.askopenfilename(filetypes=[("UF2 files", "*.uf2")])
        if not file_path:
            append_text("No custom firmware selected.\n")
            return
        append_text(f"Selected custom firmware: {file_path}\n")
        local_path = file_path
    else:
        firmware_url = firmware_urls.get(selection)
        if not firmware_url:
            append_text("No firmware URL for selected firmware.\n")
            return

        local_path = os.path.join(tempfile.gettempdir(), os.path.basename(firmware_url))

        try:
            append_text(f"Downloading firmware from {firmware_url}...\n")
            response = requests.get(firmware_url, stream=True, timeout=20)
            response.raise_for_status()
            with open(local_path, 'wb') as f:
                shutil.copyfileobj(response.raw, f)
            append_text(f"Firmware downloaded to: {local_path}\n")
        except Exception as e:
            append_text(f"[Error] Firmware download failed: {e}\n")
            return

    append_text("Entering bootloader mode...\n")
    send_command("dfu")
    append_text("Waiting up to 5 seconds for UF2 device to appear. If you have issues, please post an issue https://github.com/ICantMakeThings/NiceTOTP \n")
    time.sleep(5)

    mount_point = None
    system = platform.system()
    try:
        candidate_paths = []

        if system == "Windows":
            import win32api
            drives = win32api.GetLogicalDriveStrings().split('\000')[:-1]
            candidate_paths = drives

        elif system == "Darwin":
            candidate_paths = [os.path.join("/Volumes", d) for d in os.listdir("/Volumes")]

        elif system == "Linux":
            media_root = "/media"
            for root, dirs, _ in os.walk(media_root):
                for d in dirs:
                    candidate_paths.append(os.path.join(root, d))

        for path in candidate_paths:
            try:
                if os.path.isfile(os.path.join(path, "INFO_UF2.TXT")):
                    mount_point = path
                    break
            except Exception:
                continue

        if mount_point and os.path.isdir(mount_point):
            dest = os.path.join(mount_point, os.path.basename(local_path))
            append_text(f"Copying firmware to {dest}...\n")
            shutil.copy(local_path, dest)
            append_text(f"DONE: Firmware successfully flashed to {mount_point}\n")
        else:
            append_text("ERROR: Could not find NICENANO or UF2 boot device. Is the device in DFU/bootloader mode?\n")

    except Exception as e:
        append_text(f"[Error flashing] {e}\n")
        append_text("NOTE! On windows [WinError 433] doesn't mean it failed!\n")

# Buttons!
btn_download_fw = ctk.CTkButton(top_frame, text="⬇ Firmware", width=80, command=download_firmware)
btn_download_fw.pack(side="left", padx=5)
ToolTip(btn_download_fw, "Upgrade your firmware!")

status_label = ctk.CTkLabel(top_frame, text="Not connected", text_color="red")
status_label.pack(side="left", padx=10)

# Tabs
tabs = ctk.CTkTabview(app)
tabs.pack(padx=10, pady=10, fill="x")
tab_users = tabs.add("Users")
tab_manage = tabs.add("Manage")

# Users tab
frame_u = tab_users
ctk.CTkLabel(frame_u, text="Username:").grid(row=0, column=0, padx=5, pady=5, sticky="e")
entry_user = ctk.CTkEntry(frame_u)
entry_user.grid(row=0, column=1, padx=5, pady=5)

ctk.CTkLabel(frame_u, text="Base32 Secret:").grid(row=1, column=0, padx=5, pady=5, sticky="e")
entry_secret = ctk.CTkEntry(frame_u, show="*")
entry_secret.grid(row=1, column=1, padx=5, pady=5)

btn_add = ctk.CTkButton(frame_u, text="Add", command=on_add_user)
btn_add.grid(row=2, column=0, columnspan=2, pady=10)

btn_google_import = ctk.CTkButton(
    frame_u,
    text="Import from Google (Camera)",
    command=on_import_google_camera
)
btn_google_import.grid(row=4, column=0, columnspan=2, pady=10)

# Manage tab
frame_m = tab_manage

btn_list = ctk.CTkButton(frame_m, text="List Users", command=on_list)
btn_list.grid(row=0, column=0, padx=5, pady=5)

ctk.CTkLabel(frame_m, text="ID to delete:").grid(row=1, column=0, padx=5, pady=5, sticky="e")
entry_delete = ctk.CTkEntry(frame_m)
entry_delete.grid(row=1, column=1, padx=5, pady=5)

btn_delete = ctk.CTkButton(frame_m, text="Delete", command=on_delete)
btn_delete.grid(row=2, column=1, columnspan=2, pady=10)

btn_factory = ctk.CTkButton(frame_m, text="Factory Reset", fg_color="red", command=on_factory_reset)
btn_factory.grid(row=3, column=0, columnspan=2, pady=20)

# Set time button
btn_set_time = ctk.CTkButton(frame_m, text="Set Time", command=on_set_time)
btn_set_time.grid(row=0, column=11, columnspan=2, pady=10)

# CLI
console = ctk.CTkTextbox(app, height=200)
console.pack(padx=10, pady=(0, 10), fill="both", expand=True)
console.configure(state="disabled")

app.after(100, populate_firmware_menu)
app.mainloop()
