# Digital Keypad (14 Tactile Switches) Reference

The Gamma synthesizer has 14 tactile keypad switches arranged into two distinct physical grids: 7 Note keys on the left and 7 Chord keys on the right.

---

## 1. Hardware Pinout & Physical Layout

Each switch is connected to an MCU GPIO pin configured with an internal pull-up resistor (`INPUT_PULLUP`).
* **Active-Low:** `1` (HIGH) = Unpressed; `0` (LOW) = Pressed.

### Left Keypad: 7 Note Keys (N1–N7)
```text
  [ N1 ]   [ N2 ]   [ N3 ]   [ N4 ]     <-- Top Row
  [ N5 ]   [ N6 ]   [ N7 ]              <-- Bottom Row
```

| Key | Position | Daisy Seed Pin | STM32 Pin | Logic |
| :---: | :--- | :--- | :--- | :--- |
| **N1** | Top Row, Col 1 | `seed::D1` | `PC11` | Active-Low |
| **N2** | Top Row, Col 2 | `seed::D2` | `PC10` | Active-Low |
| **N3** | Top Row, Col 3 | `seed::D3` | `PC9` | Active-Low |
| **N4** | Top Row, Col 4 | `seed::D4` | `PC8` | Active-Low |
| **N5** | Bottom Row, Col 1 | `seed::D5` | `PD2` | Active-Low |
| **N6** | Bottom Row, Col 2 | `seed::D6` | `PC12` | Active-Low |
| **N7** | Bottom Row, Col 3 | `seed::D7` | `PG10` | Active-Low |

### Right Keypad: 7 Chord Keys (C1–C7)
```text
  [ C1 ]   [ C2 ]   [ C3 ]   [ C4 ]     <-- Top Row
  [ C5 ]   [ C6 ]   [ C7 ]              <-- Bottom Row
```

| Key | Position | Daisy Seed Pin | STM32 Pin | Logic |
| :---: | :--- | :--- | :--- | :--- |
| **C1** | Top Row, Col 1 | `seed::D8` | `PG11` | Active-Low |
| **C2** | Top Row, Col 2 | `seed::D9` | `PB4` | Active-Low |
| **C3** | Top Row, Col 3 | `seed::D10` | `PB5` | Active-Low |
| **C4** | Top Row, Col 4 | `seed::D13` | `PB6` | Active-Low |
| **C5** | Bottom Row, Col 1 | `seed::D14` | `PB7` | Active-Low |
| **C6** | Bottom Row, Col 2 | `seed::D26` | `PD11` | Active-Low |
| **C7** | Bottom Row, Col 3 | `seed::D27` | `PG9` | Active-Low |

---

## 2. Key Debouncing & 1 kHz Sampling Architecture

The standard `daisy::Switch` class provides software debouncing, edge detection (`RisingEdge()`, `FallingEdge()`), and level state (`Pressed()`).

> [!TIP]
> **Fast Debounce Rate:**
> Initializing `Switch` with an update rate of `1000.0f` Hz and calling `Debounce()` inside a 1 kHz timer ISR achieves key press latency of **~8 to 10 ms**. Debouncing at 50 Hz in the main loop introduces noticeable ~200 ms latency.

```cpp
#include "daisy_seed.h"
#include "gamma_pins.h"

using namespace daisy;

static Switch g_note_keys[gamma_pins::note_keys::COUNT];
static Switch g_chord_keys[gamma_pins::chord_keys::COUNT];

void InitKeys()
{
    // Initialize with 1000 Hz expected debounce polling rate
    for(size_t i = 0; i < gamma_pins::note_keys::COUNT; i++)
        g_note_keys[i].Init(gamma_pins::note_keys::pins[i], 1000.0f);

    for(size_t i = 0; i < gamma_pins::chord_keys::COUNT; i++)
        g_chord_keys[i].Init(gamma_pins::chord_keys::pins[i], 1000.0f);
}
```

---

## 3. Interrupt-Safe Event Queue

To transfer key press and release events cleanly from the 1 kHz timer ISR to the application loop without mutex locks, use a lightweight single-producer single-consumer ring buffer:

```cpp
struct KeyEvent
{
    uint8_t id;      // 0-6: Note keys (N1-N7), 7-13: Chord keys (C1-C7)
    bool    pressed; // true = Press, false = Release
};

static constexpr size_t kEventQueueSize = 32;
static KeyEvent         g_event_queue[kEventQueueSize];
static volatile size_t  g_eq_head = 0;
static volatile size_t  g_eq_tail = 0;

inline void EnqueueKeyEvent(uint8_t id, bool pressed)
{
    size_t next = (g_eq_head + 1) % kEventQueueSize;
    if(next != g_eq_tail)
    {
        g_event_queue[g_eq_head].id      = id;
        g_event_queue[g_eq_head].pressed = pressed;
        g_eq_head                        = next;
    }
}

// Called within the 1 kHz Timer ISR
void DebounceKeysISR()
{
    for(size_t i = 0; i < gamma_pins::note_keys::COUNT; i++)
    {
        g_note_keys[i].Debounce();
        if(g_note_keys[i].RisingEdge())
            EnqueueKeyEvent(i, true);
        else if(g_note_keys[i].FallingEdge())
            EnqueueKeyEvent(i, false);
    }

    for(size_t i = 0; i < gamma_pins::chord_keys::COUNT; i++)
    {
        g_chord_keys[i].Debounce();
        if(g_chord_keys[i].RisingEdge())
            EnqueueKeyEvent(7 + i, true);
        else if(g_chord_keys[i].FallingEdge())
            EnqueueKeyEvent(7 + i, false);
    }
}
```
