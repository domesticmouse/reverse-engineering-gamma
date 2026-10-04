# Reverse Engineering the this.is.NOISE Gamma

Welcome to the documentation and agent skills reference for the **this.is.NOISE Gamma Mini Synth** reverse-engineering initiative.

This project reverse-engineers the hardware pinout, interfaces, and peripherals of the Gamma synthesizer in order to build an open-source Board Support Package (BSP) and support custom embedded firmware development.

The Gamma synth is powered by an embedded **Electro-Smith Daisy Seed 2 DFM** (ARM Cortex-M7 @ 480 MHz, STM32H750IBK6, PCM3060 stereo audio codec).

---

## Hardware Architecture

```mermaid
flowchart TD
    subgraph Gamma Front Panel
        K["4 Potentiometers (Knobs)\nChord/Notes Vol & Filter\nADC1"]
        J["2 Dual-Axis Joysticks\nPitch, Res, Mod, Pan\nADC1"]
        SW["14 Tactile Switches\n7 Note Keys + 7 Chord Keys\nDiscrete GPIO Pull-Up"]
        ENC["Rotary Encoder + Push Button\nQuadrature Gray Code & Select\nGPIO TIM ISR"]
        OLED["1.3\" 128x64 OLED Display\nSSD1306 Controller\nI2C1 @ 0x3D"]
    end

    subgraph Daisy Seed 2 DFM Core
        MCU["STM32H750IBK6 (ARM Cortex-M7 @ 480 MHz)\n128 KB Internal Flash | 1 MB RAM\n64 MB QSPI Flash | AXI SRAM"]
        CODEC["TI PCM3060 Stereo Codec\n24-bit / 48 kHz\nSAI1 Audio Engine"]
        USB["USB-C Interface (External HS FS)\nDFU Flashing | CDC Serial | USB MIDI"]
    end

    subgraph Audio Output
        HP["3.5mm Stereo Headphone Output"]
        AMP["Stereo Class-D Amplifier\nMute Control (seed::D32 / PC3)"]
        SPK["Stereo Case Speakers"]
    end

    K -->|Analog Channels| MCU
    J -->|Analog Channels| MCU
    SW -->|Active-Low GPIO| MCU
    ENC -->|Quadrature Signals| MCU
    MCU -->|I2C 1 MHz| OLED
    MCU <-->|SAI1 Stereo Audio| CODEC
    MCU <-->|USB-C Port| USB
    CODEC --> HP
    CODEC --> AMP
    AMP --> SPK
```

---

## Hardware Specifications

| Component | Specification | Description & Mapping |
| :--- | :--- | :--- |
| **Microcontroller** | STM32H750IBK6 | ARM Cortex-M7 running at 480 MHz with DSP and FPU |
| **System Memory** | 1 MB RAM | AXI SRAM (`0x24000000`), DTCM (`0x20000000`), SRAM1 DMA buffers |
| **Storage** | 64 MB QSPI Flash | External QSPI flash memory (`0x90040000`) for application binaries |
| **Audio Codec** | Texas Instruments PCM3060 | 24-bit stereo audio codec running via `SAI1` @ 48 kHz |
| **Display** | 1.3" Monochrome OLED | 128x64 pixels, SSD1306 controller via `I2C1` (`seed::D11`/`seed::D12`) at `0x3D` |
| **Note Keys** | 7 Discrete Tactile Switches | `N1`–`N7` on `seed::D1`–`D7` (`PC11`, `PC10`, `PC9`, `PC8`, `PD2`, `PC12`, `PG10`) |
| **Chord Keys** | 7 Discrete Tactile Switches | `C1`–`C7` on `seed::D8`–`D10`, `D13`, `D14`, `D26`, `D27` |
| **Potentiometers** | 4 Analog Knobs | 10k linear pots on `seed::D17`, `D18`, `D19`, `D20` (`ADC1`, software inverted) |
| **Joysticks** | 2 Dual-Axis Analog Sticks | Left (Pitch/Resonance) and Right (Mod/Pan) on `seed::D21`–`D24` |
| **Rotary Encoder** | Quadrature Encoder + Push | 4 transitions/detent on `seed::D15`, `D16` with push switch on `seed::D28` |
| **Speaker Amplifier** | Stereo Class-D Amp | Case speakers with hardware enable/mute line on `seed::D32` (`PC3`) |
| **USB Connectivity** | External USB-C (`USB_OTG_HS`) | Full Speed DFU flashing, USB CDC logging (`seed::D29`/`seed::D30`), and USB MIDI |

---

## Documentation Sections

Explore the key sections of this documentation suite:

<div class="grid cards" markdown>

-   :material-robot:{ .lg .middle } **[Skills Catalog](skills/index.md)**

    ---

    Curated agent skills with operational runbooks, pinout references, hardware drivers, and recovery procedures.

    [:octicons-arrow-right-24: Browse Skills](skills/index.md)

-   :material-sine-wave:{ .lg .middle } **[DaisySP Deep Dive](DAISYSP_GUIDE.md)**

    ---

    Exhaustive architectural reference for the DaisySP DSP library: synthesis engines, drum modules, filters, dynamics, and callbacks.

    [:octicons-arrow-right-24: Read DaisySP Guide](DAISYSP_GUIDE.md)

-   :material-chip:{ .lg .middle } **[libDaisy Deep Dive](LIBDAISY_GUIDE.md)**

    ---

    Comprehensive hardware abstraction guide for STM32H7: memory layout, DMA cache coherency, SAI audio engine, and peripheral drivers.

    [:octicons-arrow-right-24: Read libDaisy Guide](LIBDAISY_GUIDE.md)

-   :material-map-legend:{ .lg .middle } **[Reverse Engineering Roadmap](PLAN.md)**

    ---

    Step-by-step validation roadmap covering firmware flashing, hardware probing, non-destructive dump, and BSP construction.

    [:octicons-arrow-right-24: View Plan](PLAN.md)

</div>

---

## Quick Reference Links

* **GitHub Repository:** [domesticmouse/reverse-engineering-gamma](https://github.com/domesticmouse/reverse-engineering-gamma)
* **Master Pin Header:** [`resources/gamma_pins.h`](skills/gamma-pinout/resources/gamma_pins.h)
* **Firmware Flashing Runbook:** [gamma-firmware-flash](skills/gamma-firmware-flash/index.md)
* **Unbricking & Recovery Runbook:** [gamma-firmware-restore](skills/gamma-firmware-restore/index.md)
* **USB CDC & Python Tools:** [gamma-usb-connectivity](skills/gamma-usb-connectivity/index.md)
