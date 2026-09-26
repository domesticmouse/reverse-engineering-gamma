# Reverse Engineering the this.is.NOISE Gamma

This project aims to reverse-engineer the hardware pinout, interfaces, and peripherals of the **Gamma Mini Synth** by **this.is.NOISE inc.** in order to create an open-source Board Support Package (BSP) and support custom firmware development.

The Gamma synth is powered by an embedded **Electro-Smith Daisy Seed 2 DFM** (ARM Cortex-M7 @ 480 MHz, STM32H750, PCM3060 stereo audio codec).

---

## Hardware Overview

The Gamma features a compact, performance-oriented interface:
* **Core:** Electro-Smith Daisy Seed 2 DFM
* **Display:** 1.3" OLED display (suspected I2C / SSD1306 or SH1106)
* **Keys:** 14 low-profile tactile keys (7 chord keys on the left, 7 note keys on the right)
* **Thumbsticks:** 2 analog joysticks (Left X/Y, Right X/Y for modulation and chord alterations)
* **Knobs:** 4 potentiometers (Chords Volume/Filter, Notes Volume/Filter)
* **Encoder:** 1 rotary encoder with integrated push button (key/scale selection; 4-wire harness: shared GND + Phase A, Phase B, Push Switch)
* **Audio:** 3.5mm stereo headphone output (driven by on-board PCM3060 codec via SAI)
* **MIDI:** 3.5mm TRS MIDI output (UART TX)
* **USB:** USB-C connector for power, DFU firmware flashing, and USB serial/MIDI communication

---

## Reverse-Engineering Roadmap

For the detailed step-by-step plan covering software setup, non-destructive flash dumping, diagnostic firmware probing, pin mapping, and BSP creation, see:
* **[PLAN.md](PLAN.md)**: Reverse-Engineering and Pinout Mapping Plan

---

## Firmware Flashing

The Gamma runs the **Electro-Smith Daisy Bootloader** in internal flash (`0x08000000`). Custom and diagnostic firmware binaries are flashed to external QSPI flash (`0x90040000`) and executed out of internal SRAM.

### Prerequisites & Build Target

* **Target Architecture:** Binaries **must** be compiled with `APP_TYPE = BOOT_SRAM` in your `Makefile`:
  ```makefile
  APP_TYPE = BOOT_SRAM
  ```
  *(The bootloader copies the binary from QSPI flash to SRAM at `0x24000000`. Do not use `BOOT_QSPI` or `BOOT_NONE`; the Daisy Bootloader will reject the binary and show an SOS LED blink error).*
* **QSPI Flash Address:** `0x90040000`

### Method 1: Automated Polling (Recommended)

Because the Daisy Bootloader's DFU window lasts ~2.5 seconds after a reset, using the automated Python helper avoids timing races:

```bash
# Flash the latest binary built under firmware/:
python3 .agents/skills/gamma-firmware-flash/scripts/flash_firmware.py

# Or specify a custom binary:
python3 .agents/skills/gamma-firmware-flash/scripts/flash_firmware.py firmware/phase1_i2c_scan/build/phase1_i2c_scan.bin
```

1. Run the script (it verifies that the vector table targets SRAM `0x24000000` and begins polling `dfu-util`).
2. Press the **RESET** button on the Daisy Seed (or power-cycle the unit).
3. The script automatically catches the DFU device (`0483:df11`), flashes to `0x90040000:leave`, and reboots into the new firmware.

### Method 2: Manual Terminal Flashing (`dfu-util`)

1. Build your firmware:
   ```bash
   make clean && make
   ```
2. Put the Daisy Seed into bootloader mode:
   - Hold **BOOT**, press and release **RESET**, then release **BOOT** (or tap **BOOT** during startup to extend bootloader mode).
3. Check that the bootloader is detected:
   ```bash
   dfu-util -l
   ```
   *(Expected: `Found DFU: [0483:df11] ... Product: "Daisy Bootloader" (Electrosmith)`)*
4. Flash the binary to QSPI flash:
   ```bash
   dfu-util -a 0 -s 0x90040000:leave -D build/<firmware_name>.bin
   ```
   > [!NOTE]
   > A `dfu-util: Error during download get_status` / exit code `74` message upon `:leave` is normal; the MCU resets immediately upon flashing completion before the final USB query can complete.

### Serial Output Verification

For diagnostic firmware with USB serial logging enabled:
```bash
tio /dev/cu.usbmodem*
# or
screen /dev/cu.usbmodem* 115200
```

---

## Restoring Factory Firmware

If custom firmware halts, crashes, or you want to return the synthesizer to stock factory operation, you can restore the official firmware image (`gamma-v2.0.3.bin`).

### Method 1: Automated CLI Restore (Recommended)

Run the automated restore script:
```bash
python3 .agents/skills/gamma-firmware-restore/scripts/restore_firmware.py
```

1. The script verifies or downloads the official `gamma-v2.0.3.bin` into `backups/` and begins polling for DFU.
2. Tap the **RESET** button on the Daisy Seed (or hold **BOOT**, tap **RESET**, release **BOOT**).
3. The script flashes the factory binary to `0x90040000:leave`.
4. The Gamma reboots into the official synthesizer firmware.

### Method 2: Manual Terminal Flash (`dfu-util`)

1. Ensure the factory binary is present:
   ```bash
   # Already cached in repository:
   ls -lh backups/gamma-v2.0.3.bin
   ```
   *(If needed, download directly: `curl -L -o backups/gamma-v2.0.3.bin https://gammaupdatetool.netlify.app/data/gamma-v2.0.3.bin`)*
2. Put the device into bootloader mode (Hold **BOOT**, tap **RESET**, release **BOOT**).
3. Flash the stock binary:
   ```bash
   dfu-util -a 0 -s 0x90040000:leave -D backups/gamma-v2.0.3.bin
   ```

### Method 3: Browser Web Recovery

If you prefer using a web browser:
1. Navigate to the official [Gamma Update Tool](https://gammaupdatetool.netlify.app/).
2. Expand **Troubleshooting** $\rightarrow$ **"Can't Enter Boot"**.
3. Click **"Connect & Install"**.
4. Power cycle or tap **RESET** on the Gamma.
5. Within the ~2-second boot window, select **"Daisy Bootloader"** and click **Connect**.
6. The web updater will reflash the factory firmware.

### Post-Restore Verification

Verify that the Gamma enumerates as a USB MIDI/audio device:
```bash
# macOS USB enumeration check:
ioreg -p IOUSB -l -w0 | grep -A 10 "Gamma"
```
* The device will enumerate with `kUSBProductString` = `"Gamma"` (`0483:5740`).
* The 1.3" OLED will display the active Gamma synthesizer interface.

---

## Skills & Runbooks

Operational runbooks and automated tooling are maintained as workspace skills:
* **[Gamma Firmware Flashing](.agents/skills/gamma-firmware-flash/SKILL.md)**: Detailed runbook and automated polling script (`flash_firmware.py`).
* **[Gamma Firmware Restore](.agents/skills/gamma-firmware-restore/SKILL.md)**: Detailed runbook and automated restore script (`restore_firmware.py`).

---

## Project Status

- [x] **Toolchain & Software:** ARM toolchain (`arm-none-eabi-gcc 15.3.1`), `dfu-util 0.11`, `make`, and serial monitors (`tio`, `minicom`, `screen`) confirmed working.
- [x] **Submodules:** `libDaisy` and `DaisySP` linked as Git submodules and compiled.
- [x] **Phase 0 (Baseline Verification & Safety Net):** Daisy Bootloader identified over USB DFU (`0483:df11`, Electrosmith Daisy Bootloader). Factory firmware binaries (`gamma-v2.0.3.bin` and `gamma1_1.bin`) downloaded and verified. Automated restore skill created and tested.
- [ ] **Phase 1 (Diagnostic Console & I2C Scan):** Diagnostic firmware developed (`firmware/phase1_i2c_scan`). Target architecture verified as `BOOT_SRAM` (`0x24000000`). Ready for flash via the flashing skill.

---

## Helpful Resources & Links

### Hardware & Manufacturer
* [this.is.NOISE inc. Official Site](https://thisisnoiseinc.com)
* [this.is.NOISE inc. Gamma Product Page](https://thisisnoiseinc.com/pages/gamma)

### Daisy Seed & Embedded Platform
* [Electro-Smith Daisy Seed 2 DFM](https://daisy.audio/pages/daisy-seed2-dfm)
* [Electro-Smith libDaisy GitHub Repository](https://github.com/electro-smith/libDaisy)
* [Electro-Smith DaisySP DSP Library GitHub Repository](https://github.com/electro-smith/DaisySP)
* [Daisy Documentation & Pinout Guides](https://docs.daisy.audio/)

---

## License & Disclaimer

* **License:** Distributed under the Apache 2.0 License. See [LICENSE](LICENSE) for more information.
* **Disclaimer:** This is not an officially supported Google product.