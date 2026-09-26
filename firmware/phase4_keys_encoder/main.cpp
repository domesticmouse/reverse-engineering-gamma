#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include "util/oled_fonts.h"
#include <cstdio>

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
// Hardware Pin Definitions (Discovered via Static Analysis)
// ========================================================================
// Group 1: 7 Note Keys (Physical keys on the LEFT: Top N1-N4, Bottom N5-N7)
static const Pin kNotePins[7] = {
    seed::D1, // N1: PC11
    seed::D2, // N2: PC10
    seed::D3, // N3: PC9
    seed::D4, // N4: PC8
    seed::D5, // N5: PD2
    seed::D6, // N6: PC12
    seed::D7, // N7: PG10
};

// Group 2: 7 Chord Keys (Physical keys on the RIGHT: Top C1-C4, Bottom C5-C7)
static const Pin kChordPins[7] = {
    seed::D8,  // C1: PG11
    seed::D9,  // C2: PB4
    seed::D10, // C3: PB5
    seed::D13, // C4: PB6
    seed::D14, // C5: PB7
    seed::D26, // C6: PD11
    seed::D27, // C7: PG9
};

// Rotary Encoder (Phase A, Phase B, Push Switch)
static const Pin kEncPinA  = seed::D15; // PC0
static const Pin kEncPinB  = seed::D16; // PA3
static const Pin kEncClick = seed::D28; // PA2

// Extra Board Rail / Grounding Pins
static const Pin kAuxOutLow = Pin(PORTC, 3); // PC3 (Output LOW)
static const Pin kAuxInPull = seed::D0;      // PB12 (Input Pull-up)

// ========================================================================
// Peripheral Object Instances & State
// ========================================================================
static Switch      g_note_keys[7];
static Switch      g_chord_keys[7];
static Encoder     g_encoder;
static GPIO        g_enc_gpio_a;
static GPIO        g_enc_gpio_b;
static GPIO        g_aux_low;
static GPIO        g_aux_pull;
static TimerHandle g_timer;

static volatile int32_t  g_enc_pos         = 0;
static volatile int      g_last_inc        = 0;
static volatile uint8_t  g_raw_a           = 1;
static volatile uint8_t  g_raw_b           = 1;
static volatile uint32_t g_enc_transitions = 0;
static volatile int32_t  g_quad_pos        = 0;

static char     g_last_event[36]  = "Ready - Turn/Click/Keys";
static uint32_t g_last_event_time = 0;

// High-frequency 1 kHz timer interrupt for jitter-free encoder sampling
void TimerCallback(void* data)
{
    // 1. Read raw GPIOs
    uint8_t a = g_enc_gpio_a.Read();
    uint8_t b = g_enc_gpio_b.Read();
    g_raw_a = a;
    g_raw_b = b;

    // 2. Full-transition Quadrature State Machine
    static uint8_t s_prev_quad = 0x03;
    uint8_t curr_quad = (a << 1) | b;
    if(curr_quad != s_prev_quad)
    {
        g_enc_transitions++;
        // Standard Gray code state transition lookup table
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
            // 4 Gray code transitions per mechanical detent click
            if(s_sub >= 4)
            {
                g_quad_pos++;
                g_enc_pos++;
                g_last_inc = 1;
                s_sub = 0;
            }
            else if(s_sub <= -4)
            {
                g_quad_pos--;
                g_enc_pos--;
                g_last_inc = -1;
                s_sub = 0;
            }
        }
        s_prev_quad = curr_quad;
    }

    // 3. Official libDaisy Encoder Debounce
    g_encoder.Debounce();
    int lib_inc = g_encoder.Increment();
    if(lib_inc != 0)
    {
        // If libDaisy caught an increment, ensure direction is reflected
        g_last_inc = lib_inc;
    }
}

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
        else if(c == 'r' || c == 'R')
        {
            g_enc_pos = 0;
            g_quad_pos = 0;
            g_enc_transitions = 0;
            hw.PrintLine("Encoder counters reset to 0.");
        }
        else if(c == 'h' || c == 'H' || c == '?')
        {
            hw.PrintLine("\n--- Phase 4 Diagnostic Commands ---");
            hw.PrintLine("  r : Reset encoder position counter to 0");
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

// Helper to draw a single labeled key button
static void DrawKey(int x, int y, const char* label, bool pressed)
{
    int w = 13, h = 9;
    if(pressed)
    {
        DrawFillRect(x, y, x + w, y + h, true);
        oled.SetCursor(x + 2, y + 1);
        oled.WriteString(label, Font_6x8, false); // Inverted black on white
    }
    else
    {
        DrawRect(x, y, x + w, y + h, true);
        oled.SetCursor(x + 2, y + 1);
        oled.WriteString(label, Font_6x8, true); // White on black
    }
}

// ========================================================================
// OLED Screen Update
// ========================================================================
static void UpdateScreen()
{
    if(!g_oled_active)
        return;

    oled.Fill(false);

    // Header: Physical layout labels (Left: NOTES, Right: CHORDS)
    oled.SetCursor(1, 0);
    oled.WriteString("NOTES(L)", Font_6x8, true);
    oled.SetCursor(74, 0);
    oled.WriteString("CHORDS(R)", Font_6x8, true);
    DrawLineH(0, 127, 9, true);

    // Physical Key Layout (Left side: N1-N4 top, N5-N7 bottom)
    // Top row N1-N4
    DrawKey(1,  11, "N1", g_note_keys[0].Pressed());
    DrawKey(16, 11, "N2", g_note_keys[1].Pressed());
    DrawKey(31, 11, "N3", g_note_keys[2].Pressed());
    DrawKey(46, 11, "N4", g_note_keys[3].Pressed());
    // Bottom row N5-N7
    DrawKey(8,  22, "N5", g_note_keys[4].Pressed());
    DrawKey(23, 22, "N6", g_note_keys[5].Pressed());
    DrawKey(38, 22, "N7", g_note_keys[6].Pressed());

    // Vertical separator between Left and Right keypads
    DrawLineV(63, 0, 32, true);

    // Physical Key Layout (Right side: C1-C4 top, C5-C7 bottom)
    // Top row C1-C4
    DrawKey(68,  11, "C1", g_chord_keys[0].Pressed());
    DrawKey(83,  11, "C2", g_chord_keys[1].Pressed());
    DrawKey(98,  11, "C3", g_chord_keys[2].Pressed());
    DrawKey(113, 11, "C4", g_chord_keys[3].Pressed());
    // Bottom row C5-C7
    DrawKey(75,  22, "C5", g_chord_keys[4].Pressed());
    DrawKey(90,  22, "C6", g_chord_keys[5].Pressed());
    DrawKey(105, 22, "C7", g_chord_keys[6].Pressed());

    // Divider above encoder section
    DrawLineH(0, 127, 33, true);

    // Row 3: Encoder Position, Direction, and Click Box
    oled.SetCursor(0, 36);
    char enc_buf[24];
    const char* dir_str = (g_last_inc > 0) ? "CW " : (g_last_inc < 0) ? "CCW" : "---";
    snprintf(enc_buf, sizeof(enc_buf), "POS:%+4ld %s", (long)g_enc_pos, dir_str);
    oled.WriteString(enc_buf, Font_6x8, true);

    // Encoder Click Box
    int sw_x = 88, sw_y = 35;
    bool sw_pressed = g_encoder.Pressed();
    if(sw_pressed)
    {
        DrawFillRect(sw_x, sw_y, sw_x + 38, sw_y + 9, true);
        oled.SetCursor(sw_x + 5, sw_y + 1);
        oled.WriteString("CLICK", Font_6x8, false);
    }
    else
    {
        DrawRect(sw_x, sw_y, sw_x + 38, sw_y + 9, true);
        oled.SetCursor(sw_x + 5, sw_y + 1);
        oled.WriteString("CLICK", Font_6x8, true);
    }

    // Row 4: Raw GPIO and Quadrature Diagnostic Info
    oled.SetCursor(0, 46);
    char diag_buf[24];
    snprintf(diag_buf, sizeof(diag_buf), "A:%d B:%d T:%-4lu Q:%+ld",
             (int)g_raw_a, (int)g_raw_b,
             (unsigned long)(g_enc_transitions % 10000),
             (long)g_quad_pos);
    oled.WriteString(diag_buf, Font_6x8, true);

    // Divider
    DrawLineH(0, 127, 54, true);

    // Row 5: Footer / Last Event
    oled.SetCursor(0, 56);
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
    hw.PrintLine("  Gamma Mini Synth Diagnostic Console - Phase 4");
    hw.PrintLine("  Digital Input Verification: 14 Keys + Rotary Encoder");
    hw.PrintLine("========================================================");
    hw.PrintLine("Send 'b' to reboot into DFU Bootloader, 'r' to reset counter.\n");

    // Initialize OLED Display
    InitOled();

    // Initialize Auxiliary GPIO pins (mirroring factory firmware)
    g_aux_low.Init(kAuxOutLow, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL);
    g_aux_low.Write(false); // Drive PC3 LOW

    g_aux_pull.Init(kAuxInPull, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    // Initialize 14 Key Switches (discrete active-low with pullups)
    for(int i = 0; i < 7; i++)
    {
        g_chord_keys[i].Init(kChordPins[i], 1000.0f);
        g_note_keys[i].Init(kNotePins[i], 1000.0f);
    }

    // Initialize Raw GPIOs for Encoder A and B (with internal pullups)
    g_enc_gpio_a.Init(kEncPinA, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    g_enc_gpio_b.Init(kEncPinB, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    // Initialize Rotary Encoder class instance
    g_encoder.Init(kEncPinA, kEncPinB, kEncClick, 1000.0f);

    // Start 1 kHz Hardware Timer (TIM5) for jitter-free encoder sampling
    TimerHandle::Config tim_cfg;
    tim_cfg.periph     = TimerHandle::Config::Peripheral::TIM_5;
    tim_cfg.dir        = TimerHandle::Config::CounterDir::UP;
    tim_cfg.enable_irq = true;
    tim_cfg.period     = 1000;
    g_timer.Init(tim_cfg);
    g_timer.SetPrescaler(240 - 1); // 240 MHz / 240 = 1 MHz; period 1000 = 1 kHz callback
    g_timer.SetCallback(TimerCallback, nullptr);
    g_timer.Start();

    hw.PrintLine("14 Keys, Encoder, 1kHz Timer, and OLED initialized successfully.");

    uint32_t last_blink_time  = System::GetNow();
    uint32_t last_screen_time = System::GetNow();
    int32_t  last_logged_pos  = 0;
    bool     led_state        = false;

    while(1)
    {
        uint32_t now = System::GetNow();

        // 1. Debounce Digital Keys
        for(int i = 0; i < 7; i++)
        {
            g_note_keys[i].Debounce();
            if(g_note_keys[i].RisingEdge())
            {
                snprintf(g_last_event, sizeof(g_last_event), "Pressed N%d (D%d)", i + 1, kNotePins[i].pin);
                g_last_event_time = now;
                hw.PrintLine("[KEY] Note Key N%d PRESSED (D%d)", i + 1, kNotePins[i].pin);
            }
            else if(g_note_keys[i].FallingEdge())
            {
                snprintf(g_last_event, sizeof(g_last_event), "Released N%d", i + 1);
                g_last_event_time = now;
                hw.PrintLine("[KEY] Note Key N%d RELEASED", i + 1);
            }

            g_chord_keys[i].Debounce();
            if(g_chord_keys[i].RisingEdge())
            {
                snprintf(g_last_event, sizeof(g_last_event), "Pressed C%d (D%d)", i + 1, kChordPins[i].pin);
                g_last_event_time = now;
                hw.PrintLine("[KEY] Chord Key C%d PRESSED (D%d)", i + 1, kChordPins[i].pin);
            }
            else if(g_chord_keys[i].FallingEdge())
            {
                snprintf(g_last_event, sizeof(g_last_event), "Released C%d", i + 1);
                g_last_event_time = now;
                hw.PrintLine("[KEY] Chord Key C%d RELEASED", i + 1);
            }
        }

        // 2. Monitor Rotary Encoder position changes (updated by 1kHz TimerCallback)
        int32_t cur_pos = g_enc_pos;
        if(cur_pos != last_logged_pos)
        {
            int32_t delta = cur_pos - last_logged_pos;
            last_logged_pos = cur_pos;
            snprintf(g_last_event, sizeof(g_last_event), "Enc: %+ld (%s)",
                     (long)cur_pos,
                     (delta > 0) ? "CW" : "CCW");
            g_last_event_time = now;
            hw.PrintLine("[ENC] Position: %+ld (Delta: %+ld, Trans: %lu)",
                         (long)cur_pos, (long)delta, (unsigned long)g_enc_transitions);
        }

        if(g_encoder.RisingEdge())
        {
            snprintf(g_last_event, sizeof(g_last_event), "Enc Switch PRESSED (D%d)", kEncClick.pin);
            g_last_event_time = now;
            hw.PrintLine("[ENC] Push Switch PRESSED (D%d)", kEncClick.pin);
        }
        else if(g_encoder.FallingEdge())
        {
            snprintf(g_last_event, sizeof(g_last_event), "Enc Switch RELEASED");
            g_last_event_time = now;
            hw.PrintLine("[ENC] Push Switch RELEASED");
        }

        // Reset last event text after 4 seconds of idle
        if(now - g_last_event_time > 4000)
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
