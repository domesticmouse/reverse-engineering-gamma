# Reverse Engineering the this.is.NOISE Gamma

This project aims to reverse-engineer the hardware pinout, interfaces, and peripherals of the **Gamma Mini Synth** by **this.is.NOISE inc.** in order to create an open-source Board Support Package (BSP) and support custom firmware development.

The Gamma synth is powered by an embedded **Electro-Smith Daisy Seed 2 DFM** (ARM Cortex-M7 @ 480 MHz, STM32H750, PCM3060 stereo audio codec).

---

## Hardware Overview

The Gamma features a compact, performance-oriented interface:
* **Core:** Electro-Smith Daisy Seed 2 DFM (ARM Cortex-M7 @ 480 MHz, STM32H750IBK6, 128 KB internal flash, 1 MB RAM, 64 MB external QSPI flash)
* **Display:** 1.3" 128x64 monochrome OLED display (Solomon Systech SSD1306 controller via `I2C1` on `seed::D11` / `PB8` [SCL] & `seed::D12` / `PB9` [SDA] at address `0x3D`)
* **Keys:** 14 discrete low-profile tactile switches with internal pull-ups (active low):
  * **Left Keypad (7 Note Keys):** `N1`–`N7` on `seed::D1`–`D7` (`PC11`, `PC10`, `PC9`, `PC8`, `PD2`, `PC12`, `PG10`)
  * **Right Keypad (7 Chord Keys):** `C1`–`C7` on `seed::D8`–`D10`, `D13`, `D14`, `D26`, `D27` (`PG11`, `PB4`, `PB5`, `PB6`, `PB7`, `PD11`, `PG9`)
* **Thumbsticks:** 2 dual-axis analog joysticks:
  * **Left Stick (Pitch Bend / Resonance):** `LX` on `seed::D22` (`PA5` / `A7`), `LY` on `seed::D21` (`PC4` / `A6`, software inverted)
  * **Right Stick (Pan / Modulation):** `RX` on `seed::D24` (`PA1` / `A9`, software inverted), `RY` on `seed::D23` (`PA4` / `A8`, software inverted)
* **Knobs:** 4 rotary potentiometers across the top panel (`1.0 - raw` software inverted):
  * **Left Pair (Chords):** Knob 1 / Chord Vol on `seed::D18` (`PA7` / `A3`), Knob 2 / Chord Filter on `seed::D17` (`PB1` / `A2`)
  * **Right Pair (Notes):** Knob 3 / Notes Vol on `seed::D19` (`PA6` / `A4`), Knob 4 / Notes Filter on `seed::D20` (`PC1` / `A5`)
* **Encoder:** 1 rotary encoder with integrated push switch for scale, waveform, and menu navigation:
  * Phase A on `seed::D15` (`PC0`), Phase B on `seed::D16` (`PA3`), Push Switch on `seed::D28` (`PA2`, active low)
  * 4 Gray-code transitions per detent click decoded with a Buxton finite-state machine
* **Audio & Speakers:**
  * **Headphone Jack:** 3.5mm stereo output driven by on-board Texas Instruments PCM3060 24-bit stereo codec via `SAI1` @ 48 kHz
  * **Internal Speakers:** On-board Class-D amplifier driving stereo case speakers with hardware enable/mute on `seed::D32` (`PC3`, active high: `1`=On, `0`=Muted)
* **Auxiliary ADC & Power Monitor:**
  * **Aux ADC:** `seed::D31` (`PC2` / `A12`) auxiliary voltage / battery sense
  * **Battery Low / Power Fault:** `seed::D0` (`PB12`, input pull-up, active low triggers factory "Charge Me!" alert)
* **MIDI:** Class-compliant USB MIDI over USB-C using libDaisy `MidiUsbHandler` (no hardware 3.5mm TRS MIDI port)
* **USB:** USB-C connector for power, DFU firmware flashing, USB CDC serial diagnostics, and USB MIDI. Wired to the Seed's **external** USB port (`seed::D29`/`seed::D30` = `PB14`/`PB15`, `USB_OTG_HS` in Full Speed mode) — use `UsbHandle::FS_EXTERNAL` / `Logger<LOGGER_EXTERNAL>`, not `hw.StartLog()`

---

## Documentation Website & Skills Reference

The documentation website is hosted on GitHub Pages:
👉 **[https://domesticmouse.github.io/reverse-engineering-gamma/](https://domesticmouse.github.io/reverse-engineering-gamma/)**

The site is built with Material for MkDocs and automatically updated on each commit to the `main` branch via GitHub Actions:
* **[Agent Skills Catalog](https://domesticmouse.github.io/reverse-engineering-gamma/skills/)**: Front-panel hardware drivers, pinout tables, firmware flashing runbooks, recovery workflows, and audio engine blueprints.
* **[DaisySP Guide](https://domesticmouse.github.io/reverse-engineering-gamma/DAISYSP_GUIDE/)**: DSP synthesis modules, filters, dynamics, memory footprints, and audio recipes.
* **[libDaisy Guide](https://domesticmouse.github.io/reverse-engineering-gamma/LIBDAISY_GUIDE/)**: STM32H750 HAL, memory sections (AXI SRAM, DTCM), DMA cache coherency, and audio engine architecture.
* **[Reverse-Engineering Roadmap](https://domesticmouse.github.io/reverse-engineering-gamma/PLAN/)**: Step-by-step phase execution plan.

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

Or use the zero-dependency host tool from the [USB Connectivity skill](.agents/skills/gamma-usb-connectivity/SKILL.md):
```bash
python3 .agents/skills/gamma-usb-connectivity/scripts/gamma_usb.py --monitor      # stream log output
python3 .agents/skills/gamma-usb-connectivity/scripts/gamma_usb.py --interactive  # send commands
python3 .agents/skills/gamma-usb-connectivity/scripts/gamma_usb.py --bootloader   # reboot into DFU
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
2. Expand **Troubleshooting** → **"Can't Enter Boot"**.
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

## Custom Firmware: Gamma Drum Synth

[`firmware/drum_synth`](firmware/drum_synth/main.cpp) is a seven-voice drum synthesizer built on libDaisy and DaisySP. It turns the Gamma into a playable drum machine with per-voice sound editing on the OLED.

**Voices** ([`drum_voices.h`](firmware/drum_synth/drum_voices.h)), mapped to the right-hand keypad:

| Key | Voice | Engine |
| --- | --- | --- |
| `C1` | Kick | 808-style bridged-T resonator (`AnalogBassDrum`), 30–90 Hz with resonance capping |
| `C2` | Snare | 808-style resonators + noise (`AnalogSnareDrum`), 120–400 Hz |
| `C3` | Clap | Band-passed noise with three "hand" bursts and a tail |
| `C4` | Tom | Pitch-swept analog tom (70–300 Hz) with exponential decay and attack punch |
| `C5` | Closed Hat | 808 metallic noise (`HiHat`) with custom envelope; chokes the open hat |
| `C6` | Open Hat | 808 metallic noise with longer decay |
| `C7` | Cymbal | 808 metallic noise with multi-second wash |

**Controls:**

| Control | Function |
| --- | --- |
| Encoder turn | Select the drum being edited (shown on OLED) |
| Encoder click | Audition the selected drum |
| Encoder hold 2 s | Reboot into the Daisy bootloader for a firmware update |
| Knobs 1–4 | Level, Tune, Decay, Tone of the selected drum (soft takeover — a knob only takes effect once it passes the stored value, so sounds never jump) |
| Right stick X | Master DJ filter (left = low-pass, right = high-pass, centre = bypass) |
| Right stick Y | Master drive (push up) |

**USB serial commands** (CDC on the USB-C port, see the [USB Connectivity skill](.agents/skills/gamma-usb-connectivity/SKILL.md)): `1`–`7` trigger voices, `p` prints all drum parameters and CPU load, `s` toggles the internal speaker, `b` reboots into the bootloader, `h`/`?` shows help.

**Build & flash:**
```bash
make -C firmware/drum_synth
python3 .agents/skills/gamma-firmware-flash/scripts/flash_firmware.py firmware/drum_synth/build/drum_synth.bin
```

Keys and the encoder are scanned in a 1 kHz timer ISR for low-latency triggering, idle voices are gated off to save CPU, and USB logging is non-blocking so the synth keeps running when the host disconnects.

---

## Documentation & Developer Guides

Comprehensive architectural and implementation guides for the core software stack:
* **[Comprehensive Guide to libDaisy](docs/LIBDAISY_GUIDE.md)**: Hardware abstraction, STM32H750 memory layout, peripherals (ADC/DMA, I2C, SPI, SAI), audio engine, graphics canvas, and board support packages.
* **[Comprehensive Guide to DaisySP](docs/DAISYSP_GUIDE.md)**: Digital Signal Processing library guide covering all 60+ synthesis, filter, effect, percussion, physical modeling, dynamics, and utility modules, lifecycle conventions, memory management, and practical DSP recipes.

---

## Skills & Runbooks

Operational runbooks, hardware specifications, and automated tooling are maintained as workspace skills:
* **[Gamma Hardware Pinout & Peripheral Reference](.agents/skills/gamma-pinout/SKILL.md)**: Master pin mappings, electrical characteristics, ADC formulas, and C++ header.
* **[Gamma Hardware Controls Guide](.agents/skills/gamma-hardware-controls/SKILL.md)**: Comprehensive guide for OLED display, keys, encoder, knobs, and joysticks.
* **[Gamma USB Connectivity](.agents/skills/gamma-usb-connectivity/SKILL.md)**: USB CDC virtual COM port setup, deadlock-immune non-blocking logging, host↔synth command dispatch, drop-in firmware driver (`gamma_usb.h`), and zero-dependency Python host tool (`gamma_usb.py`) for testing, monitoring, interactive control, and rebooting into the bootloader.
* **[Gamma Firmware Flashing](.agents/skills/gamma-firmware-flash/SKILL.md)**: Detailed runbook and automated polling script (`flash_firmware.py`).
* **[Gamma Firmware Restore](.agents/skills/gamma-firmware-restore/SKILL.md)**: Detailed runbook and automated restore script (`restore_firmware.py`).
* **[libDaisy Developer Guide](.agents/skills/libdaisy-guide/SKILL.md)**: Architectural reference and API guide for libDaisy hardware abstraction.
* **[DaisySP Developer Guide](.agents/skills/daisysp-guide/SKILL.md)**: Architectural reference and DSP module catalog for DaisySP audio algorithms.

---

## Project Status

- [x] **Toolchain & Software:** ARM toolchain (`arm-none-eabi-gcc 15.3.1`), `dfu-util 0.11`, `make`, and serial monitors (`tio`, `minicom`, `screen`) confirmed working.
- [x] **Submodules:** `libDaisy` and `DaisySP` linked as Git submodules and compiled.
- [x] **Phase 0 (Baseline Verification & Safety Net):** Daisy Bootloader identified over USB DFU (`0483:df11`, Electrosmith Daisy Bootloader). Factory firmware binaries (`gamma-v2.0.3.bin` and `gamma1_1.bin`) downloaded, analyzed via disassembly, and verified. Automated restore and flashing skills created and tested.
- [x] **Phase 1 (Diagnostic Console & I2C Scan):** USB CDC virtual COM port established. Scanned candidate I2C peripherals and discovered SSD1306 OLED display responding on `I2C1` (`seed::D11`/`seed::D12`) at address `0x3D`.
- [x] **Phase 2 (OLED Display Initialization & UI):** SSD1306 128x64 OLED display driver verified on hardware with real-time diagnostic dashboard and uptime rendering.
- [x] **Phase 3 (Analog Pin Mapping):** All 4 potentiometers (`K0`–`K3` on `seed::D18`, `D17`, `D19`, `D20`) and 2 dual-axis joysticks (`LX`, `LY`, `RX`, `RY` on `seed::D22`, `D21`, `D24`, `D23`) mapped, calibrated, and verified on hardware with real-time OLED bargraphs.
- [x] **Phase 4 (Digital Pin Mapping):** All 14 discrete tactile keys (7 note keys `N1`–`N7` on `seed::D1`–`D7`, 7 chord keys `C1`–`C7` on `seed::D8`–`D10`, `D13`, `D14`, `D26`, `D27`), rotary encoder quadrature pins (`seed::D15`, `seed::D16`) with Buxton FSM decoder, encoder click (`seed::D28`), speaker amp enable (`seed::D32` / `PC3`), and power fault sense (`seed::D0`) verified on hardware.
- [ ] **Phase 5 (Audio & MIDI Verification - ACTIVE):** Stereo audio engine implemented (`firmware/phase5_audio`) using libDaisy & DaisySP with PCM3060 codec via SAI1 @ 48 kHz (interactive synth with 7 notes + 7 polyphonic triad chords, dual SVF filters, joystick modulation, test tone mode). Audio engine ready for hardware validation; USB MIDI device implementation upcoming.
- [x] **USB Connectivity:** Non-blocking, deadlock-immune USB CDC logging and bidirectional host command dispatch documented in the [`gamma-usb-connectivity`](.agents/skills/gamma-usb-connectivity/SKILL.md) skill, with a drop-in firmware driver and Python host tool.
- [x] **Drum Synth Firmware:** Seven-voice drum synthesizer (`firmware/drum_synth`) with per-voice Level/Tune/Decay/Tone editing, soft-takeover knobs, master DJ filter and drive, OLED UI, and USB serial control. See [Custom Firmware: Gamma Drum Synth](#custom-firmware-gamma-drum-synth).
- [ ] **Phase 6 (Gamma Board Support Package):** Create unified `gamma_hw` C++ Board Support Package (BSP) and polyphonic synthesizer reference firmware.

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