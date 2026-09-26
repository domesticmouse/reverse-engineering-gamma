#!/usr/bin/env python3
"""
restore_firmware.py - Gamma Mini Synth Firmware Restore Utility
Automates polling for the Daisy Bootloader DFU interface and flashing the official firmware.
"""

import os
import sys
import time
import urllib.request
import subprocess

OFFICIAL_FIRMWARE_URL = "https://gammaupdatetool.netlify.app/data/gamma-v2.0.3.bin"
QSPI_START_ADDRESS = "0x90040000"

def get_repo_root():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    # script is in .agents/skills/gamma-firmware-restore/scripts/
    return os.path.abspath(os.path.join(script_dir, "../../../../"))

def ensure_firmware_binary(repo_root):
    backup_dir = os.path.join(repo_root, "backups")
    os.makedirs(backup_dir, exist_ok=True)
    bin_path = os.path.join(backup_dir, "gamma-v2.0.3.bin")

    if not os.path.exists(bin_path):
        print(f"[*] Downloading official firmware from {OFFICIAL_FIRMWARE_URL}...")
        try:
            urllib.request.urlretrieve(OFFICIAL_FIRMWARE_URL, bin_path)
            print(f"[+] Download complete: {bin_path} ({os.path.getsize(bin_path)} bytes)")
        except Exception as e:
            print(f"[!] Error downloading firmware: {e}")
            sys.exit(1)
    else:
        print(f"[+] Using cached firmware: {bin_path} ({os.path.getsize(bin_path)} bytes)")

    return bin_path

def wait_and_flash(bin_path, timeout=60):
    print(f"\n[*] Listening for Daisy Bootloader (0483:df11)...")
    print(f"[*] Please reset your Gamma or trigger bootloader mode now (timeout: {timeout}s)...")
    sys.stdout.flush()

    start_time = time.time()
    found = False

    while time.time() - start_time < timeout:
        res = subprocess.run(["dfu-util", "-l"], capture_output=True, text=True)
        if "0483:df11" in res.stdout or "Found DFU" in res.stdout:
            print(f"\n[+] Daisy Bootloader detected! Initiating DFU flash to {QSPI_START_ADDRESS}...")
            sys.stdout.flush()

            cmd = [
                "dfu-util",
                "-a", "0",
                "-s", f"{QSPI_START_ADDRESS}:leave",
                "-D", bin_path
            ]
            flash_proc = subprocess.run(cmd)

            # Note: dfu-util often exits with error status on ':leave' because the STM32 resets instantly
            print("\n[*] Flash operation finished. Checking device reboot...")
            found = True
            break
        time.sleep(0.1)

    if not found:
        print(f"\n[!] Timed out waiting for DFU device. Ensure USB cable is connected and try again.")
        sys.exit(1)

    # Verification: Wait a moment and check if "Gamma" enumerates
    time.sleep(1.5)
    usb_check = subprocess.run(["ioreg", "-p", "IOUSB", "-l", "-w0"], capture_output=True, text=True)
    if "Gamma" in usb_check.stdout:
        print("[+] SUCCESS: Device successfully rebooted and enumerated as 'Gamma' over USB!")
    else:
        print("[*] Flashing completed. Please verify the OLED screen shows the Gamma UI.")

def main():
    repo_root = get_repo_root()
    bin_path = ensure_firmware_binary(repo_root)
    wait_and_flash(bin_path)

if __name__ == "__main__":
    main()
