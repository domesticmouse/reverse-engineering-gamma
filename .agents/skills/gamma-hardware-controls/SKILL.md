---
name: gamma-hardware-controls
description: >-
  Comprehensive guide and implementation reference for interfacing with all front-panel hardware controls
  on the this.is.NOISE Gamma Mini Synth (Daisy Seed 2 DFM): SSD1306 128x64 OLED display via I2C,
  14 discrete tactile keys (7 note + 7 chord), rotary encoder with push switch, 4 analog potentiometers (knobs),
  and 2 dual-axis joysticks (thumbsticks). Details initialization sequences, software inversion/polarities,
  sampling timers, and display blocking hazard workarounds.
---

# Gamma Hardware Controls & Peripheral Driver Reference

This skill documents how to interface with all front-panel user interface controls on the **this.is.NOISE Gamma Mini Synth**, powered by an embedded **Electro-Smith Daisy Seed 2 DFM** (ARM Cortex-M7 @ 480 MHz, STM32H750IBK6).

It synthesizes findings, drivers, and timing architectures validated in:
* [`firmware/phase3_adc`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/firmware/phase3_adc) (Analog Potentiometers, Joysticks & OLED Display)
* [`firmware/phase4_keys_encoder`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/firmware/phase4_keys_encoder) (14 Tactile Keys, Rotary Encoder & 1 kHz Timer ISR)

---

## Pin Definitions & Header Location

All hardware pin assignments, peripheral channel constants, and polarity inversion flags are centralized in the shared header provided by the [Gamma Pinout Skill](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/.agents/skills/gamma-pinout/SKILL.md):

* **Canonical Skill Header:** [`gamma-pinout/resources/gamma_pins.h`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/.agents/skills/gamma-pinout/resources/gamma_pins.h)
* **Firmware Working Copies:**
  * [`firmware/phase3_adc/gamma_pins.h`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/firmware/phase3_adc/gamma_pins.h)
  * [`firmware/phase4_keys_encoder/gamma_pins.h`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/firmware/phase4_keys_encoder/gamma_pins.h)

---

## Front-Panel Controls Summary

| Peripheral Group | Controls | Daisy Seed Pins | Bus / Interface | Software Inversion | Reference Guide |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **OLED Display** | 1.3" 128x64 Monochrome | `D11` (SCL), `D12` (SDA) | `I2C1` @ `0x3D` (1 MHz) | N/A | [Display Reference](references/display.md) |
| **Potentiometers** | 4 Top Knobs (Chord/Notes Vol & Filter) | `D18`, `D17`, `D19`, `D20` | `ADC1` (Ch 7, 5, 3, 11) | **Inverted** (`1.0 - raw`) | [Analog Inputs Reference](references/analog_inputs.md) |
| **Left Joystick** | LX (Mod/Pitch), LY (Mod/Pitch) | `D22`, `D21` | `ADC1` (Ch 19, 4) | LX: Normal, LY: **Inverted** | [Analog Inputs Reference](references/analog_inputs.md) |
| **Right Joystick**| RX (Filter/FX), RY (Filter/FX) | `D24`, `D23` | `ADC1` (Ch 17, 18) | RX: **Inverted**, RY: **Inverted** | [Analog Inputs Reference](references/analog_inputs.md) |
| **Note Keys** | 7 Tactile Switches (N1–N7) | `D1`–`D7` | Discrete GPIO w/ Pull-up | Active-Low (`0` = Pressed) | [Digital Keys Reference](references/digital_keys.md) |
| **Chord Keys** | 7 Tactile Switches (C1–C7) | `D8`–`D10`, `D13`, `D14`, `D26`, `D27` | Discrete GPIO w/ Pull-up | Active-Low (`0` = Pressed) | [Digital Keys Reference](references/digital_keys.md) |
| **Rotary Dial** | Quadrature Encoder & Push Switch | `D15` (A), `D16` (B), `D28` (Click) | GPIO Pull-up / FSM | 4 transitions / detent | [Rotary Encoder Reference](references/rotary_encoder.md) |

---

## Reference Guides Directory

Consult the detailed topical references below for full code snippets, electrical characteristics, and timing constraints:

1. [**OLED Display (SSD1306 on I2C1)**](references/display.md)
   * I2C1 force-reset sequence (`__HAL_RCC_I2C1_FORCE_RESET`) to recover from soft-reboots.
   * Fast Mode Plus (`I2C_1MHZ`) configuration and pin assignments (`PB8`/`PB9`).
   * Frame refresh budgeting and working around the ~9–10 ms blocking transmission hazard.

2. [**Analog Inputs: Knobs & Joysticks (ADC1)**](references/analog_inputs.md)
   * Multi-channel `AdcChannelConfig` setup for 8 user controls + 1 auxiliary channel (`D31`/`PC2`).
   * Potentiometer polarity sweep (3.3V CCW -> 0V CW) requiring software inversion.
   * Joystick axes breakdown and why Right Stick X is reversed on the PCB.
   * Deadband filtering for knobs vs. responsive exponential smoothing (`iir = 0.05f`) for joysticks.

3. [**Digital Keypad: 14 Tactile Switches**](references/digital_keys.md)
   * Physical layout: 7 Note keys (left grid) and 7 Chord keys (right grid).
   * Active-low electrical configuration with internal pull-ups (`INPUT_PULLUP`).
   * Fast 1 kHz debouncing via `daisy::Switch` (8–10 ms response time).
   * Lock-free ring buffer event queue for transferring key events to the main thread.

4. [**Rotary Encoder & Integrated Push Switch**](references/rotary_encoder.md)
   * Quadrature Gray code lookup table decoding 4 state transitions per mechanical detent click.
   * Why polling the encoder in the main loop drops pulses during OLED refreshes.
   * 1 kHz hardware timer configuration (`TIM5` @ 240 MHz APB1 clock).

5. [**Complete Integration Architecture & Main Loop Blueprint**](references/main_loop_blueprint.md)
   * End-to-end firmware boilerplate integrating all controls into a non-blocking architecture.
   * Initialization sequence: speaker mute (`PC3` driven `LOW` during boot, unmuted `HIGH` after audio start), display reset, ADC start, and timer start.
   * Main loop event processing, encoder delta dispatch, analog polling, and throttled display rendering.

---

## Diagnostic Checklist & Troubleshooting

| Symptom | Cause | Solution Reference |
| :--- | :--- | :--- |
| **OLED blank or frozen on reboot** | I2C bus locked up by previous transfer | [Display Bus Reset](references/display.md#2-bus-reset-sequence--gpio-initialization) |
| **Knob values decrease when turned CW** | Potentiometer wiper sweeps from 3.3V at CCW | [Knob Inversion](references/analog_inputs.md#2-electrical-polarities--inversion-details) |
| **Right stick X moves opposite to Left stick** | PCB trace routing reversed on right stick | [Stick Polarities](references/analog_inputs.md#2-electrical-polarities--inversion-details) |
| **Rotary encoder misses clicks while turning** | Main loop blocked by `oled.Update()` (9–23 ms) | [1 kHz Timer Architecture](references/rotary_encoder.md#3-the-display-blocking-hazard--1-khz-timer-isr) |
| **Keys feel sluggish or drop quick presses** | Low polling rate in main loop | [1 kHz Key Debounce](references/digital_keys.md#2-key-debouncing--1-khz-sampling-architecture) |
| **Internal case speakers silent** | Pin `PC3` held LOW or uninitialized | [Main Loop Blueprint (Speaker Amp)](references/main_loop_blueprint.md#2-integrated-implementation-skeleton) |
