#!/usr/bin/env python3
"""
flash_firmware.py - Gamma Mini Synth Firmware Flashing Utility
Validates binary target architecture and automates polling for the Daisy Bootloader DFU interface.
"""

import os
import sys
import time
import struct
import argparse
import subprocess

QSPI_START_ADDRESS = "0x90040000"
EXPECTED_SRAM_BASE = 0x24000000
EXPECTED_SRAM_LIMIT = 0x24080000  # 512 KB AXI SRAM

def get_repo_root():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(script_dir, "../../../../"))

def find_default_binary(repo_root):
    # Search firmware subdirectories for the newest .bin
    firmware_dir = os.path.join(repo_root, "firmware")
    candidates = []
    if os.path.exists(firmware_dir):
        for root, _, files in os.walk(firmware_dir):
            for f in files:
                if f.endswith(".bin"):
                    full_path = os.path.join(root, f)
                    candidates.append((os.path.getmtime(full_path), full_path))
    if candidates:
        candidates.sort(reverse=True)
        return candidates[0][1]
    return None

def validate_binary(bin_path):
    if not os.path.exists(bin_path):
        print(f"[!] Error: Binary file not found: {bin_path}")
        sys.exit(1)

    size = os.path.getsize(bin_path)
    if size < 32:
        print(f"[!] Error: Binary file is too small ({size} bytes)")
        sys.exit(1)

    with open(bin_path, "rb") as f:
        header = f.read(32)

    sp, reset_handler = struct.unpack("<II", header[:8])
    print(f"[*] Binary: {os.path.relpath(bin_path)}")
    print(f"[*] Size: {size} bytes")
    print(f"[*] Vector Table - Initial SP: 0x{sp:08X}, Reset Handler: 0x{reset_handler:08X}")

    # Check reset handler target address
    if EXPECTED_SRAM_BASE <= reset_handler < EXPECTED_SRAM_LIMIT:
        print("[+] Target architecture verified: BOOT_SRAM (AXI SRAM entry point)")
    else:
        print("\n[!] WARNING: The Reset Handler is NOT in AXI SRAM (0x24000000)!")
        if reset_handler >= 0x90000000:
            print("[!] It appears to be compiled with APP_TYPE = BOOT_QSPI.")
        elif reset_handler < 0x20000000:
            print("[!] It appears to be compiled for internal flash (BOOT_NONE).")
        print("[!] The Gamma on-board Daisy Bootloader expects BOOT_SRAM.")
        print("[!] Execution may fail or trigger bootloader SOS error.\n")

def poll_and_flash(bin_path, timeout=60):
    print(f"\n[*] Listening for Daisy Bootloader (0483:df11)...")
    print(f"[*] Please reset your Gamma (tap RESET or power-cycle) now (timeout: {timeout}s)...")
    sys.stdout.flush()

    import glob
    for dev in glob.glob("/dev/cu.usbmodem*"):
        try:
            print(f"[*] Detected USB CDC port {dev}, sending software DFU reboot command ('b')...")
            fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            os.write(fd, b'b')
            os.close(fd)
        except Exception:
            pass

    start_time = time.time()
    found = False

    while time.time() - start_time < timeout:
        res = subprocess.run(["dfu-util", "-l"], capture_output=True, text=True)
        if "0483:df11" in res.stdout or "Found DFU" in res.stdout:
            print(f"\n[+] Daisy Bootloader detected! Flashing {os.path.basename(bin_path)} to {QSPI_START_ADDRESS}...")
            sys.stdout.flush()

            cmd = [
                "dfu-util",
                "-a", "0",
                "-s", f"{QSPI_START_ADDRESS}:leave",
                "-D", bin_path
            ]
            flash_proc = subprocess.run(cmd)
            print("\n[*] Flash operation completed. Checking device reboot...")
            found = True
            break
        time.sleep(0.1)

    if not found:
        print(f"\n[!] Timed out waiting for DFU device. Check USB-C connection and try again.")
        sys.exit(1)

    time.sleep(1.5)
    # Check for enumerated USB devices
    usb_check = subprocess.run(["ioreg", "-p", "IOUSB", "-l", "-w0"], capture_output=True, text=True)
    if "Electrosmith" in usb_check.stdout or "Daisy" in usb_check.stdout or "Gamma" in usb_check.stdout:
        print("[+] Device enumerated on USB after reboot.")
    else:
        print("[*] Monitoring device: Check serial terminal or OLED display for diagnostic output.")

def main():
    parser = argparse.ArgumentParser(description="Gamma Mini Synth Firmware Flashing Utility")
    parser.add_argument("binary", nargs="?", help="Path to compiled .bin firmware (defaults to latest build)")
    parser.add_argument("--timeout", type=int, default=60, help="DFU polling timeout in seconds (default: 60)")
    args = parser.parse_args()

    repo_root = get_repo_root()
    bin_path = args.binary

    if not bin_path:
        bin_path = find_default_binary(repo_root)
        if not bin_path:
            print("[!] No binary specified and no .bin build artifacts found in firmware/")
            sys.exit(1)
        print(f"[*] Auto-selected latest build: {os.path.relpath(bin_path)}")

    validate_binary(bin_path)
    poll_and_flash(bin_path, timeout=args.timeout)

if __name__ == "__main__":
    main()
