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


def send_command(cmd):
    if ser and ser.is_open:
        try:
            ser.write((cmd + "\n").encode())
            append_text(f">>> {cmd}\n")
        except Exception as e:
            append_text(f"Write error: {e}\n")
    else:
        append_text("Not connected.\n")

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
