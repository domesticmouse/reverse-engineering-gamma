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
| **USB Interface** | **External** USB port: `USB_OTG_HS` in Full Speed mode on `seed::D29`/`seed::D30` (`PB14`/`PB15`). Use `UsbHandle::FS_EXTERNAL` / `Logger<LOGGER_EXTERNAL>` — **not** `hw.StartLog()` |

---

## 2. Master Pinout Table

| Peripheral | Label | Daisy Seed Pin | STM32 Pin | Function / Channel | Electrical Config | Polarity / Logic | Verification Status |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **OLED Display** | SCL | `seed::D11` | `PB8` | `I2C1_SCL` | Alternate Function 4 | Open-drain w/ pullup | Verified on Hardware |
| **OLED Display** | SDA | `seed::D12` | `PB9` | `I2C1_SDA` | Alternate Function 4 | Open-drain w/ pullup | Verified on Hardware |
| **Potentiometer** | Knob 1 / K0 (Chord Vol) | `seed::D18` | `PA7` | `ADC1_INP7` (`A3`) | Analog Input | Inverted: `1.0 - raw` | Verified on Hardware |
| **Potentiometer** | Knob 2 / K1 (Chord Filter) | `seed::D17` | `PB1` | `ADC1_INP5` (`A2`) | Analog Input | Inverted: `1.0 - raw` | Verified on Hardware |
| **Potentiometer** | Knob 3 / K2 (Notes Vol) | `seed::D19` | `PA6` | `ADC1_INP3` (`A4`) | Analog Input | Inverted: `1.0 - raw` | Verified on Hardware |
| **Potentiometer** | Knob 4 / K3 (Notes Filter) | `seed::D20` | `PC1` | `ADC1_INP11` (`A5`) | Analog Input | Inverted: `1.0 - raw` | Verified on Hardware |
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
| **Speaker Amp En** | Speaker Enable/Mute | `seed::D32` (`seed::A13`) | `PC3` | GPIO Out | Output Push-Pull | Active HIGH (`1`=On, `0`=Muted) | Verified on Hardware |
| **Power Fault Sense** | Battery Low / Power Fault | `seed::D0` | `PB12` | GPIO In | Internal Pull-up | Active LOW ("Charge Me!") | Confirmed Disassembly |
| **Audio Out** | Stereo DAC Out | Internal | Multiple | `SAI1` | PCM3060 Codec | 48 kHz / 24-bit Stereo | Internal Daisy routing |
| **MIDI** | USB-C MIDI (Class Compliant) | `seed::D29`/`seed::D30` | `PB14`/`PB15` | `USB_OTG_HS` (FS mode) | Full Speed USB Device | `MidiUsbHandler` with `MidiUsbTransport::Config::EXTERNAL` (no 3.5mm MIDI) | Not yet implemented |
| **USB-C** | D- / D+ / Power | `seed::D29`/`seed::D30` | `PB14`/`PB15` | `USB_OTG_HS` (FS mode) | Full Speed Device | `UsbHandle::FS_EXTERNAL` | Verified on Hardware (CDC) |

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

## 7. Speaker Amplifier Enable & System Power GPIOs

* **`seed::D32` / `PC3` (Speaker Amplifier Enable / Mute):**
  * Defined in `libDaisy` as `seed::D32` (and alias `seed::A13` on Daisy Seed 2 DFM).
  * Configured as a GPIO Output push-pull.
  * Controls the shutdown/enable pin of the on-board Class-D speaker amplifier driving the internal case speakers.
  * **Polarity:** Active HIGH.
    * `0` (`LOW`): Speaker amplifier in shutdown / muted (`SPK OFF`).
    * `1` (`HIGH`): Speaker amplifier enabled (`SPK ON`).
  * **Anti-Pop Initialization Sequence:** Hold `PC3` LOW (`0`) during boot and peripheral setup. After starting the audio engine (`hw.StartAudio()`), delay $\approx 60\text{ ms}$ to allow DAC bias voltages to stabilize before driving `PC3` HIGH (`1`).
* **`seed::D0` (`PB12` - Power Fault / Battery Low Monitor):**
  * Configured as a GPIO Input with internal pull-up.
  * Monitored by the main loop: when pulled LOW (`0`), factory firmware displays `"Charge Me!"` and halts audio output.

---

## 8. USB-C Interface & USB MIDI

* **Port:** The USB-C connector is wired to the Seed's **external** USB pins: `seed::D29` (`PB14`, D-) and `seed::D30` (`PB15`, D+), driven by `USB_OTG_HS` in Full Speed (12 Mbps) mode. The internal `PA11`/`PA12` (`USB_OTG_FS`) port is **not connected** to the jack.
  * *Hardware evidence:* with CDC on `FS_INTERNAL`, `USB_OTG_FS` was powered, B-valid overridden and D+ pulled up (`DCTL.SDIS = 0`), yet `DSTS` frame number stayed `0` (no SOF from the host) and macOS never enumerated it. Switching to `FS_EXTERNAL` enumerates immediately as `Daisy Seed External` (`0483:5740`).
* **USB CDC Serial (diagnostics):** `DaisySeed::StartLog()` / `hw.PrintLine()` are hard-wired to the internal port and will never appear on the host. Always initialize CDC on `UsbHandle::FS_EXTERNAL`.
* **CRITICAL: The `Logger<LOGGER_EXTERNAL>` Deadlock Hazard:**
  * `daisy::Logger<LOGGER_EXTERNAL>` starts in non-blocking mode (`LOGGER_SYNC_OUT`). However, as soon as it successfully transmits 2 packets to a host terminal, it permanently switches to synchronous blocking mode (`LOGGER_SYNC_IN`).
  * In blocking mode, `TransmitSync()` runs `while(false == impl_.Transmit) {}` with **no timeout**.
  * When a host terminal or script closes the port or stops reading, `CDC_Transmit_HS()` returns `USBD_BUSY` forever. Any subsequent log call in the main loop (e.g., when a key is pressed) enters an **infinite loop**, freezing the main loop.
  * Because the main loop freezes while the audio DMA interrupt keeps running, **the synth locks up with a note stuck playing continuously**!
* **Deadlock-Immune Non-Blocking Logger Pattern:**
  Replace `Logger<LOGGER_EXTERNAL>` with a custom timeout-guarded logger using `hw.usb_handle.TransmitExternal()`:
  ```cpp
  struct UsbLog
  {
      static void StartLog(bool wait_for_pc = false)
      {
          (void)wait_for_pc;
          hw.usb_handle.Init(UsbHandle::FS_EXTERNAL);
      }

      static void PrintLine(const char* format, ...)
      {
          va_list args;
          va_start(args, format);
          LogInternal(true, format, args);
          va_end(args);
      }

  private:
      static void LogInternal(bool newline, const char* format, va_list args)
      {
          static constexpr size_t kNumBufs = 4;
          static constexpr size_t kBufSize = 256;
          static char   s_tx_bufs[kNumBufs][kBufSize];
          static size_t s_cur_buf         = 0;
          static uint32_t s_last_timeout_ms = 0;

          char* buf = s_tx_bufs[s_cur_buf];
          int   len = vsnprintf(buf, kBufSize - 3, format, args);
          if(len <= 0) return;
          if(len > (int)(kBufSize - 3)) len = kBufSize - 3;
          if(newline) { buf[len++] = '\r'; buf[len++] = '\n'; buf[len] = '\0'; }

          uint32_t now_ms = System::GetNow();
          // Fast-fail bypass if recently timed out to avoid stacking delays
          if(s_last_timeout_ms > 0 && (now_ms - s_last_timeout_ms < 200))
          {
              if(hw.usb_handle.TransmitExternal((uint8_t*)buf, len) == UsbHandle::Result::OK)
              {
                  s_last_timeout_ms = 0;
                  s_cur_buf         = (s_cur_buf + 1) % kNumBufs;
              }
              return;
          }

          // Allow up to 500 us for an in-flight packet to complete
          uint32_t start_us = System::GetUs();
          while(hw.usb_handle.TransmitExternal((uint8_t*)buf, len) != UsbHandle::Result::OK)
          {
              if(System::GetUs() - start_us >= 500)
              {
                  s_last_timeout_ms = System::GetNow();
                  return; // Drop message safely; NEVER hang the synth!
              }
          }
          s_last_timeout_ms = 0;
          s_cur_buf         = (s_cur_buf + 1) % kNumBufs;
      }
  };
  ```
* **Never print from the USB receive callback:** `UsbRxCallback` runs in the USB interrupt. Queue received bytes into a ring buffer and process commands in the main loop (see `firmware/phase5_audio/main.cpp`). Support the `'b'` command for automated flashing by calling `System::ResetToBootloader(System::DAISY_INFINITE_TIMEOUT)`. Do **not** call it with no argument: that defaults to the STM32 ROM bootloader, which cannot program the QSPI app slot at `0x90040000`.
* **MIDI Implementation:** 
  * The Gamma Mini Synth has **no hardware 3.5mm TRS MIDI port**. All MIDI communication is handled over the USB-C connector as a class-compliant USB MIDI device.
  * In libDaisy, use `daisy::MidiUsbHandler` with `midi_cfg.transport_config.periph = daisy::MidiUsbTransport::Config::EXTERNAL`. No discrete GPIO configuration is required; libDaisy configures `PB14`/`PB15` for `USB_OTG_HS`.
* **VBUS sensing:** Stock libDaisy already sets `hpcd_USB_OTG_HS.Init.vbus_sensing_enable = DISABLE`, so the external port works with **unmodified** libDaisy. (An earlier local patch disabling VBUS sensing on `hpcd_USB_OTG_FS` only affected the unconnected internal port and has been reverted.)
* **USB clock:** The Gamma's Daisy Bootloader reports as v6.1+ (`System::GetBootloaderVersion()` = `3` = `Version::v6_1`), so `DaisySeed::Init()` performs full clock configuration, including `HSI48` routed to `RCC_USBCLKSOURCE_HSI48`. No application-level clock setup is needed.
