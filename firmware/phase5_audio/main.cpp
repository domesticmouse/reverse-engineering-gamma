#include "daisy_seed.h"
#include "daisysp.h"
#include "dev/oled_ssd130x.h"
#include "util/oled_fonts.h"
#include <cstdio>
#include <cmath>

using namespace daisy;
using namespace daisysp;

DaisySeed hw;

// ========================================================================
// OLED Display Setup (SSD1306 128x64 on I2C1, D11/D12 @ 0x3D)
// ========================================================================
using MyOled = OledDisplay<SSD130xI2c128x64Driver>;
static MyOled oled;
static bool   g_oled_active = false;

// Drawing Helpers
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
// Hardware Pin Definitions (Verified in Phases 1-4)
// ========================================================================
// 7 Note Keys (Left: N1-N4 top, N5-N7 bottom)
static const Pin kNotePins[7] = {
    seed::D1, // N1: PC11
    seed::D2, // N2: PC10
    seed::D3, // N3: PC9
    seed::D4, // N4: PC8
    seed::D5, // N5: PD2
    seed::D6, // N6: PC12
    seed::D7, // N7: PG10
};

// 7 Chord Keys (Right: C1-C4 top, C5-C7 bottom)
static const Pin kChordPins[7] = {
    seed::D8,  // C1: PG11
    seed::D9,  // C2: PB4
    seed::D10, // C3: PB5
    seed::D13, // C4: PB6
    seed::D14, // C5: PB7
    seed::D26, // C6: PD11
    seed::D27, // C7: PG9
};

// Rotary Encoder
static const Pin kEncPinA  = seed::D15; // PC0
static const Pin kEncPinB  = seed::D16; // PA3
static const Pin kEncClick = seed::D28; // PA2

// 4 Potentiometers (Across Top, Left-to-Right)
static const Pin kKnobPins[4] = {
    seed::D18, // K0: Chord Vol    (PA7 / A3)
    seed::D17, // K1: Chord Filter (PB1 / A2)
    seed::D19, // K2: Notes Vol    (PA6 / A4)
    seed::D20, // K3: Notes Filter (PC1 / A5)
};

// 4 Joystick Axes (Left Stick X/Y, Right Stick X/Y)
static const Pin kStickPins[4] = {
    seed::D22, // J0 (LX): PA5 (A7) [Standard: Left=0, Right=1]
    seed::D21, // J1 (LY): PC4 (A6) [Inverted: Down=0, Up=1]
    seed::D24, // J2 (RX): PA1 (A9) [Inverted: Left=0, Right=1]
    seed::D23, // J3 (RY): PA4 (A8) [Inverted: Down=0, Up=1]
};

static const Pin kAuxPin = seed::D31; // PC2 / ADC1_INP12

// Grounding / Aux
static const Pin kAuxOutLow = Pin(PORTC, 3); // PC3 (Output LOW)
static const Pin kAuxInPull = seed::D0;      // PB12 (Input Pull-up)

#define NUM_ADC_CHANNELS 9

// ========================================================================
// Musical Tuning & Chord Tables
// ========================================================================
// Note scale frequencies (C Major scale: C4 to B4)
static const float kNoteFreqs[7] = {
    261.63f, // N1: C4
    293.66f, // N2: D4
    329.63f, // N3: E4
    349.23f, // N4: F4
    392.00f, // N5: G4
    440.00f, // N6: A4
    493.88f  // N7: B4
};

static const char* const kNoteNames[7] = {
    "C4", "D4", "E4", "F4", "G4", "A4", "B4"
};

// 7 Chords: Root, Third, Fifth frequencies (Triads in C Major: I, ii, iii, IV, V, vi, vii°)
struct ChordDef {
    const char* name;
    float r; // Root
    float t; // Third
    float f; // Fifth
};

static const ChordDef kChords[7] = {
    {"C Maj", 130.81f, 164.81f, 196.00f}, // C1: C3, E3, G3
    {"D Min", 146.83f, 174.61f, 220.00f}, // C2: D3, F3, A3
    {"E Min", 164.81f, 196.00f, 246.94f}, // C3: E3, G3, B3
    {"F Maj", 174.61f, 220.00f, 261.63f}, // C4: F3, A3, C4
    {"G Maj", 196.00f, 246.94f, 293.66f}, // C5: G3, B3, D4
    {"A Min", 220.00f, 261.63f, 329.63f}, // C6: A3, C4, E4
    {"B Dim", 246.94f, 293.66f, 349.23f}  // C7: B3, D4, F4
};

// ========================================================================
// Audio Engine & DSP Voice Objects
// ========================================================================
// Note Synthesizer Voice
static Oscillator g_note_osc;
static Svf        g_note_filter;
static float      g_note_target_amp = 0.0f;
static float      g_note_curr_amp   = 0.0f;
static int        g_active_note_idx = -1;

// Chord Synthesizer Voice (3-oscillator polyphonic triad)
static Oscillator g_chord_osc[3];
static Svf        g_chord_filter;
static float      g_chord_target_amp = 0.0f;
static float      g_chord_curr_amp   = 0.0f;
static int        g_active_chord_idx = -1;

// Diagnostic Test Tone Generator
static Oscillator g_test_osc;
static float      g_test_freq = 440.0f;

// Operational Modes
enum AudioMode {
    MODE_SYNTH = 0,
    MODE_TEST_TONE,
    MODE_LAST
};
static AudioMode g_current_mode = MODE_SYNTH;

// Waveform choices
static const uint8_t kWaveforms[] = {
    Oscillator::WAVE_SIN,
    Oscillator::WAVE_TRI,
    Oscillator::WAVE_POLYBLEP_SAW,
    Oscillator::WAVE_POLYBLEP_SQUARE
};
static const char* const kWaveNames[] = {
    "SINE", "TRI", "SAW", "SQR"
};
#define NUM_WAVEFORMS 4
static int g_current_waveform = 2; // Default to Saw

// Peak VU Meters (0.0 to 1.0)
static volatile float g_peak_left  = 0.0f;
static volatile float g_peak_right = 0.0f;

// ========================================================================
// Controls & Physical Hardware Objects
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
static volatile uint8_t  g_raw_a           = 1;
static volatile uint8_t  g_raw_b           = 1;
static volatile uint32_t g_enc_transitions = 0;

static float g_knobs[4]  = {0.5f, 0.5f, 0.5f, 0.5f};
static float g_sticks[4] = {0.5f, 0.5f, 0.5f, 0.5f};

static char     g_last_event[36]  = "Ready - Audio Active";
static uint32_t g_last_event_time = 0;

// Soft saturation curve (avoids harsh clipping, adds warm analog saturation)
static inline float SoftSaturate(float x)
{
    if(x > 1.2f) return 1.0f;
    if(x < -1.2f) return -1.0f;
    return x - (x * x * x) * 0.166667f;
}

// ========================================================================
// High-Frequency 1 kHz Timer Interrupt (Encoder Quadrature Sampling)
// ========================================================================
void TimerCallback(void* data)
{
    uint8_t a = g_enc_gpio_a.Read();
    uint8_t b = g_enc_gpio_b.Read();
    g_raw_a = a;
    g_raw_b = b;

    static uint8_t s_prev_quad = 0x03;
    uint8_t curr_quad = (a << 1) | b;
    if(curr_quad != s_prev_quad)
    {
        g_enc_transitions++;
        static const int8_t kQuadTable[16] = {
             0, -1,  1,  0,
             1,  0,  0, -1,
            -1,  0,  0,  1,
             0,  1, -1,  0
        };
        uint8_t idx = (s_prev_quad << 2) | curr_quad;
        int8_t step = kQuadTable[idx & 0x0F];
        if(step != 0)
        {
            static int8_t s_accum = 0;
            s_accum += step;
            if(s_accum >= 4)
            {
                g_enc_pos++;
                s_accum = 0;
            }
            else if(s_accum <= -4)
            {
                g_enc_pos--;
                s_accum = 0;
            }
        }
        s_prev_quad = curr_quad;
    }
}

// ========================================================================
// USB CDC Command Callback
// ========================================================================
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
        else if(c == 'm' || c == 'M' || c == 't' || c == 'T')
        {
            g_current_mode = (g_current_mode == MODE_SYNTH) ? MODE_TEST_TONE : MODE_SYNTH;
            hw.PrintLine("[MODE] Switched to %s", (g_current_mode == MODE_SYNTH) ? "SYNTH" : "TEST TONE");
        }
        else if(c == 'w' || c == 'W')
        {
            g_current_waveform = (g_current_waveform + 1) % NUM_WAVEFORMS;
            uint8_t wf = kWaveforms[g_current_waveform];
            g_note_osc.SetWaveform(wf);
            for(int k = 0; k < 3; k++)
                g_chord_osc[k].SetWaveform(wf);
            hw.PrintLine("[WAVE] Oscillator waveform changed to %s", kWaveNames[g_current_waveform]);
        }
        else if(c == 'h' || c == 'H' || c == '?')
        {
            hw.PrintLine("\n--- Gamma Phase 5 Audio Commands ---");
            hw.PrintLine("  m/t : Toggle mode (SYNTH vs TEST TONE)");
            hw.PrintLine("  w   : Cycle waveform (Sine, Tri, Saw, Square)");
            hw.PrintLine("  b   : Reboot into DFU Bootloader");
            hw.PrintLine("  h/? : Print this help\n");
        }
    }
}

// ========================================================================
// Stereo Audio Callback (PCM3060 via SAI1 @ 48 kHz, Block Size 48)
// ========================================================================
void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
{
    // Fast parameter read
    float chord_vol = g_knobs[0]; // K0
    float note_vol  = g_knobs[2]; // K2

    // Filter cutoffs (logarithmic mapping: 100 Hz to 14,000 Hz)
    float chord_cutoff = 100.0f * powf(140.0f, g_knobs[1]);
    float note_cutoff  = 100.0f * powf(140.0f, g_knobs[3]);

    // LY: Filter resonance (0.05 to 0.75)
    float res = 0.05f + g_sticks[1] * 0.70f;
    // RX: Stereo pan (0.0 = full left, 1.0 = full right)
    float pan_r = g_sticks[2];
    float pan_l = 1.0f - pan_r;

    g_note_filter.SetFreq(note_cutoff);
    g_note_filter.SetRes(res);
    g_chord_filter.SetFreq(chord_cutoff);
    g_chord_filter.SetRes(res);

    float peak_l = 0.0f;
    float peak_r = 0.0f;

    for(size_t i = 0; i < size; i++)
    {
        // Amplitude envelope smoothing (60 Hz slew rate to prevent clicks)
        g_note_curr_amp  += 0.02f * (g_note_target_amp - g_note_curr_amp);
        g_chord_curr_amp += 0.02f * (g_chord_target_amp - g_chord_curr_amp);

        float out_l = 0.0f;
        float out_r = 0.0f;

        if(g_current_mode == MODE_SYNTH)
        {
            // 1. Process Note Voice
            float note_sig = 0.0f;
            if(g_note_curr_amp > 0.001f)
            {
                note_sig = g_note_osc.Process() * g_note_curr_amp * note_vol;
                g_note_filter.Process(note_sig);
                note_sig = g_note_filter.Low();
            }

            // 2. Process Chord Voice (3-note harmony)
            float chord_sig = 0.0f;
            if(g_chord_curr_amp > 0.001f)
            {
                chord_sig  = g_chord_osc[0].Process();
                chord_sig += g_chord_osc[1].Process();
                chord_sig += g_chord_osc[2].Process();
                chord_sig *= 0.333f * g_chord_curr_amp * chord_vol;
                g_chord_filter.Process(chord_sig);
                chord_sig = g_chord_filter.Low();
            }

            // Mix and Pan
            float mono_mix = note_sig + chord_sig;
            out_l = SoftSaturate(mono_mix * (2.0f * pan_l));
            out_r = SoftSaturate(mono_mix * (2.0f * pan_r));
        }
        else // MODE_TEST_TONE
        {
            // Continuous test tone (Knob 1 sets pitch, Knob 0 sets channel routing, Knob 2 sets volume)
            float tone_sig = g_test_osc.Process() * note_vol;
            
            // Knob 0 channel routing:
            // 0..0.35: Left only, 0.35..0.65: Both, 0.65..1.0: Right only
            float ch_l = 1.0f, ch_r = 1.0f;
            if(g_knobs[0] < 0.35f) {
                ch_r = 0.0f;
            } else if(g_knobs[0] > 0.65f) {
                ch_l = 0.0f;
            }
            out_l = SoftSaturate(tone_sig * ch_l);
            out_r = SoftSaturate(tone_sig * ch_r);
        }

        out[0][i] = out_l;
        out[1][i] = out_r;

        float abs_l = fabsf(out_l);
        if(abs_l > peak_l) peak_l = abs_l;
        float abs_r = fabsf(out_r);
        if(abs_r > peak_r) peak_r = abs_r;
    }

    // Smooth VU meter peak follower (fast attack, smooth release)
    g_peak_left  = peak_l > g_peak_left  ? peak_l : g_peak_left  * 0.95f;
    g_peak_right = peak_r > g_peak_right ? peak_r : g_peak_right * 0.95f;
}

// ========================================================================
// OLED Initialization
// ========================================================================
static void InitOled()
{
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_I2C1_FORCE_RESET();
    System::Delay(10);
    __HAL_RCC_I2C1_RELEASE_RESET();
    System::Delay(10);

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
// Draw VU Meter Bar
// ========================================================================
static void DrawVuMeter(int x, int y, int width, int height, float level)
{
    DrawRect(x, y, x + width - 1, y + height - 1, true);
    int fill_w = (int)(level * (width - 4));
    if(fill_w > width - 4) fill_w = width - 4;
    if(fill_w > 0)
        DrawFillRect(x + 2, y + 2, x + 2 + fill_w, y + height - 3, true);
}

// ========================================================================
// OLED Screen Update
// ========================================================================
static void UpdateScreen()
{
    if(!g_oled_active)
        return;

    oled.Fill(false);

    // Row 1: Header (Mode + Active Waveform)
    oled.SetCursor(0, 0);
    if(g_current_mode == MODE_SYNTH)
        oled.WriteString("GAMMA SYNTH", Font_6x8, true);
    else
        oled.WriteString("GAMMA TEST TONE", Font_6x8, true);

    oled.SetCursor(92, 0);
    oled.WriteString(kWaveNames[g_current_waveform], Font_6x8, true);
    DrawLineH(0, 127, 9, true);

    // Row 2: Stereo VU Meters
    oled.SetCursor(0, 12);
    oled.WriteString("L", Font_6x8, true);
    DrawVuMeter(10, 12, 50, 7, g_peak_left);

    oled.SetCursor(66, 12);
    oled.WriteString("R", Font_6x8, true);
    DrawVuMeter(76, 12, 50, 7, g_peak_right);

    DrawLineH(0, 127, 21, true);

    // Row 3: Active Status Details
    char line_buf[26];
    if(g_current_mode == MODE_SYNTH)
    {
        // Display current active note or chord
        oled.SetCursor(0, 24);
        if(g_active_note_idx >= 0)
            snprintf(line_buf, sizeof(line_buf), "Note:%-2s (%3dHz) V:%2d%%",
                     kNoteNames[g_active_note_idx],
                     (int)kNoteFreqs[g_active_note_idx],
                     (int)(g_knobs[2] * 99.0f));
        else
            snprintf(line_buf, sizeof(line_buf), "Note:--          V:%2d%%", (int)(g_knobs[2] * 99.0f));
        oled.WriteString(line_buf, Font_6x8, true);

        oled.SetCursor(0, 34);
        if(g_active_chord_idx >= 0)
            snprintf(line_buf, sizeof(line_buf), "Chrd:%-5s     V:%2d%%",
                     kChords[g_active_chord_idx].name,
                     (int)(g_knobs[0] * 99.0f));
        else
            snprintf(line_buf, sizeof(line_buf), "Chrd:--          V:%2d%%", (int)(g_knobs[0] * 99.0f));
        oled.WriteString(line_buf, Font_6x8, true);
    }
    else // MODE_TEST_TONE
    {
        oled.SetCursor(0, 24);
        snprintf(line_buf, sizeof(line_buf), "Tone: %4d Hz", (int)g_test_freq);
        oled.WriteString(line_buf, Font_6x8, true);

        oled.SetCursor(0, 34);
        const char* route = "STEREO (L+R)";
        if(g_knobs[0] < 0.35f) route = "LEFT ONLY";
        else if(g_knobs[0] > 0.65f) route = "RIGHT ONLY";
        snprintf(line_buf, sizeof(line_buf), "Chan: %-12s", route);
        oled.WriteString(line_buf, Font_6x8, true);
    }

    // Row 4: Knobs & Filters
    oled.SetCursor(0, 45);
    snprintf(line_buf, sizeof(line_buf), "CF:%d%% NF:%d%% Pan:%s",
             (int)(g_knobs[1] * 99.0f),
             (int)(g_knobs[3] * 99.0f),
             g_sticks[2] < 0.4f ? "L" : (g_sticks[2] > 0.6f ? "R" : "C"));
    oled.WriteString(line_buf, Font_6x8, true);

    DrawLineH(0, 127, 54, true);

    // Row 5: Footer / Last Event
    oled.SetCursor(0, 56);
    oled.WriteString(g_last_event, Font_6x8, true);

    oled.Update();
}

// ========================================================================
// Main Entry Point
// ========================================================================
int main(void)
{
    // 1. Initialize Daisy Seed 2 DFM (STM32H750 @ 480 MHz, PCM3060 codec via SAI1)
    hw.Init();

    // 2. Start USB CDC Logging (non-blocking)
    hw.StartLog(false);
    hw.usb_handle.SetReceiveCallback(UsbRxCallback, UsbHandle::UsbPeriph::FS_INTERNAL);

    hw.PrintLine("\n\n========================================================");
    hw.PrintLine("  Gamma Mini Synth Audio Verification - Phase 5");
    hw.PrintLine("  PCM3060 Stereo Codec via SAI1 @ 48 kHz (Seed 2 DFM)");
    hw.PrintLine("========================================================");
    hw.PrintLine("Controls:");
    hw.PrintLine("  - Note Keys N1-N7  : Play C4 to B4 major scale");
    hw.PrintLine("  - Chord Keys C1-C7 : Play triads (C, Dm, Em, F, G, Am, Bdim)");
    hw.PrintLine("  - Knobs K0-K3      : Chord Vol, Chord Filter, Note Vol, Note Filter");
    hw.PrintLine("  - Left Stick (LX)  : Pitch bend");
    hw.PrintLine("  - Right Stick (RX) : Stereo pan");
    hw.PrintLine("  - Rotary Encoder   : Turn = Cycle Waveform, Click = Mode Toggle");
    hw.PrintLine("  - USB Commands     : 'm' (mode), 'w' (wave), 'b' (bootloader)\n");

    // 3. Initialize OLED Display (I2C1 @ 0x3D)
    InitOled();

    // 4. Initialize Board Rail / Aux Pins
    g_aux_low.Init(kAuxOutLow, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL);
    g_aux_low.Write(false); // Drive PC3 LOW
    g_aux_pull.Init(kAuxInPull, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    // 5. Initialize 14 Key Switches
    for(int i = 0; i < 7; i++)
    {
        g_chord_keys[i].Init(kChordPins[i], 1000.0f);
        g_note_keys[i].Init(kNotePins[i], 1000.0f);
    }

    // 6. Initialize Rotary Encoder
    g_enc_gpio_a.Init(kEncPinA, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    g_enc_gpio_b.Init(kEncPinB, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    g_encoder.Init(kEncPinA, kEncPinB, kEncClick, 1000.0f);

    // 7. Start 1 kHz Hardware Timer for Encoder
    TimerHandle::Config tim_cfg;
    tim_cfg.periph     = TimerHandle::Config::Peripheral::TIM_5;
    tim_cfg.dir        = TimerHandle::Config::CounterDir::UP;
    tim_cfg.enable_irq = true;
    tim_cfg.period     = 1000;
    g_timer.Init(tim_cfg);
    g_timer.SetPrescaler(240 - 1);
    g_timer.SetCallback(TimerCallback, nullptr);
    g_timer.Start();

    // 8. Initialize ADC (9 Channels: 4 Knobs, 4 Joysticks, 1 Aux)
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

    // 9. Initialize Audio Synthesizer Engine & DaisySP Modules
    float sample_rate = hw.AudioSampleRate();

    // Note Voice
    g_note_osc.Init(sample_rate);
    g_note_osc.SetWaveform(kWaveforms[g_current_waveform]);
    g_note_osc.SetAmp(0.7f);
    g_note_filter.Init(sample_rate);
    g_note_filter.SetRes(0.2f);

    // Chord Voice (3 oscillators)
    for(int k = 0; k < 3; k++)
    {
        g_chord_osc[k].Init(sample_rate);
        g_chord_osc[k].SetWaveform(kWaveforms[g_current_waveform]);
        g_chord_osc[k].SetAmp(0.5f);
    }
    g_chord_filter.Init(sample_rate);
    g_chord_filter.SetRes(0.2f);

    // Test Tone
    g_test_osc.Init(sample_rate);
    g_test_osc.SetWaveform(Oscillator::WAVE_SIN);
    g_test_osc.SetFreq(440.0f);
    g_test_osc.SetAmp(0.5f);

    // 10. Start Audio Processing Callback
    hw.StartAudio(AudioCallback);
    hw.PrintLine("[AUDIO] SAI1 PCM3060 audio engine started @ %.0f Hz.", sample_rate);

    uint32_t last_screen_time = System::GetNow();
    uint32_t last_blink_time  = System::GetNow();
    int32_t  last_logged_pos  = 0;
    bool     led_state        = false;

    while(1)
    {
        uint32_t now = System::GetNow();

        // --- A. Read & Smooth ADC Inputs ---
        for(int i = 0; i < 4; i++)
        {
            // Potentiometers (active high w/ 1.0 - raw)
            float raw_k = 1.0f - hw.adc.GetFloat(i);
            if(raw_k < 0.0f) raw_k = 0.0f;
            if(raw_k > 1.0f) raw_k = 1.0f;
            g_knobs[i] += 0.1f * (raw_k - g_knobs[i]);

            // Joysticks
            float raw_s = hw.adc.GetFloat(4 + i);
            if(i > 0) raw_s = 1.0f - raw_s; // Invert LY, RX, RY
            if(raw_s < 0.0f) raw_s = 0.0f;
            if(raw_s > 1.0f) raw_s = 1.0f;
            g_sticks[i] += 0.1f * (raw_s - g_sticks[i]);
        }

        // Test tone frequency adjustment via Knob 1 in test mode (50 Hz to 2000 Hz)
        if(g_current_mode == MODE_TEST_TONE)
        {
            g_test_freq = 50.0f + g_knobs[1] * 1950.0f;
            g_test_osc.SetFreq(g_test_freq);
        }

        // --- B. Digital Key Switch Processing ---
        // Note Keys (N1-N7)
        int pressed_note = -1;
        for(int i = 0; i < 7; i++)
        {
            g_note_keys[i].Debounce();
            if(g_note_keys[i].Pressed())
                pressed_note = i; // Last pressed takes priority

            if(g_note_keys[i].RisingEdge())
            {
                snprintf(g_last_event, sizeof(g_last_event), "Note ON: %s", kNoteNames[i]);
                g_last_event_time = now;
                hw.PrintLine("[NOTE] %s (%.1f Hz) PRESSED", kNoteNames[i], kNoteFreqs[i]);
            }
        }

        g_active_note_idx = pressed_note;
        if(pressed_note >= 0)
        {
            // Calculate frequency with pitch bend
            float pitch_bend = powf(2.0f, (g_sticks[0] - 0.5f) * (4.0f / 12.0f));
            g_note_osc.SetFreq(kNoteFreqs[pressed_note] * pitch_bend);
            g_note_target_amp = 1.0f;
        }
        else
        {
            g_note_target_amp = 0.0f;
        }

        // Chord Keys (C1-C7)
        int pressed_chord = -1;
        for(int i = 0; i < 7; i++)
        {
            g_chord_keys[i].Debounce();
            if(g_chord_keys[i].Pressed())
                pressed_chord = i;

            if(g_chord_keys[i].RisingEdge())
            {
                snprintf(g_last_event, sizeof(g_last_event), "Chord ON: %s", kChords[i].name);
                g_last_event_time = now;
                hw.PrintLine("[CHORD] %s PRESSED", kChords[i].name);
            }
        }

        g_active_chord_idx = pressed_chord;
        if(pressed_chord >= 0)
        {
            float pitch_bend = powf(2.0f, (g_sticks[0] - 0.5f) * (4.0f / 12.0f));
            g_chord_osc[0].SetFreq(kChords[pressed_chord].r * pitch_bend);
            g_chord_osc[1].SetFreq(kChords[pressed_chord].t * pitch_bend);
            g_chord_osc[2].SetFreq(kChords[pressed_chord].f * pitch_bend);
            g_chord_target_amp = 1.0f;
        }
        else
        {
            g_chord_target_amp = 0.0f;
        }

        // --- C. Rotary Encoder Processing ---
        int32_t cur_pos = g_enc_pos;
        if(cur_pos != last_logged_pos)
        {
            int32_t delta = cur_pos - last_logged_pos;
            last_logged_pos = cur_pos;

            if(delta > 0)
                g_current_waveform = (g_current_waveform + 1) % NUM_WAVEFORMS;
            else
                g_current_waveform = (g_current_waveform - 1 + NUM_WAVEFORMS) % NUM_WAVEFORMS;

            uint8_t wf = kWaveforms[g_current_waveform];
            g_note_osc.SetWaveform(wf);
            for(int k = 0; k < 3; k++)
                g_chord_osc[k].SetWaveform(wf);

            snprintf(g_last_event, sizeof(g_last_event), "Wave: %s", kWaveNames[g_current_waveform]);
            g_last_event_time = now;
            hw.PrintLine("[ENC] Waveform: %s", kWaveNames[g_current_waveform]);
        }

        // Encoder Push Switch
        g_encoder.Debounce();
        if(g_encoder.RisingEdge())
        {
            g_current_mode = (g_current_mode == MODE_SYNTH) ? MODE_TEST_TONE : MODE_SYNTH;
            snprintf(g_last_event, sizeof(g_last_event), "Mode: %s",
                     (g_current_mode == MODE_SYNTH) ? "SYNTH" : "TEST TONE");
            g_last_event_time = now;
            hw.PrintLine("[MODE] Mode toggled to %s",
                         (g_current_mode == MODE_SYNTH) ? "SYNTH" : "TEST TONE");
        }

        // --- D. OLED Screen Refresh (~25 Hz) ---
        if(now - last_screen_time >= 40)
        {
            last_screen_time = now;
            UpdateScreen();
        }

        // --- E. LED Heartbeat (1 Hz) ---
        if(now - last_blink_time >= 500)
        {
            last_blink_time = now;
            led_state = !led_state;
            hw.SetLed(led_state);
        }

        // Clear stale last event
        if(g_last_event_time > 0 && (now - g_last_event_time > 3000))
        {
            snprintf(g_last_event, sizeof(g_last_event), "Audio Running (48k)");
            g_last_event_time = 0;
        }

        System::Delay(1);
    }
}
