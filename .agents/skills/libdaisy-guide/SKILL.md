---
name: libdaisy-guide
description: >-
  Comprehensive guide and architectural reference for developing embedded audio firmware
  with libDaisy on the Electro-Smith Daisy platform (ARM Cortex-M7 @ 480 MHz, STM32H750).
  Use this skill whenever writing audio callbacks, implementing DSP synthesis, configuring
  on-chip peripherals (ADC, DAC, GPIO, I2C, SPI, UART, SAI, QSPI), debugging STM32H7 memory
  sections (DTCM, SRAM1 DMA, SDRAM), integrating with the Daisy Bootloader, or troubleshooting
  audio glitches, cache incoherency, and build failures.
---

# libDaisy Developer Guide & Architecture Reference

This skill provides a comprehensive operational guide and architectural reference for developing embedded audio firmware using **`libDaisy`**, the official C++ hardware abstraction library and board support package (BSP) for the **Electro-Smith Daisy** platform.

---

## 1. System & Hardware Architecture

`libDaisy` targets the **STM32H750IBK6** high-performance microcontroller and standard Daisy hardware peripherals:

| Component | Hardware Specification | libDaisy Support |
| :--- | :--- | :--- |
| **MCU Core** | ARM Cortex-M7 @ 480 MHz with FPU (double/single precision) & DSP extensions | CMSIS-DSP, CMSIS Core, LL / HAL drivers |
| **Internal Flash** | 128 KB User Flash (`0x08000000`) | Base bootloader location (`APP_TYPE = BOOT_NONE`) |
| **Internal RAM** | 1 MB total: AXI SRAM (512 KB), DTCM (128 KB), SRAM1–3 (288 KB), SRAM4 (64 KB) | Fast DSP state (`DTCM`), DMA buffers (`SRAM1`) |
| **External SDRAM**| 64 MB 32-bit SDRAM (AS4C32M16SB) via FMC bus @ ~100–120 MHz | Large audio buffers / delay lines (`DSY_SDRAM_BSS`) |
| **External Flash**| 8 MB QSPI NOR Flash (IS25LP064A) via Quad-SPI | Daisy Bootloader app storage (`APP_TYPE = BOOT_SRAM`) & presets |
| **Audio Codec** | 24-bit stereo up to 96 kHz (AK4556, WM8731, PCM3060, TAC5242) | Interrupt double-buffered DMA via SAI1 |
| **USB** | USB 2.0 Full-Speed (12 Mbps) or High-Speed with external ULPI | USB CDC (Serial), USB MIDI, USB Audio, USB Host |

---

## 2. Codebase Organization & Prefix Taxonomy

The `libDaisy` source tree ([`libDaisy/src/`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/)) uses standardized module prefixes:

| Prefix | Subsystem Domain | Description & Key Classes |
| :--- | :--- | :--- |
| **`sys/`** | **System Infrastructure** | Clocks, MPU, D-cache invalidation, DMA routing, FatFs.<br>• [`System`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/sys/system.h), [`DmaHandler`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/sys/dma.h), [`FatFSInterface`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/sys/fatfs.h) |
| **`per/`** | **Internal Peripherals** | High-performance C++ drivers for STM32 on-chip peripherals.<br>• [`AdcHandle`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/per/adc.h), [`DacHandle`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/per/dac.h), [`GPIO`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/per/gpio.h), [`I2CHandle`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/per/i2c.h), [`SpiHandle`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/per/spi.h), [`SaiHandle`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/per/sai.h), [`UartHandler`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/per/uart.h), [`QSPIHandle`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/per/qspi.h), [`TimerHandle`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/per/tim.h), [`Pwm`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/per/pwm.h), [`Random`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/per/rng.h) |
| **`dev/`** | **External Devices** | External ICs mounted on boards.<br>• Codecs: [`Ak4556`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/codec_ak4556.h), [`Wm8731`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/codec_wm8731.h), [`Pcm3060`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/codec_pcm3060.h)<br>• Displays: [`SSD130x`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/oled_ssd130x.h), [`SH1106`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/oled_sh1106.h), [`SSD1327`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/oled_ssd1327.h), [`SSD1351`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/oled_ssd1351.h)<br>• Memory & Expanders: [`SdramHandle`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/sdram.h), [`IS25LP064A`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/flash_IS25LP064A.h), [`MCP23x17`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/mcp23x17.h), [`TCA9534`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/tca9534.h), [`ShiftRegister595`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/sr_595.h), [`ShiftRegister4021`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/sr_4021.h)<br>• Sensors & LEDs: [`Mpr121`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/mpr121.h), [`APDS9960`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/apds9960.h), [`NeoPixel`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/neopixel.h), [`DotStar`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/dev/dotstar.h) |
| **`hid/`** | **Human Interface** | Real-time audio engine, smoothed inputs, debounce, encoders, MIDI, USB, and display drawing.<br>• [`AudioHandle`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/audio.h), [`AnalogControl`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/ctrl.h), [`Parameter`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/parameter.h), [`Switch`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/switch.h), [`Switch3`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/switch3.h), [`Encoder`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/encoder.h), [`GateIn`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/gatein.h), [`Led`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/led.h), [`RgbLed`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/rgb_led.h), [`MidiHandler`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/midi.h), [`UsbHandle`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/usb.h), [`UsbHostHandle`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/usb_host.h), [`Logger`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/logger.h), [`Display`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/disp/display.h), [`OledDisplay`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/hid/disp/oled_display.h) |
| **`ui/`** | **User Interface** | Screen menu hierarchies, event queues, and input monitoring.<br>• [`UI`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/ui/UI.h), [`UiEventQueue`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/ui/UiEventQueue.h), [`AbstractMenu`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/ui/AbstractMenu.h), [`FullScreenItemMenu`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/ui/FullScreenItemMenu.h), [`ButtonMonitor`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/ui/ButtonMonitor.h) |
| **`util/`** | **Utilities** | Lock-free FIFOs, string formatting without heap allocation, WAV streaming, preset saving, and load profiling.<br>• [`FIFO`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/util/FIFO.h), [`FixedCapStr`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/util/FixedCapStr.h), [`WavPlayer`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/util/WavPlayer.h), [`WavWriter`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/util/WavWriter.h), [`WaveTableLoader`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/util/WaveTableLoader.h), [`PersistentStorage`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/util/PersistentStorage.h), [`CpuLoadMeter`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/util/CpuLoadMeter.h), [`VoctCalibration`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/util/VoctCalibration.h) |

---

## 3. Core Audio Engine (`AudioHandle`)

The audio engine processes audio blocks using double-buffered circular DMA via SAI1.

### 3.1 Audio Callback Formats

```cpp
#include "daisy_seed.h"
using namespace daisy;

DaisySeed hw;

// Non-interleaved (Preferred): out[channel][sample]
void AudioCallback(AudioHandle::InputBuffer in, 
                   AudioHandle::OutputBuffer out, 
                   size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        out[0][i] = in[0][i]; // Left
        out[1][i] = in[1][i]; // Right
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(48); // 1 ms @ 48 kHz
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.StartAudio(AudioCallback);

    while (1) {}
}
```

* **Sample format:** 32-bit floating point (`float`) normalized in the range `[-1.0f, +1.0f]`.
* **Block sizes:** 1 to 256 samples (powers of 2 or multiples of 48).
* **Sample rates:** 8 kHz, 16 kHz, 32 kHz, 44.1 kHz, 48 kHz, 96 kHz.

---

## 4. Memory Layout & Real-Time Constraints

### 4.1 STM32H7 Memory Sections

| Memory Region | Address | Size | Attribute / Macro | Usage & Caveats |
| :--- | :--- | :--- | :--- | :--- |
| **ITCM RAM** | `0x00000000` | 64 KB | `.itcmram` | Zero-wait-state instruction RAM. |
| **DTCM RAM** | `0x20000000` | 128 KB | `DTCM_MEM_SECTION` | 480 MHz zero-wait data RAM for DSP state. **No DMA access.** |
| **AXI SRAM** | `0x24000000` | 512 KB | Standard RAM (`.bss`, `.data`) | Primary application RAM (`BOOT_SRAM`). Cached. |
| **SRAM1 (D2)**| `0x30000000`| 128 KB | `DMA_BUFFER_MEM_SECTION` | **Non-cached.** Mandatory for all ADC, SAI, SPI, I2C DMA buffers. |
| **SDRAM** | `0xC0000000` | 64 MB | `DSY_SDRAM_BSS` | External delay buffers. No C++ static constructors before `hw.Init()`. |
| **QSPI Flash** | `0x90000000` | 8 MB | `BOOT_QSPI` / Memory-mapped | Non-volatile code storage & presets (`PersistentStorage`). |

### 4.2 Critical Real-Time Rules

1. **DMA Cache Incoherency Hazard:** The Cortex-M7 data cache does not snoop peripheral DMA buses. Any buffer targeted by DMA **must** use `DMA_BUFFER_MEM_SECTION`.
2. **Never Block Inside `AudioCallback`:** The audio callback runs inside an ISR. Never execute I2C/SPI display refreshes (`oled.Update()`), flash operations, SD card reads, or delays (`System::Delay()`) inside the audio callback.
3. **SDRAM Constructor Hazard:** Global objects in SDRAM cannot have dynamic C++ constructors, because static constructors run prior to `main()` before the SDRAM controller is initialized.

---

## 5. Bootloader & Deployment Workflows

Configure your project's `Makefile` with the appropriate `APP_TYPE`:

```makefile
TARGET = my_synth
APP_TYPE = BOOT_SRAM

CPP_SOURCES = main.cpp
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

include $(LIBDAISY_DIR)/core/Makefile
```

| `APP_TYPE` | Target Memory | Max Size | Flashing Command |
| :--- | :--- | :--- | :--- |
| **`BOOT_SRAM`** *(Recommended)* | QSPI (`0x90040000`) -> AXI SRAM (`0x24000000`) | ~480 KB | `make program-dfu` |
| **`BOOT_QSPI`** | Direct QSPI Flash execution | ~8 MB | `make program-dfu` |
| **`BOOT_NONE`** | Internal Flash (`0x08000000`) | 128 KB | `make program-dfu` or OpenOCD |

### 5.1 Build Verification & Flashing

```bash
# 1. Compile firmware and verify memory region allocations:
make clean && make

# 2. Flash application to device via USB DFU:
make program-dfu
```

To install the Daisy Bootloader onto a fresh board:
```bash
make program-boot
```

---

## 6. Common Peripheral Patterns

### 6.1 Analog Potentiometer with Curve Mapping

```cpp
#include "daisy_seed.h"
using namespace daisy;

DaisySeed     hw;
AnalogControl knob;
Parameter     cutoff_param;

int main(void)
{
    hw.Init();

    AdcChannelConfig adc_cfg;
    adc_cfg.InitSingle(seed::A0);
    hw.adc.Init(&adc_cfg, 1);
    hw.adc.Start();

    knob.Init(hw.adc.GetPtr(0), hw.AudioCallbackRate());
    cutoff_param.Init(knob, 20.0f, 20000.0f, Parameter::Curve::EXPONENTIAL);

    while (1)
    {
        System::Delay(1);
        knob.Process();
        float cutoff_hz = cutoff_param.Process();
    }
}
```

### 6.2 Debounced Switch

```cpp
Switch btn;
// Pin, update rate in Hz, momentary/latching, normal/inverted (active LOW)
btn.Init(seed::D0, 1000.0f, Switch::Type::TYPE_MOMENTARY, Switch::Polarity::POLARITY_INVERTED);

// Inside main loop (1 kHz):
btn.Debounce();
if (btn.RisingEdge())
{
    // Button just pressed
}
```

### 6.3 USB CDC Serial Logging

> [!CAUTION]
> **libDaisy Logger Blocking Hazard:** `Logger<LOGGER_EXTERNAL>` starts in non-blocking mode, but automatically transitions to synchronous blocking mode (`LOGGER_SYNC_IN`) after successfully transmitting 2 packets. Once in sync mode, `TransmitSync()` runs `while(false == impl_.Transmit) {}` with **no timeout**. If a host terminal or test script disconnects or stops reading, any subsequent log line in the main loop spins indefinitely in this loop. Because the main thread freezes while the audio DMA interrupt continues, **the synth locks up with a note stuck playing continuously**.

> [!IMPORTANT]
> **Gamma:** the USB-C jack is wired to the Seed's *external* USB pins (`D29`/`D30`). `hw.StartLog()` / `hw.PrintLine()` target the internal port and never enumerate. Always use `UsbHandle::FS_EXTERNAL` and a **deadlock-immune non-blocking logger** with a microsecond-level timeout (`System::GetUs()`) and failure bypass (see `gamma-pinout` skill §8 and `firmware/phase5_audio/main.cpp`). Never print from a USB receive callback (ISR deadlock).

```cpp
hw.Init();
UsbLog::StartLog(false); // Custom non-blocking logger on FS_EXTERNAL

while (1)
{
    System::Delay(500);
    UsbLog::PrintLine("Daisy running, Tick: %d", System::GetTick());
}
```

### 6.4 Non-Volatile Preset Storage (`PersistentStorage`)

```cpp
struct MyConfig
{
    float volume;
    int   channel;
    bool operator==(const MyConfig& o) const { return volume == o.volume && channel == o.channel; }
    bool operator!=(const MyConfig& o) const { return !(*this == o); }
};

PersistentStorage<MyConfig> storage(hw.qspi);
MyConfig defaults = { 0.8f, 1 };

int main(void)
{
    hw.Init();
    storage.Init(defaults);
    MyConfig& current = storage.GetSettings();
    current.channel = 3;
    storage.Save(); // Writes to QSPI flash wear-leveled sector
}
```

---

## 7. Troubleshooting & Diagnostics

| Symptom | Probable Cause | Corrective Action |
| :--- | :--- | :--- |
| **Audio glitches / crackling** | Callback overrun or blocking call in ISR | Use `CpuLoadMeter` to measure budget. Move all I2C, SPI, OLED, and flash operations to the main `while(1)` loop. |
| **Synth freezes with note stuck playing** | `libDaisy`'s `Logger` entered blocking `TransmitSync` after USB host closed/disconnected | Replace `Logger<LOGGER_EXTERNAL>` with a non-blocking timeout-guarded logger (`System::GetUs()` < 500 us timeout). See §6.3. |
| **ADC / SPI data corrupted or stale** | CPU D-cache reading stale memory | Decorate DMA buffers with `DMA_BUFFER_MEM_SECTION` (places them in non-cached SRAM1). |
| **HardFault on startup** | Accessing uninitialized SDRAM or DMA from DTCM | Ensure global SDRAM arrays use `DSY_SDRAM_BSS` without static constructors. Do not route DMA to `DTCM_MEM_SECTION`. |
| **Firmware rejected by bootloader** | Mismatched linker script / vector table | Verify `APP_TYPE = BOOT_SRAM` is set in the `Makefile`. |
| **I2C OLED hangs on soft reset** | SDA line held LOW by display from previous run | Force reset I2C peripheral (`__HAL_RCC_I2C1_FORCE_RESET`) and clock out dummy SCL pulses before initializing. |

---

## 8. Cross-Reference Documentation

* Detailed Architecture Guide: [`docs/LIBDAISY_GUIDE.md`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/docs/LIBDAISY_GUIDE.md)
* libDaisy Source Tree: [`libDaisy/src/`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/src/)
* libDaisy Examples: [`libDaisy/examples/`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/libDaisy/examples/)
