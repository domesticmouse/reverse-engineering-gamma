#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include "util/oled_fonts.h"
#include "gamma_pins.h"
#include <cstdio>
#include <cmath>

using namespace daisy;

DaisySeed hw;

// The Gamma's USB-C connector is wired to the Seed's *external* USB pins (D29/D30 -> USB_OTG_HS
// in FS mode), not the internal PA11/PA12 port used by DaisySeed::StartLog()/PrintLine().
// Verified on hardware: OTG_FS never receives SOF frames, while the bootloader enumerates fine.
using UsbLog = Logger<LOGGER_EXTERNAL>;

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
// Hardware Definitions (from gamma_pins.h)
// ========================================================================
static const char* const kKnobNames[gamma_pins::knobs::COUNT]       = {"CV", "CF", "NV", "NF"};
static const char* const kStickNames[gamma_pins::joysticks::COUNT]   = {"LX", "LY", "RX", "RY"};

#define NUM_ADC_CHANNELS (gamma_pins::knobs::COUNT + gamma_pins::joysticks::COUNT + 1)

// Filtered Analog Values (0.0 to 1.0)
static float g_knobs[gamma_pins::knobs::COUNT]     = {0.0f};
static float g_sticks[gamma_pins::joysticks::COUNT] = {0.5f, 0.5f, 0.5f, 0.5f};
static float g_aux_val                              = 0.0f;

static char     g_last_event[32]  = "Ready";
static uint32_t g_last_event_time = 0;

// Callback for USB CDC Serial inputs
// USB CDC receive path.
// UsbRxCallback runs inside the USB interrupt. It must NOT print: once a host terminal is
// connected the logger switches to a blocking TransmitSync(), which waits for a TX-complete
// interrupt that cannot fire while we are still inside the USB ISR -> permanent deadlock.
// Received bytes are therefore queued here and handled from the main loop.
static void HandleUsbBytes(uint8_t* buff, uint32_t* length);

static constexpr size_t kRxQueueSize = 64;
static volatile uint8_t g_rx_queue[kRxQueueSize];
static volatile size_t  g_rx_head = 0;
static volatile size_t  g_rx_tail = 0;

void UsbRxCallback(uint8_t* buff, uint32_t* length)
{
    if(!buff || !length)
        return;

    for(uint32_t i = 0; i < *length; i++)
    {
        size_t next = (g_rx_head + 1) % kRxQueueSize;
        if(next == g_rx_tail)
            break; // Queue full: drop remaining bytes
        g_rx_queue[g_rx_head] = buff[i];
        __DSB();
        g_rx_head = next;
    }
}

// Drain queued bytes and dispatch them to HandleUsbBytes() from the main loop
static void ProcessUsbCommands()
{
    uint8_t  local[kRxQueueSize];
    uint32_t n = 0;
    while(g_rx_tail != g_rx_head && n < kRxQueueSize)
    {
        local[n++] = g_rx_queue[g_rx_tail];
        __DSB();
        g_rx_tail = (g_rx_tail + 1) % kRxQueueSize;
    }
    if(n > 0)
        HandleUsbBytes(local, &n);
}

// Command handler (called from the main loop via ProcessUsbCommands, never from the ISR)
static void HandleUsbBytes(uint8_t* buff, uint32_t* length)
{
    if(!buff || !length)
        return;

    for(uint32_t i = 0; i < *length; i++)
    {
        char c = (char)buff[i];
        if(c == 'b' || c == 'B')
        {
            UsbLog::PrintLine("\n*** Rebooting into Daisy DFU Bootloader... ***\n");
            System::Delay(200);
            System::ResetToBootloader(System::DAISY_INFINITE_TIMEOUT);
        }
        else if(c == 'h' || c == 'H' || c == '?')
        {
            UsbLog::PrintLine("\n--- Phase 3 Diagnostic Commands ---");
            UsbLog::PrintLine("  b : Reboot into Daisy DFU Bootloader");
            UsbLog::PrintLine("  h : Show this help message");
            UsbLog::PrintLine("-----------------------------------\n");
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

    // D11 (PB8) / D12 (PB9) configured via gamma_pins::display
    gpio_init.Pin = (1U << gamma_pins::display::pin_scl.pin) | (1U << gamma_pins::display::pin_sda.pin);
    HAL_GPIO_Init(GPIOB, &gpio_init);

    MyOled::Config disp_cfg;
    disp_cfg.driver_config.transport_config.i2c_address               = gamma_pins::display::i2c_address;
    disp_cfg.driver_config.transport_config.i2c_config.periph         = I2CHandle::Config::Peripheral::I2C_1;
    disp_cfg.driver_config.transport_config.i2c_config.speed          = I2CHandle::Config::Speed::I2C_1MHZ;
    disp_cfg.driver_config.transport_config.i2c_config.mode           = I2CHandle::Config::Mode::I2C_MASTER;
    disp_cfg.driver_config.transport_config.i2c_config.pin_config.scl = gamma_pins::display::pin_scl;
    disp_cfg.driver_config.transport_config.i2c_config.pin_config.sda = gamma_pins::display::pin_sda;
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
    for(size_t i = 0; i < gamma_pins::knobs::COUNT; i++)
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
    for(size_t i = 0; i < gamma_pins::joysticks::COUNT; i++)
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
    UsbLog::StartLog(false);
    hw.usb_handle.SetReceiveCallback(UsbRxCallback, UsbHandle::UsbPeriph::FS_EXTERNAL);

    UsbLog::PrintLine("\n\n========================================================");
    UsbLog::PrintLine("  Gamma Mini Synth Diagnostic Console - Phase 3");
    UsbLog::PrintLine("  Analog Inputs: 4 Potentiometers + 2 Dual-Axis Joysticks");
    UsbLog::PrintLine("========================================================");
    UsbLog::PrintLine("Send 'b' to reboot into DFU Bootloader.\n");

    // Initialize OLED Display
    InitOled();

    // Initialize ADC with channels:
    // 0..3: Knobs (D18, D17, D19, D20)
    // 4..7: Joysticks (D22, D21, D24, D23)
    // 8:    Aux (D31)
    AdcChannelConfig adc_cfg[NUM_ADC_CHANNELS];
    for(size_t i = 0; i < gamma_pins::knobs::COUNT; i++)
    {
        adc_cfg[i].InitSingle(gamma_pins::knobs::pins[i]);
    }
    for(size_t i = 0; i < gamma_pins::joysticks::COUNT; i++)
    {
        adc_cfg[gamma_pins::knobs::COUNT + i].InitSingle(gamma_pins::joysticks::pins[i]);
    }
    adc_cfg[gamma_pins::knobs::COUNT + gamma_pins::joysticks::COUNT].InitSingle(gamma_pins::aux_adc::pin);

    hw.adc.Init(adc_cfg, NUM_ADC_CHANNELS);
    hw.adc.Start();

    UsbLog::PrintLine("ADC and OLED initialized successfully.");

    uint32_t last_blink_time  = System::GetNow();
    uint32_t last_screen_time = System::GetNow();
    bool     led_state        = false;

    // Previous values for delta reporting
    float prev_knobs[gamma_pins::knobs::COUNT]       = {0.0f};
    float prev_sticks[gamma_pins::joysticks::COUNT]   = {0.5f, 0.5f, 0.5f, 0.5f};

    while(1)
    {
        uint32_t now = System::GetNow();

        // Handle USB CDC commands queued by UsbRxCallback (safe to print here)
        ProcessUsbCommands();

        // 1. Read Potentiometer Knobs (0..3) with inversion from gamma_pins
        for(size_t i = 0; i < gamma_pins::knobs::COUNT; i++)
        {
            float raw = hw.adc.GetFloat(i);
            if(gamma_pins::knobs::invert[i])
                raw = 1.0f - raw;
            if(raw < 0.0f) raw = 0.0f;
            if(raw > 1.0f) raw = 1.0f;

            // Instant tracking with micro-jitter deadband
            float delta = fabsf(raw - g_knobs[i]);
            if(delta > 0.005f)
            {
                g_knobs[i] = raw;
                if(fabsf(g_knobs[i] - prev_knobs[i]) > 0.015f)
                {
                    prev_knobs[i] = g_knobs[i];
                    snprintf(g_last_event, sizeof(g_last_event), "%s: %d%%", kKnobNames[i], (int)(g_knobs[i] * 99.0f));
                    g_last_event_time = now;
                    UsbLog::PrintLine("[ADC] Knob %s: %.3f (%d%%)", kKnobNames[i], g_knobs[i], (int)(g_knobs[i] * 100.0f));
                }
            }
        }

        // 2. Read Joystick Axes (4..7: LX, LY, RX, RY) with polarity normalization
        for(size_t i = 0; i < gamma_pins::joysticks::COUNT; i++)
        {
            float raw = hw.adc.GetFloat(gamma_pins::knobs::COUNT + i);
            if(gamma_pins::joysticks::invert[i])
                raw = 1.0f - raw;
            if(raw < 0.0f) raw = 0.0f;
            if(raw > 1.0f) raw = 1.0f;

            float delta = fabsf(raw - g_sticks[i]);
            if(delta > 0.005f)
            {
                g_sticks[i] = raw;
                if(fabsf(g_sticks[i] - prev_sticks[i]) > 0.025f)
                {
                    prev_sticks[i] = g_sticks[i];
                    snprintf(g_last_event, sizeof(g_last_event), "%s: %d%%", kStickNames[i], (int)(g_sticks[i] * 99.0f));
                    g_last_event_time = now;
                    UsbLog::PrintLine("[ADC] Stick %s: %.3f (%d%%)", kStickNames[i], g_sticks[i], (int)(g_sticks[i] * 100.0f));
                }
            }
        }

        g_aux_val += 0.05f * (hw.adc.GetFloat(gamma_pins::knobs::COUNT + gamma_pins::joysticks::COUNT) - g_aux_val);

        // Reset last event text after 3 seconds of idle
        if(now - g_last_event_time > 3000)
        {
            snprintf(g_last_event, sizeof(g_last_event), "Up: %lu s", (unsigned long)(now / 1000));
        }

        // 3. Update OLED Display (~50 Hz / every 20ms)
        if(now - last_screen_time >= 20)
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
