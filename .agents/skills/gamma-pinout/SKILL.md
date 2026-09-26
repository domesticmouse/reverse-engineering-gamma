---
name: gamma-pinout
description: >-
  Comprehensive hardware pinout and peripheral specification for the this.is.NOISE Gamma Mini Synth
  (Electro-Smith Daisy Seed 2 DFM). Includes pin mappings, electrical characteristics, ADC channels,
  polarity/inversion details, and peripheral initialization rules.
---

# Gamma Hardware Pinout & Peripheral Reference

This skill documents the complete hardware pinout, electrical characteristics, and peripheral mappings for the **this.is.NOISE Gamma Mini Synth**, powered by an embedded **Electro-Smith Daisy Seed 2 DFM** (ARM Cortex-M7 @ 480 MHz, STM32H750IBK6, PCM3060 stereo audio codec).

A ready-to-use C++ header containing these definitions is available at:
[`resources/gamma_pins.h`](resources/gamma_pins.h)

---

## 1. System Overview

| Parameter | Specification |
| :--- | :--- |
| **Microcontroller** | STMicroelectronics STM32H750IBK6 (ARM Cortex-M7 @ 480 MHz) |
| **Carrier Board** | Electro-Smith Daisy Seed 2 DFM |
| **Audio Codec** | Texas Instruments PCM3060 24-bit stereo codec via SAI1 @ 48 kHz |
| **Display** | 1.3" OLED (SSD1306 controller, 128x64 monochrome) via I2C1 @ `0x3D` |
| **Bootloader** | Electro-Smith Daisy Bootloader in internal flash (`0x08000000`) |
| **Application Memory** | Binaries flashed to QSPI flash (`0x90040000`), loaded to **AXI SRAM** (`0x24000000`, `APP_TYPE = BOOT_SRAM`) |
| **USB Interface** | USB OTG FS (Full Speed 12 Mbps) with **`vbus_sensing_enable = DISABLE`** |

---

## 2. Master Pinout Table

| Peripheral | Label | Daisy Seed Pin | STM32 Pin | Function / Channel | Electrical Config | Polarity / Logic | Verification Status |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **OLED Display** | SCL | `seed::D11` | `PB8` | `I2C1_SCL` | Alternate Function 4 | Open-drain w/ pullup | Verified on Hardware |
| **OLED Display** | SDA | `seed::D12` | `PB9` | `I2C1_SDA` | Alternate Function 4 | Open-drain w/ pullup | Verified on Hardware |
| **Potentiometer** | Knob 0 (Chord Vol) | `seed::D18` | `PA7` | `ADC1_INP7` (`A3`) | Analog Input | Inverted: `1.0 - raw` | Verified on Hardware |
| **Potentiometer** | Knob 1 (Chord Filter) | `seed::D17` | `PB1` | `ADC1_INP5` (`A2`) | Analog Input | Inverted: `1.0 - raw` | Verified on Hardware |
| **Potentiometer** | Knob 2 (Notes Vol) | `seed::D19` | `PA6` | `ADC1_INP3` (`A4`) | Analog Input | Inverted: `1.0 - raw` | Verified on Hardware |
| **Potentiometer** | Knob 3 (Notes Filter) | `seed::D20` | `PC1` | `ADC1_INP11` (`A5`) | Analog Input | Inverted: `1.0 - raw` | Verified on Hardware |
| **Joystick Left** | LX (Left-Right) | `seed::D22` | `PA5` | `ADC1_INP19` (`A7`) | Analog Input | Normal: Left=0%, Right=100% | Verified on Hardware |
| **Joystick Left** | LY (Down-Up) | `seed::D21` | `PC4` | `ADC1_INP4` (`A6`) | Analog Input | Inverted: Down=0%, Up=100% | Verified on Hardware |
| **Joystick Right** | RX (Left-Right) | `seed::D24` | `PA1` | `ADC1_INP17` (`A9`) | Analog Input | Inverted: Left=0%, Right=100% | Verified on Hardware |
| **Joystick Right** | RY (Down-Up) | `seed::D23` | `PA4` | `ADC1_INP18` (`A8`) | Analog Input | Inverted: Down=0%, Up=100% | Verified on Hardware |
| **Auxiliary ADC** | AUX (Battery/Sens) | `seed::D31` | `PC2` | `ADC1_INP12` (`A12`) | Analog Input | Direct voltage readout | Confirmed Disassembly |
| **Note Key N1** | Keypad Left Top 1 | `seed::D1` | `PC11` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Note Key N2** | Keypad Left Top 2 | `seed::D2` | `PC10` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Note Key N3** | Keypad Left Top 3 | `seed::D3` | `PC9` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Note Key N4** | Keypad Left Top 4 | `seed::D4` | `PC8` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Note Key N5** | Keypad Left Bot 1 | `seed::D5` | `PD2` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Note Key N6** | Keypad Left Bot 2 | `seed::D6` | `PC12` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Note Key N7** | Keypad Left Bot 3 | `seed::D7` | `PG10` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Chord Key C1** | Keypad Right Top 1 | `seed::D8` | `PG11` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Chord Key C2** | Keypad Right Top 2 | `seed::D9` | `PB4` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Chord Key C3** | Keypad Right Top 3 | `seed::D10` | `PB5` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Chord Key C4** | Keypad Right Top 4 | `seed::D13` | `PB6` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Chord Key C5** | Keypad Right Bot 1 | `seed::D14` | `PB7` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Chord Key C6** | Keypad Right Bot 2 | `seed::D26` | `PD11` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Chord Key C7** | Keypad Right Bot 3 | `seed::D27` | `PG9` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Rotary Encoder** | Phase A | `seed::D15` | `PC0` | GPIO In | Internal Pull-up | Quadrature Gray Code | Verified on Hardware |
| **Rotary Encoder** | Phase B | `seed::D16` | `PA3` | GPIO In | Internal Pull-up | Quadrature Gray Code | Verified on Hardware |
| **Rotary Encoder** | Push Button | `seed::D28` | `PA2` | GPIO In | Internal Pull-up | Active LOW (Pressed = 0) | Verified on Hardware |
| **Rail Ground** | Board Rail Low | N/A | `PC3` | GPIO Out | Output Push-Pull | Driven LOW (`0`) | Confirmed Disassembly |
| **Auxiliary In** | Aux Input Pull | `seed::D0` | `PB12` | GPIO In | Internal Pull-up | Logic input | Confirmed Disassembly |
| **Audio Out** | Stereo DAC Out | Internal | Multiple | `SAI1` | PCM3060 Codec | 48 kHz / 24-bit Stereo | Internal Daisy routing |
| **MIDI Out** | 3.5mm TRS MIDI | `seed::D14` / `D29` | `PB7` / `PB10` | Hardware UART TX | 31,250 baud | MIDI Serial | Testing in Phase 5 |
| **USB-C** | D+ / D- / Power | Internal | `PA11`/`PA12` | `USB_OTG_FS` | Full Speed Device | `vbus_sensing = DISABLE` | Verified on Hardware |

---

## 3. OLED Display Interface

* **Controller:** Solomon Systech SSD1306 (128x64 monochrome)
* **Bus Peripheral:** `I2C1` (`0x40005400`)
* **Pins:**
  * SCL: `seed::D11` (`PB8`, `GPIO_AF4_I2C1`)
  * SDA: `seed::D12` (`PB9`, `GPIO_AF4_I2C1`)
* **7-bit I2C Address:** **`0x3D`** (`0x7A` 8-bit write address)
* **Clock Speed:** Up to `I2C_1MHZ` (Fast Mode Plus).
* **Hardware & Driver Traps:**
  * **Persistent GDDRAM:** The display panel retains its internal image as long as 3.3V power is applied. An uninitialized or hung microcontroller leaves the previous image on screen.
  * **Bus Reset:** After soft-resetting or branching from the bootloader, ensure `__HAL_RCC_I2C1_FORCE_RESET()` and `__HAL_RCC_I2C1_RELEASE_RESET()` are invoked before initializing `I2CHandle` to clear any stalled bus transitions.

```cpp
I2CHandle::Config i2c_config;
i2c_config.periph         = I2CHandle::Config::Peripheral::I2C_1;
i2c_config.speed          = I2CHandle::Config::Speed::I2C_1MHZ;
i2c_config.pin_config.scl = seed::D11; // PB8
i2c_config.pin_config.sda = seed::D12; // PB9

OledDisplay<SSD130xI2c128x64Driver>::Config oled_config;
oled_config.driver_config.transport_config.i2c_address = 0x3D;
oled_config.driver_config.transport_config.i2c_config  = i2c_config;
```

---

## 4. Analog Inputs (ADC1: Knobs & Joysticks)

All 8 user analog controls and the auxiliary channel route into the STM32H750's `ADC1` converter.

### A. 4 Rotary Potentiometers (Knobs Across Top Panel)

Physical order from left to right:
1. **Knob 0 (Chord Vol):** `seed::D18` (`PA7` / `ADC1_INP7` / `A3`)
2. **Knob 1 (Chord Filter):** `seed::D17` (`PB1` / `ADC1_INP5` / `A2`)
3. **Knob 2 (Notes Vol):** `seed::D19` (`PA6` / `ADC1_INP3` / `A4`)
4. **Knob 3 (Notes Filter):** `seed::D20` (`PC1` / `ADC1_INP11` / `A5`)

* **Electrical Polarity:** Potentiometer wipers sweep from 3.3V (counter-clockwise) to 0V (clockwise).
* **Software Inversion:** Must be inverted in software (`value = 1.0f - raw`) so that `0.0f` = full CCW and `1.0f` = full CW.
* **Filtering:** Official firmware applies a heavy IIR smoothing filter (coefficient $\approx 0.002$) for jitter-free potentiometer reading.

### B. 2 Dual-Axis Joysticks (4 Analog Axes)

1. **Left Stick X (LX):** `seed::D22` (`PA5` / `ADC1_INP19` / `A7`)
   * *Polarity:* Standard (`raw`). Left = `0.0f`, Right = `1.0f`.
2. **Left Stick Y (LY):** `seed::D21` (`PC4` / `ADC1_INP4` / `A6`)
   * *Polarity:* Inverted (`1.0f - raw`). Down = `0.0f`, Up = `1.0f`.
3. **Right Stick X (RX):** `seed::D24` (`PA1` / `ADC1_INP17` / `A9`)
   * *Polarity:* Inverted (`1.0f - raw`). Left = `0.0f`, Right = `1.0f`.
4. **Right Stick Y (RY):** `seed::D23` (`PA4` / `ADC1_INP18` / `A8`)
   * *Polarity:* Inverted (`1.0f - raw`). Down = `0.0f`, Up = `1.0f`.

> [!NOTE]
> **Hardware Polarity Rationale:**
> Both joystick gimbals share identical Y-axis orientation relative to VCC/GND. However, the X-axis on the Right stick is electrically reversed relative to the Left stick. This is standard PCB design practice on dual-joystick layouts: reversing 3.3V and GND on one potentiometer simplifies PCB trace fanout and ground plane continuity without adding via hops.

* **Filtering:** Official firmware applies a fast, responsive IIR filter (coefficient $\approx 0.05$) to provide low-latency modulation.

### C. Auxiliary / Battery ADC

* **Aux Channel:** `seed::D31` (`PC2` / `ADC1_INP12`)
* Used for battery level monitoring or internal board power diagnostics.

---

## 5. Digital Keypad (14 Low-Profile Tactile Switches)

The Gamma synthesizer has 14 tactile keys arranged into two distinct physical grids: 7 Note keys on the left and 7 Chord keys on the right.

Each switch connects directly to a dedicated MCU GPIO with internal pull-up (`INPUT_PULLUP`).
* **Unpressed:** Pin reads `HIGH` (`1`)
* **Pressed:** Pin reads `LOW` (`0`)

### Left Keypad: 7 Note Keys (N1–N7)

```text
  [ N1 ]   [ N2 ]   [ N3 ]   [ N4 ]     <-- Top Row
  [ N5 ]   [ N6 ]   [ N7 ]              <-- Bottom Row
```

* **Top Row (Left to Right):**
  * `N1`: `seed::D1` (`PC11`)
  * `N2`: `seed::D2` (`PC10`)
  * `N3`: `seed::D3` (`PC9`)
  * `N4`: `seed::D4` (`PC8`)
* **Bottom Row (Left to Right):**
  * `N5`: `seed::D5` (`PD2`)
  * `N6`: `seed::D6` (`PC12`)
  * `N7`: `seed::D7` (`PG10`)

### Right Keypad: 7 Chord Keys (C1–C7)

```text
  [ C1 ]   [ C2 ]   [ C3 ]   [ C4 ]     <-- Top Row
  [ C5 ]   [ C6 ]   [ C7 ]              <-- Bottom Row
```

* **Top Row (Left to Right):**
  * `C1`: `seed::D8` (`PG11`)
  * `C2`: `seed::D9` (`PB4`)
  * `C3`: `seed::D10` (`PB5`)
  * `C4`: `seed::D13` (`PB6`)
* **Bottom Row (Left to Right):**
  * `C5`: `seed::D14` (`PB7`)
  * `C6`: `seed::D26` (`PD11`)
  * `C7`: `seed::D27` (`PG9`)

---

## 6. Rotary Encoder & Integrated Push Switch

The rotary dial on the front panel is connected via a 4-wire harness:
* **GND:** Shared ground wire
* **Phase A:** `seed::D15` (`PC0`, input pull-up)
* **Phase B:** `seed::D16` (`PA3`, input pull-up)
* **Push Switch:** `seed::D28` (`PA2`, active-low input pull-up; reads `LOW` when clicked)

### Quadrature Timing & Interrupt Requirements

* **Detent Resolution:** Standard mechanical encoder generating 4 Gray code transitions per detent click.
* **Polling Hazard:** Updating the 128x64 SSD1306 display via I2C blocks the CPU for $\approx 23\text{ ms}$ per frame. Polling the encoder in the main `while(1)` loop will reliably drop clicks during display refreshes.
* **Solution:** Sample the encoder in a high-frequency, jitter-free interrupt callback:
  * Either an audio DMA callback (`AudioCallback` @ 48 kHz / block size), or
  * A dedicated hardware timer (e.g., `TIM5` @ 1 kHz).

---

## 7. Board Rail & Auxiliary GPIOs

Disassembly of factory firmware revealed two auxiliary pins initialized during board setup:
* **`PC3` (Board Rail):** Configured as a GPIO Output and driven `LOW` (`0`). Acts as an auxiliary ground reference or power rail switch.
* **`seed::D0` (`PB12`):** Configured as a GPIO Input with internal pull-up.

---

## 8. USB-C Interface & VBUS Sensing Discovery

* **Port:** STM32 USB OTG FS (Full Speed, 12 Mbps)
* **Crucial Hardware Trap (`vbus_sensing_enable`):**
  * Standard `libDaisy` firmware enables hardware VBUS sensing (`vbus_sensing_enable = ENABLE`), expecting a 5V sense voltage on pin `PA9` before activating the internal D+ pullup resistor.
  * The Gamma PCB **does not route 5V VBUS to `PA9`**.
  * As a result, stock `libDaisy` applications detect "cable disconnected" and never pull up D+, preventing USB enumeration.
  * **Solution:** In [`libDaisy/src/usbd/usbd_conf.c`](../../libDaisy/src/usbd/usbd_conf.c), configure:
    ```c
    hpcd_USB_OTG_FS.Init.vbus_sensing_enable = DISABLE;
    ```
  * Additionally, ensure the `HSI48` oscillator is enabled and routed to `RCC_USBCLKSOURCE_HSI48` when launching from SRAM.
