---
name: gamma-usb-connectivity
description: >-
  Comprehensive guide and implementation reference for USB connectivity on the this.is.NOISE
  Gamma Mini Synth (Electro-Smith Daisy Seed 2 DFM): USB CDC Virtual COM Port configuration,
  deadlock-immune non-blocking logging, bidirectional host-synth command dispatch, Python host
  communication scripts, and firmware integration examples.
---

# Gamma USB Connectivity Reference

This skill documents how to interface with the **this.is.NOISE Gamma Mini Synth** over USB using its embedded **Electro-Smith Daisy Seed 2 DFM** (ARM Cortex-M7 @ 480 MHz, STM32H750IBK6).

It includes:
1. **Hardware & USB CDC Architecture**: Device identifiers, Full-Speed peripheral configuration, and host port enumeration.
2. **Deadlock Immunity & Real-Time Audio Protection**: Eliminating the blocking hazards inherent in standard libDaisy USB logging.
3. **Firmware Sample Code**: Drop-in C++ driver [`gamma_usb.h`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/.agents/skills/gamma-usb-connectivity/resources/gamma_usb.h) and implementation patterns.
4. **Host Python Communication Script**: Zero-dependency cross-platform utility [`gamma_usb.py`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/.agents/skills/gamma-usb-connectivity/scripts/gamma_usb.py) for testing, interactive control, monitoring, and DFU bootloader rebooting.

---

## 1. Hardware & Peripheral Overview

The Gamma Mini Synth routes the Daisy Seed 2 DFM's Full-Speed USB peripheral to the external USB port on the chassis.

* **Daisy Peripheral ID**: `UsbHandle::FS_EXTERNAL`
* **Protocol**: USB CDC ACM (Communications Device Class / Virtual COM Port)
* **USB Speed**: Full-Speed (12 Mbps)
* **USB Vendor ID (VID)**: `0x0483` (STMicroelectronics / Electrosmith)
* **USB Product ID (PID)**: `0x5740` (Virtual COM Port)
* **USB Product String**: `"Daisy Seed External"`
* **USB Manufacturer**: `"Electrosmith"`
* **Default Baud Rate**: `115200` (virtual; physical throughput runs at full USB FS packet speed)

### Host Port Identification

| Operating System | Device Path Pattern | Example |
| :--- | :--- | :--- |
| **macOS** | `/dev/cu.usbmodem<serial>` | `/dev/cu.usbmodem3067366D34331` |
| **Linux** | `/dev/ttyACM<n>` or `/dev/serial/by-id/*Daisy*` | `/dev/ttyACM0` |
| **Windows** | `COM<n>` | `COM5` |

---

## 2. Embedded Real-Time Constraints & Deadlock Hazards

When integrating USB CDC serial on real-time synthesis firmware running SAI audio interrupts (e.g. 48 kHz, block size 48), standard blocking patterns can crash or stall the firmware.

### Hazard 1: libDaisy `Logger<LOGGER_EXTERNAL>` Infinite Spinloop
libDaisy's built-in `daisy::Logger<LOGGER_EXTERNAL>` uses `TransmitSync()`. If a host terminal is disconnected, closed, or stops reading the USB CDC IN endpoint:
* `TransmitSync()` enters an infinite `while(1)` waiting for the USB TX-complete flag.
* The main loop freezes indefinitely.
* User controls (knobs, keys, OLED) lock up, and while audio interrupts may continue in hardware, no control updates or state changes can occur.

### Hazard 2: USB Interrupt Deadlock via `UsbRxCallback`
When the host transmits a byte to the synth, libDaisy invokes the registered `UsbRxCallback` in the **USB ISR context**:
* Calling any blocking log function (like `hw.PrintLine` or `TransmitSync`) from within `UsbRxCallback` causes an immediate, permanent deadlock: the TX-complete interrupt cannot fire while still servicing the RX interrupt.
* **Rule**: The USB RX callback must **never** format or transmit data. It must only append incoming bytes into a lock-free ring buffer for consumption by the main thread.

### The Solution: Non-Blocking Multi-Buffer Pool with Timeout & Backoff
The [`gamma_usb.h`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/.agents/skills/gamma-usb-connectivity/resources/gamma_usb.h) driver resolves this with:
1. **Multi-Buffer TX Pool**: Rotates across 4 static buffers to avoid clobbering in-flight packets.
2. **500 µs Maximum Wait**: If the host is not actively polling, TX drops after 500 µs rather than stalling the frame loop.
3. **200 ms Fast-Fail Backoff**: If a transmission times out, subsequent print calls try once without waiting for 200 ms, preventing delay accumulation when no terminal is connected.
4. **Lock-Free RX Queue**: Interrupt-safe single-producer single-consumer ring buffer processed in `UsbComm::Process()`.

---

## 3. Firmware Integration & Sample Code

### Drop-In Header
Copy [`gamma_usb.h`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/.agents/skills/gamma-usb-connectivity/resources/gamma_usb.h) into your firmware directory:

```cpp
#include "daisy_seed.h"
#include "gamma_usb.h"

using namespace daisy;
using namespace gamma_usb; // NOT `gamma`: that name clashes with libm's gamma() function

static DaisySeed hw;
static GPIO      spk_en; // PC3 speaker amp enable

// Dispatch host ASCII commands
void MyCommandHandler(char c)
{
    switch(c)
    {
        case 'm':
        case 'M':
            UsbComm::PrintLine("[CMD] Mode toggled!");
            break;

        case 'w':
        case 'W':
            UsbComm::PrintLine("[CMD] Waveform cycled!");
            break;

        case 'h':
        case '?':
            UsbComm::PrintLine("\n--- Gamma Synth USB Commands ---");
            UsbComm::PrintLine("  m : Toggle mode");
            UsbComm::PrintLine("  w : Cycle waveform");
            UsbComm::PrintLine("  b : Reboot into DFU bootloader");
            UsbComm::PrintLine("  h : Print this help\n");
            break;

        default:
            break;
    }
}

// Optional: runs just before the reboot into the bootloader
void BeforeBootloader()
{
    spk_en.Write(false); // Mute the speaker amp to avoid a pop
    hw.StopAudio();
    // ...draw a "DFU MODE" screen here: the OLED keeps its last frame in the bootloader
}

int main(void)
{
    hw.Init();

    // Initialize USB CDC communications and register command callback
    UsbComm::Init(hw, MyCommandHandler);
    UsbComm::SetBootloaderHook(BeforeBootloader);
    UsbComm::PrintLine("Gamma Synth Initialized. Ready for commands.");

    while(1)
    {
        // Must be called periodically in the main loop to dispatch queued commands
        UsbComm::Process();

        // Application tasks (display update, control sampling, etc.)
        System::Delay(1);
    }
}
```

> [!NOTE]
> The `'b'` (bootloader) command is intercepted automatically inside `UsbComm::Process()`, which calls `UsbComm::RebootToBootloader()`. You can also call that from your own code, e.g. on a long press of the encoder. It runs the optional bootloader hook and then calls `System::ResetToBootloader(System::DAISY_INFINITE_TIMEOUT)`, so you can reflash over USB without pressing the physical RESET + BOOT buttons. The Daisy bootloader then waits until new firmware arrives.

> [!WARNING]
> Never call `System::ResetToBootloader()` with no argument on the Gamma. It defaults to `BootloaderMode::STM`, the STM32 ROM bootloader. That also shows up as `0483:df11`, but it can only program internal flash, so flashing a `BOOT_SRAM` app to `0x90040000` fails. Always pass a `DAISY_*` mode.

> [!NOTE]
> The header is C++14-compatible (libDaisy builds with `-std=gnu++14`) and can be included from multiple `.cpp` files.

---

## 4. Python Host Tool: `gamma_usb.py`

The repository provides a standalone Python script located at:
[`gamma_usb.py`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/.agents/skills/gamma-usb-connectivity/scripts/gamma_usb.py)

It requires **no external packages** (uses POSIX `termios`/`select` on macOS and Linux) and automatically falls back to `pyserial` if installed.

### Common Commands

#### 1. Automated Bidirectional Self-Test
Runs a diagnostic verification test sending commands, checking responses, and measuring round-trip integrity:
```bash
python3 .agents/skills/gamma-usb-connectivity/scripts/gamma_usb.py --test
```

#### 2. Send Specific Commands
Send a single ASCII command (e.g. `'h'` for help menu, `'w'` for waveform cycle):
```bash
python3 .agents/skills/gamma-usb-connectivity/scripts/gamma_usb.py --cmd "h"
python3 .agents/skills/gamma-usb-connectivity/scripts/gamma_usb.py --cmd "w"
```

#### 3. Monitor Serial Stream
Listen to real-time firmware logs, diagnostic telemetry, and event messages:
```bash
python3 .agents/skills/gamma-usb-connectivity/scripts/gamma_usb.py --monitor
```

#### 4. Interactive Terminal Mode
Provides a raw bidirectional serial terminal where keystrokes are transmitted instantly and synth responses are rendered live:
```bash
python3 .agents/skills/gamma-usb-connectivity/scripts/gamma_usb.py --interactive
```
*(Press `Ctrl+C` or `Ctrl+]` to exit).*

#### 5. Remote DFU Bootloader Reboot
Instructs the firmware over USB to reboot into the **Daisy** DFU bootloader (not the STM32 ROM bootloader) for immediate flashing with `dfu-util` or `flash_firmware.py`:
```bash
python3 .agents/skills/gamma-usb-connectivity/scripts/gamma_usb.py --bootloader
```

---

## 5. Diagnostic & Troubleshooting Checklist

| Symptom | Probable Cause | Corrective Action |
| :--- | :--- | :--- |
| **Port `/dev/cu.usbmodem*` not appearing** | USB cable is power-only or device is in DFU mode | Verify data cable. Run `ioreg -p IOUSB -w0 -l` to check if `Daisy Seed External` (VID `0x0483`, PID `0x5740`) appears. |
| **Port appears as `DFU in FS Mode`** | Device is in bootloader, not running application | Flash valid application firmware using [`gamma-firmware-flash`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/.agents/skills/gamma-firmware-flash/SKILL.md). |
| **Firmware hangs when USB host disconnects** | Firmware uses standard `daisy::Logger` | Replace `Logger<LOGGER_EXTERNAL>` with [`gamma_usb.h`](file:///Users/brett/Documents/GitHub/reverse-engineering-gamma/.agents/skills/gamma-usb-connectivity/resources/gamma_usb.h) or the non-blocking `UsbLog` struct. |
| **Crash / HardFault on receiving USB byte** | Print/TX function called within `UsbRxCallback` | Buffer incoming characters into an ISR-safe queue and process them in the main `while(1)` loop. |
| **Intermittent dropped characters** | RX buffer size too small | Increase `kRxQueueSize` in `gamma_usb.h` (default is 128 bytes). |
| **`dfu-util` fails writing `0x90040000` after `'b'`** | Firmware called `System::ResetToBootloader()` with no argument and entered the STM32 ROM bootloader, not the Daisy bootloader | Use `UsbComm::RebootToBootloader()` or pass `System::DAISY_INFINITE_TIMEOUT`. If the device is stuck in ROM DFU, tap RESET to get back to the Daisy bootloader. |
| **`'namespace gamma' redeclared as different kind of entity`** | `gamma` clashes with libm's `gamma()` function | Use the `gamma_usb` namespace from the current `gamma_usb.h`. |
