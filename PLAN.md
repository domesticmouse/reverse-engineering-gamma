# Gamma Mini Synth Pinout Reverse-Engineering Plan

This document outlines the step-by-step strategy for reverse-engineering the hardware pinout and peripherals of the **this.is.Noise Inc Gamma** synthesizer. The device is powered by an **Electro-Smith Daisy Seed 2 DFM** (ARM Cortex-M7 @ 480 MHz, STM32H750, PCM3060 audio codec).

---

## 1. Required Software & Toolchain Setup

To build, flash, and interact with diagnostic firmware over USB-C, you will need the following tools installed on your host machine (macOS):

### A. ARM Embedded Toolchain (`arm-none-eabi-*`)
Compiles C/C++ firmware targeting the ARM Cortex-M7 microcontroller.
* **Option 1 (Homebrew - Recommended):**
  ```bash
  brew install --cask gcc-arm-embedded
  ```
* **Option 2 (Official Arm Developer Site):**
  Download the macOS `.pkg` or `.tar.xz` from [Arm GNU Toolchain Downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads).
* **Verify:**
  ```bash
  arm-none-eabi-gcc --version
  ```

### B. DFU Utility (`dfu-util`)
Sends compiled `.bin` firmware files directly to the Daisy Seed bootloader over the USB-C connection without requiring an external debugger.
* **Installation (Homebrew):**
  ```bash
  brew install dfu-util
  ```
* **Verify:**
  ```bash
  dfu-util --version
  ```

### C. Build Tools (`make` / Xcode Command Line Tools)
Used by `libDaisy` to build projects.
* **Installation:**
  ```bash
  xcode-select --install
  ```
* **Verify:**
  ```bash
  make --version
  ```

### D. Serial Terminal Monitor
Reads real-time diagnostic output transmitted from the Daisy over USB CDC (Virtual COM Port).
* **Terminal tools:**
  - Built-in `screen`:
    ```bash
    screen /dev/cu.usbmodem* 115200
    ```
    *(Exit screen using `Ctrl-A` then `Ctrl-\`)*
  - Alternatively: `minicom` (`brew install minicom`), `tio` (`brew install tio`), or Python `pyserial`:
    ```bash
    python3 -m pip install pyserial
    python3 -m serial.tools.miniterm /dev/cu.usbmodem* 115200
    ```

### E. Electro-Smith Daisy Repositories
The core HAL and DSP libraries for the Daisy Seed 2 DFM:

  - `libDaisy`: [libDaisy/](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy) (Hardware abstraction layer; builds `build/libdaisy.a`)
  - `DaisySP`: [DaisySP/](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/DaisySP) (DSP synthesis library; builds `build/libdaisysp.a`)

---

## 2. Hardware Inventory & Daisy Seed 2 DFM Target

The complete pin mapping, electrical specifications, and peripheral configurations have been moved to the dedicated skill:
👉 **[Gamma Hardware Pinout & Peripheral Reference](.agents/skills/gamma-pinout/SKILL.md)** (with C++ header [`gamma_pins.h`](.agents/skills/gamma-pinout/resources/gamma_pins.h))

| Component | Description | Confirmed Hardware Interface / Pinout | Status |
| :--- | :--- | :--- | :--- |
| **OLED Display** | 1.3" display (SSD1306 controller, 128x64) | `I2C1` (SCL: `seed::D11` / `PB8`, SDA: `seed::D12` / `PB9`) @ `0x3D` | **Verified on Hardware** |
| **Potentiometers** | 4 rotary knobs (Chords Vol/Filter, Notes Vol/Filter) | 4 ADC channels (`seed::D18`, `D17`, `D19`, `D20` / `A3`, `A2`, `A4`, `A5`) | **Verified on Hardware** |
| **Thumbsticks** | 2 analog joysticks (Left X/Y, Right X/Y) | 4 ADC channels (`seed::D22`, `D21`, `D24`, `D23` / `A7`, `A6`, `A9`, `A8`) | **Verified on Hardware** |
| **Aux ADC** | Internal / Battery / Aux sensor | 1 ADC channel: `seed::D31` (`PC2`/`A12`) | **Confirmed via Disassembly** |
| **14 Keys** | 14 tactile low-profile switches (7 chord, 7 note) | 14 discrete GPIO inputs w/ internal pullups (`seed::D1`–`D10`, `D13`, `D14`, `D26`, `D27`) | **Verified on Hardware** |
| **Rotary Encoder** | 1 rotary dial w/ push switch (scale/key selection) | Phase A (`D15`), Phase B (`D16`), Push Switch (`D28`) | **Verified on Hardware** |
| **Speaker Amp En** | Internal speaker amplifier enable / mute | `PC3` (GPIO Out, Active HIGH: `1`=On, `0`=Muted) | **Verified on Hardware** |
| **Audio Output** | 3.5mm stereo headphone jack | On-board PCM3060 codec via SAI | Internal Daisy routing |
| **MIDI Output** | 3.5mm TRS MIDI Out | Hardware UART TX @ 31,250 baud | In progress |
| **USB-C** | Power, flashing (DFU), USB Serial/MIDI | STM32 USB OTG FS (`vbus_sensing_enable = DISABLE`) | **Verified on Hardware** |

---

## 3. Reverse-Engineering Roadmap

```mermaid
flowchart TD
    P0["Phase 0: Baseline & Backup (COMPLETED)"]:::done --> P1["Phase 1: USB CDC & I2C Bus Scan (COMPLETED)"]:::done
    P1 --> P2["Phase 2: Display Initialization (COMPLETED)"]:::done
    P2 --> P3["Phase 3: ADC Mapping (Knobs & Joysticks) (COMPLETED)"]:::done
    P3 --> P4["Phase 4: Digital Pin Mapping (Keys & Encoder) (COMPLETED)"]:::done
    P4 --> P5["Phase 5: Audio & MIDI Verification - ACTIVE"]:::active
    P5 --> P6["Phase 6: Gamma Board Support Package (BSP)"]

    classDef done fill:#2e7d32,stroke:#1b5e20,color:#fff
    classDef active fill:#1565c0,stroke:#0d47a1,color:#fff
```

---

### Phase 0: Baseline Verification & Firmware Backup (COMPLETED)
**Goal:** Verify communication over USB-C, understand the bootloader runtime model, and secure a verifiable factory restore path.

1. **Boot into DFU Mode:**
   - Put the Daisy into system bootloader mode (hold `BOOT`, tap `RESET`, release `BOOT`).
   - Query USB DFU status:
     ```bash
     dfu-util -l
     ```
   - **Result (Verified):** The Gamma was successfully detected:
     ```text
     Found DFU: [0483:df11] ver=0200, devnum=1, cfg=1, intf=0, path="1-1", alt=0, name="@Flash /0x90000000/64*4Kg/0x90040000/60*64Kg/0x90400000/60*64Kg", serial="3067366D3433"
     Product: "Daisy Bootloader" (Electrosmith)
     ```

2. **Bootloader Runtime & Memory Architecture Findings:**
   - **Storage vs. Execution:** Programs are stored in external QSPI flash starting at `0x90040000`. However, the Daisy Bootloader on this device is configured to load binaries into **AXI SRAM (`0x24000000`)** before branching to them (`APP_TYPE = BOOT_SRAM`).
   - **Boot Window:** On power-up or hardware reset, the bootloader listens for DFU for ~2 to 2.5 seconds (USER LED pulses). If no connection is made, it jumps to the SRAM application.
   - **Display Persistence:** The 1.3" OLED panel retains its internal display RAM (GDDRAM) as long as 3.3V logic power is present. An unbooted MCU or bootloader halt leaves the previous image (`this.is.NOISE inc`) on screen.
   - **Factory Firmware Recovery:** Acquired official production binaries hosted by the web update tool (`https://gammaupdatetool.netlify.app/`):
     - [`backups/gamma-v2.0.3.bin`](backups/gamma-v2.0.3.bin) (v2.0.3, 360,740 bytes)
     - [`backups/gamma1_1.bin`](backups/gamma1_1.bin) (v1.1, 269,764 bytes)
   - **Vector Table Analysis of Factory Binary:**
     - Initial SP: `0x20020000` (top of DTCMRAM, 128 KB)
     - Reset Handler: `0x240047ED` (AXI SRAM at `0x24000000`)
     - Confirms `APP_TYPE = BOOT_SRAM` is mandatory.
     - Strings analysis shows the factory firmware is developed in embedded **Rust** (`src/display/`, `src/midi/`, `src/looper/`).
   - **Bootloader Modes & DFU Entry Points:**
     - **Hardware ROM Bootloader (Hold BOOT + Tap RESET):** Pulls the MCU's `BOOT0` pin HIGH and boots into STMicroelectronics' factory system ROM. Bypasses the Daisy Bootloader entirely; does not initialize the Seed 2 DFM High-Speed USB PHY (USER LED stays OFF, host cannot communicate). **Do not use this mode for flashing.**
     - **Daisy Bootloader Grace Period (Tap RESET alone or Power-Cycle):** Microcontroller boots into internal flash (`0x08000000`), pulses/breathes USER LED, and listens for DFU on USB-C for ~2.5 seconds before jumping to the application at `0x90040000`. This is the primary interactive flashing workflow.
     - **Daisy Bootloader Infinite Timeout (Gamma Menu `System -> Info -> Enter Boot`):** Sets `DAISY_INFINITE_TIMEOUT` in Backup SRAM (`0x38800000`) and soft-resets into the Daisy Bootloader, holding DFU mode indefinitely without timing out.
   - **Clock Initialization Traps:**
     - The Daisy Bootloader initializes the system PLL to 480 MHz before jumping to SRAM. In `daisy_seed.cpp`, `syscfg.skip_clocks` must remain `true` for SRAM builds. Re-configuring the active PLL in application code traps in `Error_Handler()` and freezes SysTick.
   - **USB CDC Enumeration & VBUS Sensing Discovery:**
     - Disassembly of official production firmware (`gamma-v2.0.3.bin` at `0x24029c78`) revealed that `hpcd_USB_OTG_FS.Init.vbus_sensing_enable` is set to **`DISABLE` (`0`)**.
     - Upstream `libDaisy` defaults `vbus_sensing_enable` to `ENABLE` (`1`), requiring 5V on pin `PA9` to activate the internal D+ pullup resistor. Because Gamma does not route 5V VBUS to `PA9`, STM32 detects "cable disconnected" and never pulls up D+.
     - Patched [`libDaisy/src/usbd/usbd_conf.c`](libDaisy/src/usbd/usbd_conf.c) to set `vbus_sensing_enable = DISABLE`.
     - When launching from the bootloader, `syscfg.skip_clocks` is set, which leaves `HSI48` and `RCC_USBCLKSOURCE_HSI48` uninitialized unless explicitly enabled in application code.
   - **I2C Bus Recovery:**
     - Explicit bus reset (`__HAL_RCC_I2C1_FORCE_RESET()` / `RELEASE_RESET()`) and clean GPIO alternate function pin muxing (`GPIO_AF4_I2C1`) with pullups ensures reliable OLED communication without hanging on bus transitions.

---

### Dual-Track Strategy: Static Analysis & Dynamic Hardware Probing

Having official production firmware (`gamma-v2.0.3.bin`) enabled static reverse-engineering alongside dynamic probing:

1. **Static Analysis Results (Verified from Disassembly):**
   - **Display Hardware Peripheral:** Disassembly of the hardware initialization routine (`0x24007f1c` - `0x24007f3e` and `0x240077b0`) revealed the exact configuration passed to `I2CHandle::Init`:
     - **Peripheral:** `I2C1` (`0x40005400`)
     - **SCL Pin:** `seed::D11` (`PB8`, GPIO Port B pin 8)
     - **SDA Pin:** `seed::D12` (`PB9`, GPIO Port B pin 9)
     - **Bus Speed:** `I2C_1MHZ` (Fast Mode Plus)
     - **I2C Device Address:** **`0x3D`** (`movs r3, #61` at `0x24007f2a`)
     - **Display Controller:** **SSD1306** (128x64).
   - **Analog ADC Configuration (Knobs & Joysticks):** Disassembly of the ADC setup routine (`0x24007d4c` - `0x24007f18` and `0x240261f8` `AdcChannelConfig::InitSingle`):
     - **9 Total ADC Channels** configured with `AdcHandle::Init(&cfg, 9)`:
       - **Potentiometers (Channels 0–3, 0.05 IIR filter in factory firmware):**
         - Channel 0: `seed::D18` (`PA7` / `ADC1_INP7` / `A3`) — Knob 1 (Chord Vol)
         - Channel 1: `seed::D17` (`PB1` / `ADC1_INP5` / `A2`) — Knob 2 (Chord Filter)
         - Channel 2: `seed::D19` (`PA6` / `ADC1_INP3` / `A4`) — Knob 3 (Notes Vol)
         - Channel 3: `seed::D20` (`PC1` / `ADC1_INP11` / `A5`) — Knob 4 (Notes Filter)
       - **Joysticks (Channels 4–7, 0.002 IIR filter in factory firmware):**
         - Channel 4: `seed::D24` (`PA1` / `ADC1_INP17` / `A9`) — Right Stick X (RX)
         - Channel 5: `seed::D23` (`PA4` / `ADC1_INP18` / `A8`) — Right Stick Y (RY)
         - Channel 6: `seed::D22` (`PA5` / `ADC1_INP19` / `A7`) — Left Stick X (LX)
         - Channel 7: `seed::D21` (`PC4` / `ADC1_INP4` / `A6`) — Left Stick Y (LY)
       - **Auxiliary / Battery ADC (Channel 8):**
         - Channel 8: `seed::D31` (`PC2` / `ADC1_INP12`)
     - *Note on Filter Values:* Disassembly confirms factory firmware initialized channels 0–3 with `0.05` and channels 4–7 with `0.002`. In custom firmware, we assign `0.002` (or deadband) to the top knobs for steady values and `0.05` to the joysticks for responsive modulation.
   - **14 Discrete Key Switches:** Disassembly of key initialization at `0x24007ab8` - `0x24007bd6` (`Switch::Init` at `0x24025de0`):
     - Each switch is directly connected to a dedicated MCU GPIO with internal pull-up resistor (active low):
       - **Chord Keys (7 switches):** `D8` (`PG11`), `D9` (`PB4`), `D10` (`PB5`), `D13` (`PB6`), `D14` (`PB7`), `D26` (`PD11`), `D27` (`PG9`)
       - **Note Keys (7 switches):** `D1` (`PC11`), `D2` (`PC10`), `D3` (`PC9`), `D4` (`PC8`), `D5` (`PD2`), `D6` (`PC12`), `D7` (`PG10`)
   - **Rotary Encoder with Push Switch:** Disassembly of encoder initialization at `0x24007c22` - `0x24007d22` (`Encoder::Init` at `0x24025b84`):
     - **Phase A Pin:** `seed::D15` (`PC0`)
     - **Phase B Pin:** `seed::D16` (`PA3`)
     - **Push Switch Pin:** `seed::D28` (`PA2`, active low w/ internal pull-up)
   - **Speaker Amplifier Enable & Auxiliary GPIOs:**
     - `PC3`: Speaker amplifier enable / mute (active HIGH). Held LOW (`0`) at `0x24007bfc` during boot for anti-pop protection; driven HIGH (`1`) 60ms after audio initialization (`0x24006b9c`) and toggled via menu (`SPK ON`/`SPK OFF` at `0x2400eb16`).
     - `seed::D0` (`PB12`): Power fault / battery monitor (input pull-up). Triggers "Charge Me!" display when pulled LOW.
   - **Daisy Hal Integration:** The firmware statically links libDaisy peripheral abstractions (`I2CHandle`, `AdcHandle`, `Switch`, `Encoder`).

2. **Dynamic Probing & On-Screen UI (Diagnostic Builds):**
   - Flash targeted C++ diagnostic builds using `libDaisy` to interactively verify pin behaviors, readout analog voltages, and drive the OLED display.

---

### Phase 1: USB CDC Diagnostic Console & I2C Bus Scanning (COMPLETED)
**Goal:** Establish serial communication over USB-C, probe for peripherals, and render live diagnostics on the 1.3" OLED display.

1. **Diagnostic Firmware Setup:**
   - Source: [`firmware/phase1_i2c_scan/main.cpp`](firmware/phase1_i2c_scan/main.cpp)
   - Compiled with **`APP_TYPE = BOOT_SRAM`** (vectors at `0x24000000`, validated entry point `0x24000795`).
   - Integrated HSI48 oscillator activation and routing for USB clock domain.
   - Non-blocking USB CDC virtual COM port logging with receive commands (`'s'` scan, `'b'` DFU bootloader, `'h'` help).
   - Integrated SSD1306 128x64 OLED display driver (`I2C1`, `D11`/`D12` @ `0x3D`) to render scan results and live uptime directly on the device.
   - LED heartbeat at 2 Hz (250ms toggle).

2. **Automated Flashing Workflow:**
   - Runbook: [`.agents/skills/gamma-firmware-flash/SKILL.md`](.agents/skills/gamma-firmware-flash/SKILL.md)
   - Script: `python3 .agents/skills/gamma-firmware-flash/scripts/flash_firmware.py`
   - Automatically polls for DFU every 100ms, validates SRAM vector layout, and flashes to `0x90040000:leave`.

3. **I2C Scanner Implementation & HAL Hardware Fix:**
   - Multi-candidate hardware bus scanner:
     - **Bus 1 (Hardware Verified):** `I2C1` on `D11` (`PB8` / SCL) & `D12` (`PB9` / SDA)
     - **Bus 2:** `I2C1` on `D13` (`PB6` / SCL) & `D14` (`PB7` / SDA)
     - **Bus 3:** `I2C4` on `D11` (`PB8` / SCL) & `D12` (`PB9` / SDA)
     - **Bus 4:** `I2C4` on `D13` (`PB6` / SCL) & `D14` (`PB7` / SDA)
   - **STM32 HAL Pin-Muxing Fix:** Explicitly re-initializes and de-initializes GPIO alternate function pin muxing between candidate sweeps.
   - **Target Detection & On-Screen Output:** Live results displayed on both USB CDC serial and on the OLED dashboard.

4. **Hardware Verification Results:**
   - Successfully flashed custom `BOOT_SRAM` firmware over USB DFU via the Daisy Bootloader.
   - Live hardware execution confirmed on device:
     - Scanned all candidate I2C peripherals and discovered the display responding at **`0x3D`**.
     - Initialized the SSD1306 OLED display driver over `I2C1` (`seed::D11` / `seed::D12`).
     - Real-time diagnostic UI actively running and rendering on the Gamma's physical display with ticking uptime counter.

---

### Phase 2: OLED Display Initialization & Local UI (COMPLETED)
**Goal:** Drive the display directly to show live diagnostics on the device.

1. **Hardware Verification Results:**
   - **Controller:** SSD1306 (128x64 monochrome).
   - **Interface:** `I2C1` via `seed::D11` (PB8 / SCL) & `seed::D12` (PB9 / SDA) at 7-bit address **`0x3D`**.
   - **Status:** **Fully verified on hardware**. Live graphics buffer rendering text and status frames reliably.
2. **On-Screen Dashboard:**
   - Rendered diagnostic screen showing:
     - Header: `GAMMA I2C SCANNER`
     - Bus: `I2C1 (D11/D12)`
     - Discovered Devices: `0x3D`
     - Status: `OLED @ 0x3D: Active`
     - Live counter: `Uptime: Xs` updating continuously.

---

### Phase 3: Analog Pin Mapping (Potentiometers & Joysticks) (COMPLETED)
**Goal:** Verify and calibrate the 4 knobs and 2 joysticks (4 axes) using live on-screen visual bargraphs and USB serial.

1. **Hardware Pin Mapping (Hardware Verified):**
   - Detailed pinout, electrical characteristics, and ADC formulas are documented in:
     👉 **[Gamma Hardware Pinout & Peripheral Reference](.agents/skills/gamma-pinout/SKILL.md)**
   - **4 Rotary Potentiometers:** `K0` (`D18` / `PA7`), `K1` (`D17` / `PB1`), `K2` (`D19` / `PA6`), `K3` (`D20` / `PC1`). Sweeps 3.3V (CCW) to 0V (CW); inverted in software (`1.0 - raw`).
   - **2 Joysticks (4 Axes):** `LX` (`D22` / `PA5`, direct), `LY` (`D21` / `PC4`, inverted), `RX` (`D24` / `PA1`, inverted), `RY` (`D23` / `PA4`, inverted).
   - **Auxiliary ADC:** `AUX` (`D31` / `PC2`).

2. **Phase 3 Diagnostic Firmware:**
   - Source: [`firmware/phase3_adc/main.cpp`](firmware/phase3_adc/main.cpp)
   - Binary: `firmware/phase3_adc/build/phase3_adc.bin`
   - Compiled with **`APP_TYPE = BOOT_SRAM`** (verified vectors: SP `0x20020000`, Reset `0x24000795`).
   - Features real-time OLED diagnostic display:
     - **Dual-Column Analog Overview:** Left column shows 0%–100% bargraphs and numerical values for all 4 knobs (`CV`, `CF`, `NV`, `NF`), right column shows zero-centered bidirectional bargraphs for all 4 joystick axes (`LX`, `LY`, `RX`, `RY`).
     - **Status Bar:** Displays real-time delta events or uptime counter.
   - Non-blocking USB CDC logging of real-time deltas and value changes.
   - *(Note: Dedicated 14 keys & encoder dashboard was implemented in Phase 4 `phase4_keys_encoder`).*

3. **Interactive Hardware Calibration Protocol:**
   - Turn each knob (Chord Vol, Chord Filter, Notes Vol, Notes Filter) $\rightarrow$ Confirm physical-to-logical channel mapping (`K0`–`K3`).
   - Deflect Left and Right thumbsticks along X and Y axes $\rightarrow$ Confirm physical-to-logical channel mapping (`J0`–`J3`).
   - Record ADC zero-points, center deadbands, and maximum endpoints.

---

### Phase 4: Digital Pin Mapping (14 Keys & Rotary Encoder) (COMPLETED)
**Goal:** Confirm physical key layout and rotary encoder quadrature behavior.

1. **Hardware Pin Mapping & Physical Layout (100% Hardware Verified):**
   - Detailed keypad matrices, quadrature timing, and auxiliary pin behaviors are documented in:
     👉 **[Gamma Hardware Pinout & Peripheral Reference](.agents/skills/gamma-pinout/SKILL.md)**
   - **Left Keypad (7 Note Keys, Active-Low w/ Pull-up):**
     - Top row: `N1`–`N4` (`D1`–`D4` / `PC11`, `PC10`, `PC9`, `PC8`)
     - Bottom row: `N5`–`N7` (`D5`–`D7` / `PD2`, `PC12`, `PG10`)
   - **Right Keypad (7 Chord Keys, Active-Low w/ Pull-up):**
     - Top row: `C1`–`C4` (`D8`–`D10`, `D13` / `PG11`, `PB4`, `PB5`, `PB6`)
     - Bottom row: `C5`–`C7` (`D14`, `D26`, `D27` / `PB7`, `PD11`, `PG9`)
   - **Rotary Encoder with Push Switch:** Phase A (`D15` / `PC0`), Phase B (`D16` / `PA3`), Click (`D28` / `PA2`). Standard mechanical encoder with 4 Gray code transitions per detent click (FSM decoder, detents at `11`). Requires high-frequency sampling (timer / audio DMA) to avoid missed ticks during I2C display updates.
   - **Speaker Amplifier Enable & Auxiliary Pins:** `PC3` (Speaker Enable/Mute, active HIGH), `seed::D0` (`PB12`, Input pull-up).

2. **Dedicated Diagnostic Firmware:**
   - Source: [`firmware/phase4_keys_encoder/main.cpp`](firmware/phase4_keys_encoder/main.cpp)
   - Binary: `firmware/phase4_keys_encoder/build/phase4_keys_encoder.bin`
   - Features real-time graphical representation of the physical 4+3 left/right keypad, encoder position counter, last direction indicator (`CW`/`CCW`), click indicator, and raw pin diagnostics.

---

### Phase 5: Audio & MIDI Verification (ACTIVE)
**Goal:** Validate audio generation and external MIDI communication.

1. **Stereo Audio Engine (Firmware Implemented & Ready for Hardware Verification):**
   - Source: [`firmware/phase5_audio/main.cpp`](firmware/phase5_audio/main.cpp)
   - Binary: `firmware/phase5_audio/build/phase5_audio.bin`
   - Compiled with **`APP_TYPE = BOOT_SRAM`** (vectors at `0x24000000`, validated entry point `0x24000795`).
   - Links `libDaisy` and `DaisySP` (`Oscillator`, `Svf` filters).
   - Initializes on-board **PCM3060 24-bit stereo codec via SAI1 @ 48 kHz** (Daisy Seed 2 DFM).
   - **Mode 0: Interactive Synthesizer:**
     - **7 Note Keys (N1–N7):** Play C Major scale notes ($C_4$ to $B_4$, 261.63 Hz – 493.88 Hz) through an envelope-smoothed oscillator and dedicated state variable lowpass filter.
     - **7 Chord Keys (C1–C7):** Play full 3-oscillator polyphonic triads ($C\text{ Maj}$, $D\text{ Min}$, $E\text{ Min}$, $F\text{ Maj}$, $G\text{ Maj}$, $A\text{ Min}$, $B\text{ Dim}$) through a dedicated state variable lowpass filter.
     - **Potentiometers (Knobs 1–4 across top panel, Left to Right):**
       - `Knob 1` (K0 / Chord Vol): Chord synthesizer volume (0% to 100%).
       - `Knob 2` (K1 / Chord Filter): Chord lowpass filter cutoff (100 Hz to 14,000 Hz).
       - `Knob 3` (K2 / Notes Vol): Note synthesizer volume (0% to 100%).
       - `Knob 4` (K3 / Notes Filter): Note lowpass filter cutoff (100 Hz to 14,000 Hz).
       - *Ergonomic Design:* Note keys on the left are played with the left hand while Knobs 3 & 4 (Notes Vol/Filter) on the right are adjusted with the right hand. Chord keys on the right are played with the right hand while Knobs 1 & 2 (Chord Vol/Filter) on the left are adjusted with the left hand.
     - **Joysticks:**
       - `LX` (Pitch Bend): $\pm 2$ semitones bend.
       - `LY` (Resonance): Modulates filter resonance ($Q = 0.05$ to $0.75$).
       - `RX` (Stereo Pan): Pans mix smoothly between Left and Right channels.
     - **Rotary Encoder:**
       - Turn dial: Cycles oscillator waveform (`SINE` $\rightarrow$ `TRI` $\rightarrow$ `SAW` $\rightarrow$ `SQR`).
       - Click dial: Toggles between **SYNTH MODE** and **TEST TONE MODE**.
   - **Mode 1: Diagnostic Test Tone (Stereo Channel Isolation):**
     - Continuous test tone to test 3.5mm stereo headphone output and codec DAC.
     - `K0`: Channel routing selector (**Left Only** / **Stereo Center** / **Right Only**) for channel isolation checks.
     - `K1`: Sweep test frequency (50 Hz to 2,000 Hz).
     - `K2`: Master Volume (0% to 100%).
   - **OLED Dashboard (SSD1306 128x64):**
     - Real-time stereo peak VU meters (`L` and `R`).
     - Real-time active note, chord, frequency, volume %, and filter cutoff display.
     - Waveform indicator and last event log.
   - **USB CDC Diagnostics:** Non-blocking serial logging and commands (`'m'`/`'t'` toggle mode, `'w'` cycle waveform, `'b'` reboot to DFU bootloader).

2. **Real-Time DSP Profiling (`CpuLoadMeter`):**
   - Integrate `daisy::CpuLoadMeter` into the audio callback (`load_meter.OnBlockStart()` / `load_meter.OnBlockEnd()`) to track real-time DSP load margins and display on the OLED / USB serial console before expanding synthesis algorithms.

3. **MIDI TRS Output (Upcoming):**
   - Implement external MIDI transmission using libDaisy's native `MidiUartHandler` ([`libDaisy/src/hid/midi.h`](libDaisy/src/hid/midi.h)):
     - Configure `MidiUartHandler::Config` for `USART_3` with TX pin on candidate `seed::D29` (`PB10` / `USART3_TX`) at standard 31,250 baud.
     - *(Note: `D14` / `PB7` was initially considered, but hardware testing confirmed `D14` is dedicated to Chord Key `C5`).*
   - Transmit continuous MIDI Note On / Note Off messages to verify the 3.5mm TRS MIDI Out jack.
   - Verify electrical polarity across Tip vs. Ring (MIDI Association Type A standard vs. legacy Type B).

---

### Phase 6: Board Support Package (BSP) Synthesis
**Goal:** Bundle all findings into a clean, reusable C++ library modeled after official libDaisy BSPs ([`DaisyPod`](libDaisy/src/daisy_pod.h), [`DaisyField`](libDaisy/src/daisy_field.h), [`DaisyPatch`](libDaisy/src/daisy_patch.h)).

* **`gamma_pins.h`**: Comprehensive pin mapping enum and constant definitions (located in [`.agents/skills/gamma-pinout/resources/gamma_pins.h`](.agents/skills/gamma-pinout/resources/gamma_pins.h)).
* **`gamma_hw.h` / `gamma_hw.cpp`**: Unified hardware abstraction class (`Gamma`):
  - **Embedded `DaisySeed seed;`** base instance.
  - **Analog Controls:** 4 rotary knobs and 4 joystick axes encapsulated as `AnalogControl` objects with built-in IIR filtering, deadband rejection, and software inversion.
  - **Parameter Curving:** Native `Parameter` helpers with `Parameter::Curve::EXPONENTIAL` for volume and filter cutoffs.
  - **Digital Controls:** 14 `Switch` objects (7 note + 7 chord keys) and encoder click initialized with `Switch::Polarity::POLARITY_INVERTED`, sampled at 1 kHz.
  - **Rotary Encoder:** Gray-code FSM quadrature decoding.
  - **Display:** Pre-configured `OledDisplay<SSD130xI2c128x64Driver>` on `I2C1` @ `0x3D`.
  - **MIDI:** Native `MidiUartHandler` integration for TRS MIDI Out.
  - **Audio & Amplification:** Methods to control speaker mute (`PC3`), anti-pop sequencing, and audio stream routing.
  - **Standardized BSP Control API:**
    - `Gamma::Init(bool boost = false)`
    - `Gamma::ProcessAnalogControls()`
    - `Gamma::ProcessDigitalControls()`
    - `Gamma::ProcessAllControls()`
    - `Gamma::SetSpeakerMute(bool muted)`
* **Preset Persistence:** Support user patch and configuration persistence via libDaisy's `PersistentStorage<T>` ([`libDaisy/src/util/PersistentStorage.h`](libDaisy/src/util/PersistentStorage.h)) targeting upper sectors of external QSPI flash (`IS25LP064A`).
* **Example Project:** Polyphonic synthesizer reference application built directly on top of the Gamma BSP.
