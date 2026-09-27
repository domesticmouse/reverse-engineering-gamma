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

## 2. Mechanical Detents & Quadrature Gray Code

* **Detent Resolution:** The encoder produces **4 Gray code state transitions per mechanical detent click**.
* Simple edge detection or relying solely on single-pin interrupts can lead to bounce or double-counting.
* A robust full-quadrature lookup table decodes transitions and accumulates 4 sub-steps before committing a position increment/decrement.

### Gray Code State Transition Lookup Table

```text
Previous [A, B] -> Current [A, B] (Index: prev << 2 | curr)
Valid Steps: +1 (CW), -1 (CCW), 0 (No change or invalid transition)
```

```cpp
static const int8_t kQuadTable[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
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

    // 2. Decode Gray Code
    static uint8_t s_prev_quad = 0x03;
    uint8_t curr_quad = (a << 1) | b;
    if(curr_quad != s_prev_quad)
    {
        static const int8_t kQuadTable[16] = {
             0, -1,  1,  0,
             1,  0,  0, -1,
            -1,  0,  0,  1,
             0,  1, -1,  0
        };
        int8_t step = kQuadTable[(s_prev_quad << 2) | curr_quad];
        if(step != 0)
        {
            static int8_t s_sub = 0;
            s_sub += step;
            // 4 transitions per detent click
            if(s_sub >= 4)
            {
                g_enc_pos++;
                g_last_inc = 1;
                s_sub = 0;
            }
            else if(s_sub <= -4)
            {
                g_enc_pos--;
                g_last_inc = -1;
                s_sub = 0;
            }
        }
        s_prev_quad = curr_quad;
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
