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
* **Encoder:** 1 rotary encoder with integrated push button (key/scale selection)
* **Audio:** 3.5mm stereo headphone output (driven by on-board PCM3060 codec via SAI)
* **MIDI:** 3.5mm TRS MIDI output (UART TX)
* **USB:** USB-C connector for power, DFU firmware flashing, and USB serial/MIDI communication

---

## Reverse-Engineering Roadmap

For the detailed step-by-step plan covering software setup, non-destructive flash dumping, diagnostic firmware probing, pin mapping, and BSP creation, see:
* **[PLAN.md](PLAN.md)**: Reverse-Engineering and Pinout Mapping Plan

---

## Project Status

- [x] **Toolchain & Software:** ARM toolchain (`arm-none-eabi-gcc 15.3.1`), `dfu-util 0.11`, `make`, and serial monitors (`tio`, `minicom`, `screen`) confirmed working.
- [x] **Submodules:** `libDaisy` and `DaisySP` linked as Git submodules and compiled.
- [x] **Phase 0 (Baseline Verification):** Daisy Bootloader identified over USB DFU (`0483:df11`, Electrosmith Daisy Bootloader). Flash memory readout (`dfu-util -U`) confirmed unsupported by the Daisy Bootloader. Applications will target QSPI flash (`APP_TYPE = BOOT_QSPI`).
- [ ] **Phase 1 (Diagnostic Console & I2C Scan):** Next step.

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