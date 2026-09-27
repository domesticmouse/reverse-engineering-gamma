#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include "util/oled_fonts.h"
#include "gamma_pins.h"
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
// Peripheral Object Instances & State (Pins defined in gamma_pins.h)
// ========================================================================
static Switch      g_note_keys[gamma_pins::note_keys::COUNT];
static Switch      g_chord_keys[gamma_pins::chord_keys::COUNT];
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

struct KeyEvent
{
    uint8_t id;      // 0-6: Note keys (N1-N7), 7-13: Chord keys (C1-C7), 14: Enc click
    bool    pressed;
};

static constexpr size_t kEventQueueSize = 32;
static KeyEvent         g_event_queue[kEventQueueSize];
static volatile size_t  g_eq_head = 0;
static volatile size_t  g_eq_tail = 0;

static inline void EnqueueEvent(uint8_t id, bool pressed)
{
    size_t next = (g_eq_head + 1) % kEventQueueSize;
    if(next != g_eq_tail)
    {
        g_event_queue[g_eq_head].id      = id;
        g_event_queue[g_eq_head].pressed = pressed;
        g_eq_head                        = next;
    }
}

// High-frequency 1 kHz timer interrupt for jitter-free encoder & key sampling
void TimerCallback(void* data)
{
    // 1. Read raw GPIOs
    uint8_t a = g_enc_gpio_a.Read();
    uint8_t b = g_enc_gpio_b.Read();
    g_raw_a = a;
    g_raw_b = b;

    // 2. Finite State Machine Rotary Encoder Decoder (zero hysteresis, detents at 11)
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

    static uint8_t s_enc_state = R_START;
    static uint8_t s_prev_quad = 0x03;
    uint8_t curr_quad = (a << 1) | b;
    if(curr_quad != s_prev_quad)
    {
        g_enc_transitions++;
        s_prev_quad = curr_quad;
        s_enc_state = kStateTable[s_enc_state & 0x0F][curr_quad];
        uint8_t result = s_enc_state & 0x30;
        if(result == DIR_CW)
        {
            g_quad_pos++;
            g_enc_pos++;
            g_last_inc = 1;
        }
        else if(result == DIR_CCW)
        {
            g_quad_pos--;
            g_enc_pos--;
            g_last_inc = -1;
        }
    }

    // 3. Rotary Encoder Debounce and Switch Click Capture
    g_encoder.Debounce();
    int lib_inc = g_encoder.Increment();
    if(lib_inc != 0)
    {
        // If libDaisy caught an increment, ensure direction is reflected
        g_last_inc = lib_inc;
    }
    if(g_encoder.RisingEdge())
        EnqueueEvent(14, true);
    else if(g_encoder.FallingEdge())
        EnqueueEvent(14, false);

    // 4. 1 kHz Key Debounce (reaches Pressed() in 8 ms instead of ~200 ms)
    for(size_t i = 0; i < gamma_pins::note_keys::COUNT; i++)
    {
        g_note_keys[i].Debounce();
        if(g_note_keys[i].RisingEdge())
            EnqueueEvent(i, true);
        else if(g_note_keys[i].FallingEdge())
            EnqueueEvent(i, false);
    }
    for(size_t i = 0; i < gamma_pins::chord_keys::COUNT; i++)
    {
        g_chord_keys[i].Debounce();
        if(g_chord_keys[i].RisingEdge())
            EnqueueEvent(7 + i, true);
        else if(g_chord_keys[i].FallingEdge())
            EnqueueEvent(7 + i, false);
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

    // Initialize Speaker Amp Enable (PC3) and Power Fault Monitor (PB12)
    g_aux_low.Init(gamma_pins::system_pins::pin_speaker_en, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL);
    g_aux_low.Write(false); // Hold PC3 LOW during diagnostics

    g_aux_pull.Init(gamma_pins::system_pins::pin_power_fault, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    // Initialize 14 Key Switches (discrete active-low with pullups)
    for(size_t i = 0; i < gamma_pins::chord_keys::COUNT; i++)
    {
        g_chord_keys[i].Init(gamma_pins::chord_keys::pins[i], 1000.0f);
    }
    for(size_t i = 0; i < gamma_pins::note_keys::COUNT; i++)
    {
        g_note_keys[i].Init(gamma_pins::note_keys::pins[i], 1000.0f);
    }

    // Initialize Raw GPIOs for Encoder A and B (with internal pullups)
    g_enc_gpio_a.Init(gamma_pins::encoder::pin_a, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    g_enc_gpio_b.Init(gamma_pins::encoder::pin_b, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    // Initialize Rotary Encoder class instance
    g_encoder.Init(gamma_pins::encoder::pin_a, gamma_pins::encoder::pin_b, gamma_pins::encoder::pin_click, 1000.0f);

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

        // 1. Process Key & Encoder Events from 1 kHz Interrupt Queue
        while(g_eq_tail != g_eq_head)
        {
            KeyEvent ev = g_event_queue[g_eq_tail];
            g_eq_tail   = (g_eq_tail + 1) % kEventQueueSize;

            if(ev.id < 7)
            {
                int k = ev.id;
                if(ev.pressed)
                {
                    snprintf(g_last_event, sizeof(g_last_event), "Pressed N%d (D%d)", k + 1, gamma_pins::note_keys::pins[k].pin);
                    g_last_event_time = now;
                    hw.PrintLine("[KEY] Note Key N%d PRESSED (D%d)", k + 1, gamma_pins::note_keys::pins[k].pin);
                }
                else
                {
                    snprintf(g_last_event, sizeof(g_last_event), "Released N%d", k + 1);
                    g_last_event_time = now;
                    hw.PrintLine("[KEY] Note Key N%d RELEASED", k + 1);
                }
            }
            else if(ev.id < 14)
            {
                int k = ev.id - 7;
                if(ev.pressed)
                {
                    snprintf(g_last_event, sizeof(g_last_event), "Pressed C%d (D%d)", k + 1, gamma_pins::chord_keys::pins[k].pin);
                    g_last_event_time = now;
                    hw.PrintLine("[KEY] Chord Key C%d PRESSED (D%d)", k + 1, gamma_pins::chord_keys::pins[k].pin);
                }
                else
                {
                    snprintf(g_last_event, sizeof(g_last_event), "Released C%d", k + 1);
                    g_last_event_time = now;
                    hw.PrintLine("[KEY] Chord Key C%d RELEASED", k + 1);
                }
            }
            else if(ev.id == 14)
            {
                if(ev.pressed)
                {
                    snprintf(g_last_event, sizeof(g_last_event), "Enc Switch PRESSED (D%d)", gamma_pins::encoder::pin_click.pin);
                    g_last_event_time = now;
                    hw.PrintLine("[ENC] Push Switch PRESSED (D%d)", gamma_pins::encoder::pin_click.pin);
                }
                else
                {
                    snprintf(g_last_event, sizeof(g_last_event), "Enc Switch RELEASED");
                    g_last_event_time = now;
                    hw.PrintLine("[ENC] Push Switch RELEASED");
                }
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

        // Reset last event text after 4 seconds of idle
        if(now - g_last_event_time > 4000)
        {
            snprintf(g_last_event, sizeof(g_last_event), "Up: %lu s", (unsigned long)(now / 1000));
        }

        // 3. Update OLED Display (~33 Hz / every 30ms, ~9ms I2C transfer at 1 MHz)
        if(now - last_screen_time >= 30)
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
