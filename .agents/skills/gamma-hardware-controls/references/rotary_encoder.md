# Rotary Encoder & Push Switch Reference

The front-panel rotary encoder on the Gamma synth features a mechanical quadrature encoder with an integrated push switch.

---

## 1. Pin Assignment & Electrical Characteristics

| Signal | Daisy Seed Pin | STM32 Pin | Configuration | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Phase A** | `seed::D15` | `PC0` | GPIO In, Pull-Up | Quadrature channel A |
| **Phase B** | `seed::D16` | `PA3` | GPIO In, Pull-Up | Quadrature channel B |
| **Click Switch** | `seed::D28` | `PA2` | GPIO In, Pull-Up | Active-Low (`0` = Pressed) |
| **Ground** | N/A | GND | Common return | Shared ground wire |

---

## 2. Mechanical Detents & Quadrature State Machine

* **Detent Resolution:** The encoder produces **4 Gray code state transitions per mechanical detent click**, resting stably at `(1, 1)` at every detent.
* **Why Sub-step Accumulators Fail:** Simple step accumulation (`s_accum += step`) with arbitrary thresholding introduces contact bounce hysteresis and deadband when reversing direction, causing direction-change steps to be dropped.
* **Finite State Machine (FSM):** A 7-state finite state machine (Buxton algorithm) tracks full Gray code paths and triggers `DIR_CW` or `DIR_CCW` upon returning to `(1, 1)`, providing zero hysteresis and inherent contact debouncing.

### State Transition Matrix (Full-Cycle Detents at 11)

```cpp
#define R_START     0x0
#define R_CW_FINAL  0x1
#define R_CW_BEGIN  0x2
#define R_CW_NEXT   0x3
#define R_CCW_BEGIN 0x4
#define R_CCW_FINAL 0x5
#define R_CCW_NEXT  0x6
#define DIR_CW      0x10
#define DIR_CCW     0x20

static const uint8_t kStateTable[7][4] = {
    // 00          01           10           11
    {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
    {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},
    {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
    {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
    {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
    {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},
    {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
};
```

---

## 3. The Display Blocking Hazard & 1 kHz Timer ISR

> [!WARNING]
> **Dropped Clicks Hazard:**
> An OLED update (`oled.Update()`) via I2C blocks the processor for 9 to 23 ms. Polling the rotary encoder in the main application loop will drop up to 10–20 quadrature transitions while the screen redraws.
> 
> **Requirement:** Sample the encoder in a **1 kHz hardware timer interrupt (e.g., `TIM5`)** or in the audio DMA callback (`AudioCallback`).

```cpp
#include "daisy_seed.h"
#include "gamma_pins.h"

using namespace daisy;

static GPIO        g_enc_gpio_a;
static GPIO        g_enc_gpio_b;
static Switch      g_enc_click;
static TimerHandle g_timer;

static volatile int32_t g_enc_pos  = 0;
static volatile int     g_last_inc = 0;

void TimerCallback(void* data)
{
    // 1. Read Raw GPIOs
    uint8_t a = g_enc_gpio_a.Read();
    uint8_t b = g_enc_gpio_b.Read();

    // 2. State Machine Rotary Encoder Decoder (zero hysteresis, detents at 11)
    static uint8_t s_enc_state = R_START;
    static uint8_t s_prev_quad = 0x03;
    uint8_t curr_quad = (a << 1) | b;
    if(curr_quad != s_prev_quad)
    {
        s_prev_quad = curr_quad;
        s_enc_state = kStateTable[s_enc_state & 0x0F][curr_quad];
        uint8_t result = s_enc_state & 0x30;
        if(result == DIR_CW)
        {
            g_enc_pos++;
            g_last_inc = 1;
        }
        else if(result == DIR_CCW)
        {
            g_enc_pos--;
            g_last_inc = -1;
        }
    }

    // 3. Debounce Click Switch
    g_enc_click.Debounce();
}
```

---

## 4. Hardware Timer Configuration (`TIM5` @ 1 kHz)

On the STM32H750 running at 480 MHz, the timer peripheral bus clock (`APB1` timer clock) runs at 240 MHz.
* Prescaler: `240 - 1` -> $240\text{ MHz} / 240 = 1\text{ MHz}$ tick frequency.
* Period: `1000` -> $1\text{ MHz} / 1000 = 1\text{ kHz}$ interrupt frequency (1 ms period).

```cpp
void InitEncoderTimer()
{
    // Initialize pins with pullups
    g_enc_gpio_a.Init(gamma_pins::encoder::pin_a, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    g_enc_gpio_b.Init(gamma_pins::encoder::pin_b, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    g_enc_click.Init(gamma_pins::encoder::pin_click, 1000.0f);

    // Configure TIM5
    TimerHandle::Config tim_cfg;
    tim_cfg.periph     = TimerHandle::Config::Peripheral::TIM_5;
    tim_cfg.dir        = TimerHandle::Config::CounterDir::UP;
    tim_cfg.enable_irq = true;
    tim_cfg.period     = 1000;
    g_timer.Init(tim_cfg);
    g_timer.SetPrescaler(240 - 1);
    g_timer.SetCallback(TimerCallback, nullptr);
    g_timer.Start();
}
```

---

## 5. Standard Push Switch Gestures (Short Click vs. Long Press)

Across Gamma firmware (e.g. `phase5_audio`, `drum_synth`), the encoder push switch follows a standardized two-tier gesture convention:

| Gesture | Threshold | Action | UI / Visual Feedback |
| :--- | :--- | :--- | :--- |
| **Short Click** | Release before $400\text{ ms}$ | **Toggle Speaker Mute** (`PC3` / `seed::D32`) | Header badge toggles `SPK` $\leftrightarrow$ `MUT` |
| **Long Press** | Hold $\ge 2000\text{ ms}$ | **Reboot into DFU Bootloader** (`System::ResetToBootloader`) | Screen shows `HOLD FOR UPDATE` progress bar |

### Implementation Pattern (Timer ISR + Main Loop)

```cpp
static constexpr uint32_t kDfuHoldMs  = 2000; // Hold time to trigger bootloader
static constexpr uint32_t kDfuShowMs  = 400;  // Show progress bar on OLED
static constexpr uint32_t kClickMaxMs = 400;  // Release before this = short click

static volatile bool     g_enc_click_event   = false;
static volatile bool     g_reboot_bootloader = false;
static volatile uint32_t g_enc_hold_ms       = 0;

// Inside 1 kHz TimerCallback:
void HandleEncoderSwitch()
{
    static uint32_t s_held_ms   = 0;
    static bool     s_dfu_fired = false;

    g_enc_click.Debounce();
    if(g_enc_click.Pressed())
    {
        if(s_held_ms < 0xFFFF)
            s_held_ms++;
        if(!s_dfu_fired && s_held_ms >= kDfuHoldMs)
        {
            s_dfu_fired         = true;
            g_reboot_bootloader = true;
        }
    }
    else
    {
        if(s_held_ms > 0 && s_held_ms < kClickMaxMs)
            g_enc_click_event = true;
        s_held_ms   = 0;
        s_dfu_fired = false;
    }
    g_enc_hold_ms = s_held_ms;
}

// Inside Main Loop:
if(g_enc_click_event)
{
    g_enc_click_event = false;
    g_speaker_enabled = !g_speaker_enabled;
    g_spk_en.Write(g_speaker_enabled);
    UsbLog::PrintLine("[SPK] Speaker %s", g_speaker_enabled ? "ON" : "MUTED");
}

if(g_reboot_bootloader)
{
    g_spk_en.Write(false); // Mute speaker amplifier before reset
    hw.StopAudio();
    ShowBootloaderScreen();
    System::Delay(200);
    System::ResetToBootloader(System::DAISY_INFINITE_TIMEOUT);
}
```
