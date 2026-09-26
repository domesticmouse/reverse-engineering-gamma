#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include "util/oled_fonts.h"
#include <cstdio>
#include <cmath>

using namespace daisy;

DaisySeed hw;

// ========================================================================
// OLED Display Setup (SSD1306 128x64 on I2C1, D11/D12 @ 0x3D)
// ========================================================================
using MyOled = OledDisplay<SSD130xI2c128x64Driver>;
static MyOled oled;
static bool   g_oled_active = false;

// Drawing Primitives
static void DrawLineH(int x0, int x1, int y, bool on)
{
    if(x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for(int x = x0; x <= x1; x++)
        oled.DrawPixel(x, y, on);
}

static void DrawLineV(int x, int y0, int y1, bool on)
{
    if(y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for(int y = y0; y <= y1; y++)
        oled.DrawPixel(x, y, on);
}

static void DrawRect(int x0, int y0, int x1, int y1, bool on)
{
    DrawLineH(x0, x1, y0, on);
    DrawLineH(x0, x1, y1, on);
    DrawLineV(x0, y0, y1, on);
    DrawLineV(x1, y0, y1, on);
}

static void DrawFillRect(int x0, int y0, int x1, int y1, bool on)
{
    if(y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for(int y = y0; y <= y1; y++)
        DrawLineH(x0, x1, y, on);
}

// ========================================================================
// Hardware Pin Definitions (Verified on Hardware)
// ========================================================================
// 4 Analog Rotary Potentiometers (Across the Top, Left-to-Right)
static const Pin kKnobPins[4] = {
    seed::D18, // K0: Chord Vol    (PA7 / ADC1_INP7 / A3)
    seed::D17, // K1: Chord Filter (PB1 / ADC1_INP5 / A2)
    seed::D19, // K2: Notes Vol    (PA6 / ADC1_INP3 / A4)
    seed::D20, // K3: Notes Filter (PC1 / ADC1_INP11 / A5)
};

static const char* const kKnobNames[4] = {"CV", "CF", "NV", "NF"};

// 4 Analog Joystick Axes (Left Stick X/Y, Right Stick X/Y)
static const Pin kStickPins[4] = {
    seed::D22, // J0 (LX): PA5 / ADC1_INP19 (A7) [Left=0%, Right=100%]
    seed::D21, // J1 (LY): PC4 / ADC1_INP4  (A6) [Down=0%, Up=100%,  inverted]
    seed::D24, // J2 (RX): PA1 / ADC1_INP17 (A9) [Left=0%, Right=100%, inverted]
    seed::D23, // J3 (RY): PA4 / ADC1_INP18 (A8) [Down=0%, Up=100%,  inverted]
};

static const char* const kStickNames[4] = {"LX", "LY", "RX", "RY"};
static const bool        kStickInvert[4] = {false, true, true, true};

// 1 Aux / Battery Analog Input
static const Pin kAuxPin = seed::D31; // PC2 / ADC1_INP12

#define NUM_ADC_CHANNELS 9

// Filtered Analog Values (0.0 to 1.0)
static float g_knobs[4]  = {0.0f};
static float g_sticks[4] = {0.5f, 0.5f, 0.5f, 0.5f};
static float g_aux_val   = 0.0f;

static char     g_last_event[32]  = "Ready";
static uint32_t g_last_event_time = 0;

// Callback for USB CDC Serial inputs
void UsbRxCallback(uint8_t* buff, uint32_t* length)
{
    if(!buff || !length)
        return;

    for(uint32_t i = 0; i < *length; i++)
    {
        char c = (char)buff[i];
        if(c == 'b' || c == 'B')
        {
            hw.PrintLine("\n*** Rebooting into Daisy DFU Bootloader... ***\n");
            System::Delay(200);
            System::ResetToBootloader(System::DAISY_INFINITE_TIMEOUT);
        }
        else if(c == 'h' || c == 'H' || c == '?')
        {
            hw.PrintLine("\n--- Phase 3 Diagnostic Commands ---");
            hw.PrintLine("  b : Reboot into Daisy DFU Bootloader");
            hw.PrintLine("  h : Show this help message");
            hw.PrintLine("-----------------------------------\n");
        }
    }
}

// ========================================================================
// OLED Display Driver Initialization
// ========================================================================
static void InitOled()
{
    __HAL_RCC_I2C1_FORCE_RESET();
    System::Delay(2);
    __HAL_RCC_I2C1_RELEASE_RESET();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio_init;
    gpio_init.Mode      = GPIO_MODE_AF_OD;
    gpio_init.Pull      = GPIO_PULLUP;
    gpio_init.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = GPIO_AF4_I2C1;

    // D11 (PB8) / D12 (PB9)
    gpio_init.Pin = (1U << 8) | (1U << 9);
    HAL_GPIO_Init(GPIOB, &gpio_init);

    MyOled::Config disp_cfg;
    disp_cfg.driver_config.transport_config.i2c_address               = 0x3D;
    disp_cfg.driver_config.transport_config.i2c_config.periph         = I2CHandle::Config::Peripheral::I2C_1;
    disp_cfg.driver_config.transport_config.i2c_config.speed          = I2CHandle::Config::Speed::I2C_400KHZ;
    disp_cfg.driver_config.transport_config.i2c_config.mode           = I2CHandle::Config::Mode::I2C_MASTER;
    disp_cfg.driver_config.transport_config.i2c_config.pin_config.scl = seed::D11;
    disp_cfg.driver_config.transport_config.i2c_config.pin_config.sda = seed::D12;
    oled.Init(disp_cfg);
    g_oled_active = true;
}

// ========================================================================
// OLED Screen Update
// ========================================================================
static void UpdateScreen()
{
    if(!g_oled_active)
        return;

    oled.Fill(false);

    // Header
    oled.SetCursor(0, 0);
    oled.WriteString("KNOBS (L) | STICKS (R)", Font_6x8, true);
    DrawLineH(0, 127, 9, true);

    // Left Column: 4 Knobs
    for(int i = 0; i < 4; i++)
    {
        int y = 12 + i * 11;
        oled.SetCursor(0, y);
        oled.WriteString(kKnobNames[i], Font_6x8, true);

        // Bargraph (box 24px wide, 7px high)
        int bx = 16;
        DrawRect(bx, y, bx + 26, y + 7, true);
        int fill_w = (int)(g_knobs[i] * 24.0f + 0.5f);
        if(fill_w > 0)
        {
            if(fill_w > 24) fill_w = 24;
            DrawFillRect(bx + 1, y + 1, bx + 1 + fill_w, y + 6, true);
        }

        // Percentage
        char pbuf[6];
        snprintf(pbuf, sizeof(pbuf), "%2d", (int)(g_knobs[i] * 99.0f));
        oled.SetCursor(bx + 29, y);
        oled.WriteString(pbuf, Font_6x8, true);
    }

    // Divider line between columns
    DrawLineV(63, 10, 54, true);

    // Right Column: 4 Sticks (LX, LY, RX, RY)
    for(int i = 0; i < 4; i++)
    {
        int y = 12 + i * 11;
        oled.SetCursor(66, y);
        oled.WriteString(kStickNames[i], Font_6x8, true);

        // Centered Bargraph
        int bx = 82;
        DrawRect(bx, y, bx + 26, y + 7, true);
        DrawLineV(bx + 13, y, y + 7, true); // Center tick
        int center_x = bx + 13;
        int bar_len  = (int)((g_sticks[i] - 0.5f) * 24.0f);
        if(bar_len > 0)
        {
            if(bar_len > 12) bar_len = 12;
            DrawFillRect(center_x, y + 1, center_x + bar_len, y + 6, true);
        }
        else if(bar_len < 0)
        {
            if(bar_len < -12) bar_len = -12;
            DrawFillRect(center_x + bar_len, y + 1, center_x, y + 6, true);
        }

        // Percentage
        char pbuf[6];
        snprintf(pbuf, sizeof(pbuf), "%2d", (int)(g_sticks[i] * 99.0f));
        oled.SetCursor(bx + 29, y);
        oled.WriteString(pbuf, Font_6x8, true);
    }

    // Bottom Status Bar
    DrawLineH(0, 127, 55, true);
    oled.SetCursor(0, 57);
    oled.WriteString(g_last_event, Font_6x8, true);

    oled.Update();
}

// ========================================================================
// Main Firmware Entry Point
// ========================================================================
int main(void)
{
    // Initialize Daisy Seed 2 DFM (ARM Cortex-M7 @ 480 MHz)
    hw.Init();

    // Start USB CDC logging (non-blocking)
    hw.StartLog(false);
    hw.usb_handle.SetReceiveCallback(UsbRxCallback, UsbHandle::UsbPeriph::FS_INTERNAL);

    hw.PrintLine("\n\n========================================================");
    hw.PrintLine("  Gamma Mini Synth Diagnostic Console - Phase 3");
    hw.PrintLine("  Analog Inputs: 4 Potentiometers + 2 Dual-Axis Joysticks");
    hw.PrintLine("========================================================");
    hw.PrintLine("Send 'b' to reboot into DFU Bootloader.\n");

    // Initialize OLED Display
    InitOled();

    // Initialize ADC with 9 channels:
    // 0..3: Knobs (D18, D17, D19, D20)
    // 4..7: Joysticks (D22, D21, D24, D23)
    // 8:    Aux (D31)
    AdcChannelConfig adc_cfg[NUM_ADC_CHANNELS];
    adc_cfg[0].InitSingle(kKnobPins[0]);
    adc_cfg[1].InitSingle(kKnobPins[1]);
    adc_cfg[2].InitSingle(kKnobPins[2]);
    adc_cfg[3].InitSingle(kKnobPins[3]);

    adc_cfg[4].InitSingle(kStickPins[0]);
    adc_cfg[5].InitSingle(kStickPins[1]);
    adc_cfg[6].InitSingle(kStickPins[2]);
    adc_cfg[7].InitSingle(kStickPins[3]);

    adc_cfg[8].InitSingle(kAuxPin);

    hw.adc.Init(adc_cfg, NUM_ADC_CHANNELS);
    hw.adc.Start();

    hw.PrintLine("ADC and OLED initialized successfully.");

    uint32_t last_blink_time  = System::GetNow();
    uint32_t last_screen_time = System::GetNow();
    bool     led_state        = false;

    // Previous values for delta reporting
    float prev_knobs[4]  = {0.0f};
    float prev_sticks[4] = {0.5f, 0.5f, 0.5f, 0.5f};

    while(1)
    {
        uint32_t now = System::GetNow();

        // 1. Read Potentiometer Knobs (0..3) with 1.0 - raw inversion
        for(int i = 0; i < 4; i++)
        {
            float raw = 1.0f - hw.adc.GetFloat(i);
            if(raw < 0.0f) raw = 0.0f;
            if(raw > 1.0f) raw = 1.0f;
            g_knobs[i] += 0.05f * (raw - g_knobs[i]);

            if(fabsf(g_knobs[i] - prev_knobs[i]) > 0.015f)
            {
                prev_knobs[i] = g_knobs[i];
                snprintf(g_last_event, sizeof(g_last_event), "%s: %d%%", kKnobNames[i], (int)(g_knobs[i] * 99.0f));
                g_last_event_time = now;
                hw.PrintLine("[ADC] Knob %s (D%d): %.3f (%d%%)", kKnobNames[i], kKnobPins[i].pin, g_knobs[i], (int)(g_knobs[i] * 100.0f));
            }
        }

        // 2. Read Joystick Axes (4..7: LX, LY, RX, RY) with polarity normalization
        for(int i = 0; i < 4; i++)
        {
            float raw = hw.adc.GetFloat(4 + i);
            if(kStickInvert[i])
                raw = 1.0f - raw;
            if(raw < 0.0f) raw = 0.0f;
            if(raw > 1.0f) raw = 1.0f;

            g_sticks[i] += 0.12f * (raw - g_sticks[i]);

            if(fabsf(g_sticks[i] - prev_sticks[i]) > 0.025f)
            {
                prev_sticks[i] = g_sticks[i];
                snprintf(g_last_event, sizeof(g_last_event), "%s: %d%%", kStickNames[i], (int)(g_sticks[i] * 99.0f));
                g_last_event_time = now;
                hw.PrintLine("[ADC] Stick %s (D%d): %.3f (%d%%)", kStickNames[i], kStickPins[i].pin, g_sticks[i], (int)(g_sticks[i] * 100.0f));
            }
        }

        g_aux_val += 0.05f * (hw.adc.GetFloat(8) - g_aux_val);

        // Reset last event text after 3 seconds of idle
        if(now - g_last_event_time > 3000)
        {
            snprintf(g_last_event, sizeof(g_last_event), "Up: %lu s", (unsigned long)(now / 1000));
        }

        // 3. Update OLED Display (~25 Hz / every 40ms)
        if(now - last_screen_time >= 40)
        {
            last_screen_time = now;
            UpdateScreen();
        }

        // 4. Heartbeat LED (2 Hz toggle)
        if(now - last_blink_time >= 250)
        {
            last_blink_time = now;
            led_state       = !led_state;
            hw.SetLed(led_state);
        }

        System::Delay(1);
    }
}
