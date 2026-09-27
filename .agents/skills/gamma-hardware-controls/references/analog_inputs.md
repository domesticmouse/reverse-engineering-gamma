# Analog Inputs (Knobs & Joysticks) Reference

The Gamma synthesizer exposes 8 analog user controls (4 potentiometers and 4 joystick axes) plus 1 auxiliary power/battery sensing channel, all multiplexed into the STM32H750's `ADC1` converter.

---

## 1. ADC Channel Mapping Table

| Index | Name | Function | Daisy Seed Pin | STM32 Pin | ADC1 Channel | Software Polarity |
| :---: | :--- | :--- | :--- | :--- | :--- | :--- |
| `0` | **Knob 0** | Chord Vol | `seed::D18` | `PA7` | `ADC1_INP7` (`A3`) | **Inverted** (`1.0 - raw`) |
| `1` | **Knob 1** | Chord Filter | `seed::D17` | `PB1` | `ADC1_INP5` (`A2`) | **Inverted** (`1.0 - raw`) |
| `2` | **Knob 2** | Notes Vol | `seed::D19` | `PA6` | `ADC1_INP3` (`A4`) | **Inverted** (`1.0 - raw`) |
| `3` | **Knob 3** | Notes Filter | `seed::D20` | `PC1` | `ADC1_INP11` (`A5`) | **Inverted** (`1.0 - raw`) |
| `4` | **Left Stick X (LX)** | Modulation / Pitch | `seed::D22` | `PA5` | `ADC1_INP19` (`A7`) | **Normal** (`raw`): Left=0%, Right=100% |
| `5` | **Left Stick Y (LY)** | Modulation / Pitch | `seed::D21` | `PC4` | `ADC1_INP4` (`A6`) | **Inverted** (`1.0 - raw`): Down=0%, Up=100% |
| `6` | **Right Stick X (RX)** | Filter / FX Control | `seed::D24` | `PA1` | `ADC1_INP17` (`A9`) | **Inverted** (`1.0 - raw`): Left=0%, Right=100% |
| `7` | **Right Stick Y (RY)** | Filter / FX Control | `seed::D23` | `PA4` | `ADC1_INP18` (`A8`) | **Inverted** (`1.0 - raw`): Down=0%, Up=100% |
| `8` | **Auxiliary ADC** | Battery / Rail Sense | `seed::D31` | `PC2` | `ADC1_INP12` (`A12`)| Direct voltage readout |

---

## 2. Electrical Polarities & Inversion Details

### 4 Potentiometers (Knobs across top panel)
* **Physical Sweep:** The potentiometer wipers sweep from 3.3V at fully counter-clockwise (CCW) to 0V at fully clockwise (CW).
* **Software Inversion:** Must be inverted in software so that `0.0f` = CCW and `1.0f` = CW:
  ```cpp
  float val = 1.0f - hw.adc.GetFloat(channel);
  ```

### 2 Dual-Axis Joysticks (Thumbsticks)
* **Vertical Y-Axes (LY, RY):** Both gimbals output 3.3V at bottom and 0V at top. Must be inverted:
  ```cpp
  float y_val = 1.0f - hw.adc.GetFloat(y_channel); // Down=0.0f, Up=1.0f
  ```
* **Horizontal X-Axes (LX, RX):**
  * **LX (Left Stick):** Normal polarity (`raw`). `Left = 0.0f`, `Right = 1.0f`.
  * **RX (Right Stick):** Electrically inverted on the PCB layout to simplify ground plane continuity without via crossings. Must be inverted in software: `1.0f - raw` so that `Left = 0.0f`, `Right = 1.0f`.

---

## 3. Initialization Code

All 9 channels are initialized using an array of `daisy::AdcChannelConfig`:

```cpp
#include "daisy_seed.h"
#include "gamma_pins.h"

using namespace daisy;

constexpr size_t NUM_ADC_CHANNELS = gamma_pins::knobs::COUNT + 
                                    gamma_pins::joysticks::COUNT + 1; // 4 + 4 + 1 = 9

void InitAdc(DaisySeed& hw)
{
    AdcChannelConfig adc_cfg[NUM_ADC_CHANNELS];

    // Channels 0..3: Knobs
    for(size_t i = 0; i < gamma_pins::knobs::COUNT; i++)
    {
        adc_cfg[i].InitSingle(gamma_pins::knobs::pins[i]);
    }

    // Channels 4..7: Joysticks (LX, LY, RX, RY)
    for(size_t i = 0; i < gamma_pins::joysticks::COUNT; i++)
    {
        adc_cfg[gamma_pins::knobs::COUNT + i].InitSingle(gamma_pins::joysticks::pins[i]);
    }

    // Channel 8: Auxiliary ADC
    adc_cfg[gamma_pins::knobs::COUNT + gamma_pins::joysticks::COUNT].InitSingle(gamma_pins::aux_adc::pin);

    hw.adc.Init(adc_cfg, NUM_ADC_CHANNELS);
    hw.adc.Start();
}
```

---

## 4. Jitter Filtering & Responsive Smoothing

1. **Knobs (Heavy Smoothing / Deadband):** Potentiometers set steady synthesizer parameters. Use a micro-jitter deadband (`0.005f` to `0.015f`) or a heavy IIR low-pass filter (`iir = 0.002f`).
2. **Joysticks (Fast Responsive Smoothing):** Joysticks are expressive performance controls. Use a responsive IIR filter (`iir = 0.05f`) with center calibration (~0.50f).

```cpp
static float g_knobs[gamma_pins::knobs::COUNT]       = {0.0f};
static float g_sticks[gamma_pins::joysticks::COUNT] = {0.5f, 0.5f, 0.5f, 0.5f};

void ReadAnalogControls(DaisySeed& hw)
{
    // 1. Read Knobs (0..3) with inversion & deadband
    for(size_t i = 0; i < gamma_pins::knobs::COUNT; i++)
    {
        float raw = hw.adc.GetFloat(i);
        if(gamma_pins::knobs::invert[i])
            raw = 1.0f - raw;
        
        raw = fmaxf(0.0f, fminf(1.0f, raw));

        if(fabsf(raw - g_knobs[i]) > 0.005f)
        {
            g_knobs[i] = raw;
        }
    }

    // 2. Read Joysticks (4..7) with polarity normalization & IIR filter
    for(size_t i = 0; i < gamma_pins::joysticks::COUNT; i++)
    {
        float raw = hw.adc.GetFloat(gamma_pins::knobs::COUNT + i);
        if(gamma_pins::joysticks::invert[i])
            raw = 1.0f - raw;

        raw = fmaxf(0.0f, fminf(1.0f, raw));

        // Exponential smoothing (alpha = 0.05)
        g_sticks[i] += gamma_pins::joysticks::iir_coefficient * (raw - g_sticks[i]);
    }
}
```
