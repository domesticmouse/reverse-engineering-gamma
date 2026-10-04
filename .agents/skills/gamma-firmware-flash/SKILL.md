---
name: gamma-firmware-flash
description: >-
  Build, validate, and flash custom or diagnostic firmware to the this.is.NOISE Gamma Mini Synth
  (Electro-Smith Daisy Seed 2 DFM) via USB DFU using the automated Daisy Bootloader polling workflow.
---

# Gamma Firmware Flashing Runbook

This skill provides instructions and tools for building, validating, and uploading custom and diagnostic firmware to the **this.is.NOISE Gamma Mini Synth** (Electro-Smith Daisy Seed 2 DFM).

---

## Prerequisites & Critical Configuration

### 1. Build Target Requirement: `APP_TYPE = BOOT_SRAM`
The Gamma uses the on-board **Electro-Smith Daisy Bootloader** stored in internal flash (`0x08000000`).
* **SRAM Entry Point:** The bootloader expects user binaries to be compiled for **AXI SRAM** (`0x24000000`). At boot, it copies the program from QSPI flash into SRAM and branches to it.
* **Makefile Setting:**
  ```makefile
  APP_TYPE = BOOT_SRAM
  ```
  *(Do NOT use `BOOT_QSPI` or `BOOT_NONE`; the Daisy Bootloader will reject `BOOT_QSPI` builds and emit an SOS error blink on the LED).*

### 2. Flashing Address
Programs are stored in external QSPI flash starting at:
```text
0x90040000
```

---

## Flashing Workflows

### Method 1: Automated Polling (Recommended)

To avoid racing the bootloader's ~2.5-second DFU window, use the automated helper:

```bash
# Flash the latest build in firmware/ automatically:
python3 .agents/skills/gamma-firmware-flash/scripts/flash_firmware.py

# Or specify an explicit binary:
python3 .agents/skills/gamma-firmware-flash/scripts/flash_firmware.py firmware/phase1_i2c_scan/build/phase1_i2c_scan.bin
```

**Procedure:**
1. The script automatically inspects the binary's vector table to verify it targets `0x24000000` (`BOOT_SRAM`).
2. If an active USB CDC device (`/dev/cu.usbmodem*`) is present, the script automatically sends `'b'` to trigger a software reboot directly into the Daisy Bootloader.
3. If the device was unprogrammed, hung, or running firmware without USB CDC support, tap the **RESET** button on the Daisy Seed (or power-cycle the Gamma).
4. The script detects the DFU interface (`0483:df11`) during the 2-second bootloader window and flashes the binary to `0x90040000:leave`.
5. The device automatically reboots into the new firmware.

---

### Method 2: Manual Terminal Flashing (`dfu-util`)

1. Verify build target in your Makefile (`APP_TYPE = BOOT_SRAM`).
2. Build the project:
   ```bash
   make clean && make
   ```
3. Put the Daisy Seed into bootloader mode:
   - Hold **BOOT**, tap **RESET**, release **BOOT** (or tap **BOOT** during startup to extend the grace period).
4. Verify the DFU interface:
   ```bash
   dfu-util -l
   ```
   *Expected: `Found DFU: [0483:df11] ... Product: "Daisy Bootloader" (Electrosmith)`*
5. Flash the `.bin` to QSPI flash:
   ```bash
   dfu-util -a 0 -s 0x90040000:leave -D build/<firmware_name>.bin
   ```
   > [!NOTE]
   > `dfu-util: Error during download get_status` / exit code 74 upon `:leave` is normal because the MCU reboots immediately upon receiving the leave command.

---

## Post-Flash Verification & Interaction

1. **USB CDC Serial Console (Diagnostics):**
   If the firmware initializes USB CDC on the external port (`hw.usb_handle.Init(UsbHandle::FS_EXTERNAL)`), it enumerates as `Daisy Seed External` (`0483:5740`):
   ```bash
   # Connect using minicom, screen, or tio:
   tio /dev/cu.usbmodem*
   # Or screen:
   screen /dev/cu.usbmodem* 115200
   ```
   > [!CAUTION]
   > Do **NOT** use `daisy::Logger<LOGGER_EXTERNAL>`! Once a terminal connects and closes, `Logger` enters an infinite loop in `TransmitSync()`, hanging the main loop and leaving notes stuck playing forever. Always use the timeout-guarded non-blocking `UsbLog` struct (see `gamma-pinout` skill §8 and `firmware/phase5_audio/main.cpp`).
2. **Error Patterns:**
   - **Rapid 3-blink pattern or SOS pattern:** Bootloader rejected the binary (check that `APP_TYPE = BOOT_SRAM` is set and binary size is under 480 KB).
   - **Static OLED splash screen:** The OLED retains the previous screen until the running firmware explicitly initializes the I2C/SPI display driver.

---

## Safety Net: Factory Firmware Restore

If custom firmware hangs or needs to be removed:
- Follow [`.agents/skills/gamma-firmware-restore/SKILL.md`](../gamma-firmware-restore/SKILL.md)
- Or run:
  ```bash
  python3 .agents/skills/gamma-firmware-restore/scripts/restore_firmware.py
  ```
