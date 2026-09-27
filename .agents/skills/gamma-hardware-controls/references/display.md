# OLED Display (SSD1306 128x64 on I2C1) Reference

The front panel features a 1.3-inch monochrome OLED display driven by an SSD1306 controller connected to the STM32H750's `I2C1` peripheral.

---

## 1. Hardware Specifications

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **Controller** | SSD1306 (128x64 monochrome) | GDDRAM persists image on soft-reset |
| **Peripheral** | `I2C1` (`0x40005400`) | Fast Mode Plus capable |
| **SCL Pin** | `seed::D11` (`PB8`) | `GPIO_AF4_I2C1` |
| **SDA Pin** | `seed::D12` (`PB9`) | `GPIO_AF4_I2C1` |
| **7-Bit Address** | **`0x3D`** | 8-bit write address `0x7A` |
| **I2C Bus Speed** | **`I2C_1MHZ`** (1 MHz) | Standard 400 kHz also supported |

---

## 2. Bus Reset Sequence & GPIO Initialization

> [!IMPORTANT]
> **I2C Bus Lockup Trap:**
> If the STM32 resets while an I2C transaction is mid-transfer (e.g., branching from the Daisy Bootloader or soft-resetting), the SSD1306 or the MCU I2C state machine can lock the bus low. You **must force-reset the I2C1 peripheral** and configure `GPIOB` pins with internal pull-ups before calling `oled.Init()`.

```cpp
#include "dev/oled_ssd130x.h"
#include "util/oled_fonts.h"
#include "gamma_pins.h"

using MyOled = daisy::OledDisplay<daisy::SSD130xI2c128x64Driver>;
static MyOled oled;

void InitDisplay()
{
    // 1. Force reset the I2C1 peripheral state machine
    __HAL_RCC_I2C1_FORCE_RESET();
    daisy::System::Delay(2);
    __HAL_RCC_I2C1_RELEASE_RESET();

    // 2. Configure PB8 (SCL) and PB9 (SDA) as Open-Drain AF4 with pull-ups
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio_init;
    gpio_init.Mode      = GPIO_MODE_AF_OD;
    gpio_init.Pull      = GPIO_PULLUP;
    gpio_init.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = GPIO_AF4_I2C1;
    gpio_init.Pin       = (1U << gamma_pins::display::pin_scl.pin) | 
                          (1U << gamma_pins::display::pin_sda.pin);
    HAL_GPIO_Init(GPIOB, &gpio_init);

    // 3. Initialize libDaisy OLED driver
    MyOled::Config disp_cfg;
    disp_cfg.driver_config.transport_config.i2c_address               = gamma_pins::display::i2c_address; // 0x3D
    disp_cfg.driver_config.transport_config.i2c_config.periph         = daisy::I2CHandle::Config::Peripheral::I2C_1;
    disp_cfg.driver_config.transport_config.i2c_config.speed          = daisy::I2CHandle::Config::Speed::I2C_1MHZ;
    disp_cfg.driver_config.transport_config.i2c_config.mode           = daisy::I2CHandle::Config::Mode::I2C_MASTER;
    disp_cfg.driver_config.transport_config.i2c_config.pin_config.scl = gamma_pins::display::pin_scl;
    disp_cfg.driver_config.transport_config.i2c_config.pin_config.sda = gamma_pins::display::pin_sda;
    oled.Init(disp_cfg);
}
```

---

## 3. Drawing Primitives & Fonts

The display uses libDaisy's standard font routines (`Font_6x8`, `Font_7x10`, `Font_11x18`, `Font_16x26`). Useful geometric primitives:

```cpp
void DrawLineH(int x0, int x1, int y, bool on)
{
    if(x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for(int x = x0; x <= x1; x++)
        oled.DrawPixel(x, y, on);
}

void DrawLineV(int x, int y0, int y1, bool on)
{
    if(y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for(int y = y0; y <= y1; y++)
        oled.DrawPixel(x, y, on);
}

void DrawRect(int x0, int y0, int x1, int y1, bool on)
{
    DrawLineH(x0, x1, y0, on);
    DrawLineH(x0, x1, y1, on);
    DrawLineV(x0, y0, y1, on);
    DrawLineV(x1, y0, y1, on);
}

void DrawFillRect(int x0, int y0, int x1, int y1, bool on)
{
    if(y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for(int y = y0; y <= y1; y++)
        DrawLineH(x0, x1, y, on);
}
```

---

## 4. Frame Refresh Budgeting & The Blocking Hazard

* Transmitting the entire 128x64 framebuffer (1024 bytes + command framing) over I2C at 1 MHz blocks the CPU for **~9 to 10 ms** (at 400 kHz, it blocks for **~23 ms**).
* **The Hazard:** Calling `oled.Update()` in the main thread completely pauses execution during transmission. Polling fast inputs (like the rotary encoder) in the same thread causes dropped clicks.
* **Best Practice:**
  1. Offload encoder and key sampling to a 1 kHz timer ISR.
  2. Throttle display updates to ~30 Hz – 50 Hz (every 20 to 33 ms).

```cpp
uint32_t now = daisy::System::GetNow();
if(now - last_screen_time >= 30) // ~33 Hz refresh rate
{
    last_screen_time = now;
    
    oled.Fill(false);
    oled.SetCursor(0, 0);
    oled.WriteString("GAMMA SYNTH", Font_6x8, true);
    // Render UI graphics...
    oled.Update();
}
```
