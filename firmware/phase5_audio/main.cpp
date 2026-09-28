#include "daisy_seed.h"
#include "daisysp.h"
#include "gamma_pins.h"
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
// Hardware Pin Definitions & Config (Imported from gamma_pins.h)
// ========================================================================
constexpr size_t NUM_ADC_CHANNELS = gamma_pins::knobs::COUNT + gamma_pins::joysticks::COUNT + 1;

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
static Oscillator     g_note_osc;
static Svf            g_note_filter;
static volatile float g_note_target_amp = 0.0f;
static float          g_note_curr_amp   = 0.0f;
static volatile int   g_active_note_idx = -1;

// Chord Synthesizer Voice (3-oscillator polyphonic triad)
static Oscillator     g_chord_osc[3];
static Svf            g_chord_filter;
static volatile float g_chord_target_amp = 0.0f;
static float          g_chord_curr_amp   = 0.0f;
static volatile int   g_active_chord_idx = -1;

// Diagnostic Test Tone Generator
static Oscillator     g_test_osc;
static volatile float g_test_freq = 440.0f;

// Operational Modes
enum AudioMode {
    MODE_SYNTH = 0,
    MODE_TEST_TONE,
    MODE_LAST
};
static volatile AudioMode g_current_mode      = MODE_SYNTH;
static volatile bool      g_reboot_bootloader = false;

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
static volatile int g_current_waveform = 2; // Default to Saw

// Peak VU Meters (0.0 to 1.0)
static volatile float g_peak_left  = 0.0f;
static volatile float g_peak_right = 0.0f;

// ========================================================================
// Controls & Physical Hardware Objects
// ========================================================================
static Switch      g_note_keys[gamma_pins::note_keys::COUNT];
static Switch      g_chord_keys[gamma_pins::chord_keys::COUNT];
static Switch      g_enc_click;
static GPIO        g_enc_gpio_a;
static GPIO        g_enc_gpio_b;
static GPIO        g_spk_en;
static bool        g_speaker_enabled = true; // PC3 state (true = Spk ON, false = Muted)
static GPIO        g_pwr_fault;              // PB12 (D0): Battery Low / Power Fault ("Charge Me!")
static TimerHandle g_timer;

// Interrupt-safe single-producer single-consumer key event ring buffer
struct KeyEvent
{
    uint8_t id;      // 0-6: Note keys (N1-N7), 7-13: Chord keys (C1-C7), 14: Enc Click
    bool    pressed; // true = Press, false = Release
};
static constexpr size_t kEventQueueSize = 32;
static KeyEvent        g_event_queue[kEventQueueSize];
static volatile size_t g_eq_head = 0;
static volatile size_t g_eq_tail = 0;

static inline void EnqueueKeyEvent(uint8_t id, bool pressed)
{
    size_t next = (g_eq_head + 1) % kEventQueueSize;
    if(next != g_eq_tail)
    {
        g_event_queue[g_eq_head].id      = id;
        g_event_queue[g_eq_head].pressed = pressed;
        __DSB(); // Ensure event data is written before head index advances
        g_eq_head                        = next;
    }
}

static volatile int32_t  g_enc_pos         = 0;
static volatile uint8_t  g_raw_a           = 1;
static volatile uint8_t  g_raw_b           = 1;
static volatile uint32_t g_enc_transitions = 0;

static volatile float g_knobs[gamma_pins::knobs::COUNT]       = {0.5f, 0.5f, 0.5f, 0.5f};
static volatile float g_sticks[gamma_pins::joysticks::COUNT] = {0.5f, 0.5f, 0.5f, 0.5f};

static char     g_last_event[36]  = "Ready - Speaker ON";
static uint32_t g_last_event_time = 0;

// Soft saturation curve using continuous daisysp::SoftClip (no step discontinuities)
static inline float SoftSaturate(float x)
{
    return daisysp::SoftClip(x);
}

// ========================================================================
// High-Frequency 1 kHz Timer Interrupt (Encoder Quadrature & Key Debounce)
// ========================================================================
void TimerCallback(void* data)
{
    // 1. Finite State Machine Rotary Encoder Decoder (zero hysteresis, detents at 11)
    uint8_t a = g_enc_gpio_a.Read();
    uint8_t b = g_enc_gpio_b.Read();
    g_raw_a = a;
    g_raw_b = b;

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
            g_enc_pos++;
        else if(result == DIR_CCW)
            g_enc_pos--;
    }

    // 2. Encoder Push Switch Debouncing & Long-Press Detection (1 kHz sampling)
    static uint32_t s_enc_press_time = 0;
    static bool     s_enc_long_fired = false;

    g_enc_click.Debounce();
    if(g_enc_click.RisingEdge())
    {
        s_enc_press_time = System::GetNow();
        s_enc_long_fired = false;
    }
    else if(g_enc_click.Pressed())
    {
        // While held, if 600 ms elapses, fire long-press event immediately
        if(!s_enc_long_fired && (System::GetNow() - s_enc_press_time >= 600))
        {
            s_enc_long_fired = true;
            EnqueueKeyEvent(16, true); // Event 16: Toggle Speaker Mute
        }
    }
    else if(g_enc_click.FallingEdge())
    {
        // Only trigger short click if long-press was not already triggered
        if(!s_enc_long_fired)
        {
            EnqueueKeyEvent(14, true); // Event 14: Toggle Audio Mode
        }
    }

    // 3. Note Keys Debouncing (1 kHz sampling)
    for(size_t i = 0; i < gamma_pins::note_keys::COUNT; i++)
    {
        g_note_keys[i].Debounce();
        if(g_note_keys[i].RisingEdge())       EnqueueKeyEvent(i, true);
        else if(g_note_keys[i].FallingEdge()) EnqueueKeyEvent(i, false);
    }

    // 4. Chord Keys Debouncing (1 kHz sampling)
    for(size_t i = 0; i < gamma_pins::chord_keys::COUNT; i++)
    {
        g_chord_keys[i].Debounce();
        if(g_chord_keys[i].RisingEdge())       EnqueueKeyEvent(7 + i, true);
        else if(g_chord_keys[i].FallingEdge()) EnqueueKeyEvent(7 + i, false);
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
            g_reboot_bootloader = true;
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
        else if(c == 's' || c == 'S')
        {
            g_speaker_enabled = !g_speaker_enabled;
            g_spk_en.Write(g_speaker_enabled);
            snprintf(g_last_event, sizeof(g_last_event), "Speaker: %s", g_speaker_enabled ? "ON" : "MUTED");
            g_last_event_time = System::GetNow();
            hw.PrintLine("[SPK] Speaker %s (PC3 = %d)",
                         g_speaker_enabled ? "ENABLED" : "MUTED",
                         g_speaker_enabled ? 1 : 0);
        }
        else if(c == 'h' || c == 'H' || c == '?')
        {
            hw.PrintLine("\n--- Gamma Phase 5 Audio Commands ---");
            hw.PrintLine("  m/t : Toggle mode (SYNTH vs TEST TONE)");
            hw.PrintLine("  s   : Toggle internal speaker mute (PC3)");
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
    float chord_vol = g_knobs[gamma_pins::knobs::CHORD_VOL];
    float note_vol  = g_knobs[gamma_pins::knobs::NOTES_VOL];

    // Filter cutoffs (logarithmic mapping: 100 Hz to 14,000 Hz)
    float chord_cutoff = 100.0f * powf(140.0f, g_knobs[gamma_pins::knobs::CHORD_FILTER]);
    float note_cutoff  = 100.0f * powf(140.0f, g_knobs[gamma_pins::knobs::NOTES_FILTER]);

    // LY: Filter resonance (0.05 to 0.75)
    float res = 0.05f + g_sticks[gamma_pins::joysticks::LEFT_Y] * 0.70f;
    // RX: Stereo pan (0.0 = full left, 1.0 = full right)
    float pan = g_sticks[gamma_pins::joysticks::RIGHT_X];
    // Constant-power equal energy panning curve
    float pan_l = cosf(pan * 1.5707963f);
    float pan_r = sinf(pan * 1.5707963f);

    g_note_filter.SetFreq(note_cutoff);
    g_note_filter.SetRes(res);
    g_chord_filter.SetFreq(chord_cutoff);
    g_chord_filter.SetRes(res);

    // Continuous 1 kHz pitch bend and voice frequency updates (unaffected by OLED refresh delays)
    float pitch_bend = powf(2.0f, (g_sticks[gamma_pins::joysticks::LEFT_X] - 0.5f) * (4.0f / 12.0f));

    int active_note = g_active_note_idx;
    if(active_note >= 0 && active_note < (int)gamma_pins::note_keys::COUNT)
    {
        g_note_osc.SetFreq(kNoteFreqs[active_note] * pitch_bend);
        g_note_target_amp = 1.0f;
    }
    else
    {
        g_note_target_amp = 0.0f;
    }

    int active_chord = g_active_chord_idx;
    if(active_chord >= 0 && active_chord < (int)gamma_pins::chord_keys::COUNT)
    {
        g_chord_osc[0].SetFreq(kChords[active_chord].r * pitch_bend);
        g_chord_osc[1].SetFreq(kChords[active_chord].t * pitch_bend);
        g_chord_osc[2].SetFreq(kChords[active_chord].f * pitch_bend);
        g_chord_target_amp = 1.0f;
    }
    else
    {
        g_chord_target_amp = 0.0f;
    }

    if(g_current_mode == MODE_TEST_TONE)
    {
        g_test_freq = 50.0f + g_knobs[gamma_pins::knobs::CHORD_FILTER] * 1950.0f;
        g_test_osc.SetFreq(g_test_freq);
    }

    float peak_l = 0.0f;
    float peak_r = 0.0f;

    for(size_t i = 0; i < size; i++)
    {
        // Amplitude envelope smoothing (fast attack/release to prevent clicks)
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
            out_l = SoftSaturate(mono_mix * pan_l);
            out_r = SoftSaturate(mono_mix * pan_r);
        }
        else // MODE_TEST_TONE
        {
            // Continuous test tone (Knob 1 sets pitch, Knob 0 sets channel routing, Knob 2 sets volume)
            float tone_sig = g_test_osc.Process() * note_vol;
            
            // Knob 0 channel routing:
            // 0..0.35: Left only, 0.35..0.65: Both, 0.65..1.0: Right only
            float ch_l = 1.0f, ch_r = 1.0f;
            if(g_knobs[gamma_pins::knobs::CHORD_VOL] < 0.35f) {
                ch_r = 0.0f;
            } else if(g_knobs[gamma_pins::knobs::CHORD_VOL] > 0.65f) {
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
    gpio_init.Pin = (1U << gamma_pins::display::pin_scl.pin) | 
                    (1U << gamma_pins::display::pin_sda.pin);
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

    // Row 1: Header (Mode + Speaker Status + Active Waveform)
    oled.SetCursor(0, 0);
    if(g_current_mode == MODE_SYNTH)
        oled.WriteString("GAMMA SYNTH", Font_6x8, true);
    else
        oled.WriteString("GAMMA TONE", Font_6x8, true);

    oled.SetCursor(70, 0);
    oled.WriteString(g_speaker_enabled ? "SPK" : "MUT", Font_6x8, true);

    oled.SetCursor(98, 0);
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
                     (int)(g_knobs[gamma_pins::knobs::NOTES_VOL] * 99.0f));
        else
            snprintf(line_buf, sizeof(line_buf), "Note:--          V:%2d%%", (int)(g_knobs[gamma_pins::knobs::NOTES_VOL] * 99.0f));
        oled.WriteString(line_buf, Font_6x8, true);

        oled.SetCursor(0, 34);
        if(g_active_chord_idx >= 0)
            snprintf(line_buf, sizeof(line_buf), "Chrd:%-5s     V:%2d%%",
                     kChords[g_active_chord_idx].name,
                     (int)(g_knobs[gamma_pins::knobs::CHORD_VOL] * 99.0f));
        else
            snprintf(line_buf, sizeof(line_buf), "Chrd:--          V:%2d%%", (int)(g_knobs[gamma_pins::knobs::CHORD_VOL] * 99.0f));
        oled.WriteString(line_buf, Font_6x8, true);
    }
    else // MODE_TEST_TONE
    {
        oled.SetCursor(0, 24);
        snprintf(line_buf, sizeof(line_buf), "Tone: %4d Hz", (int)g_test_freq);
        oled.WriteString(line_buf, Font_6x8, true);

        oled.SetCursor(0, 34);
        const char* route = "STEREO (L+R)";
        if(g_knobs[gamma_pins::knobs::CHORD_VOL] < 0.35f) route = "LEFT ONLY";
        else if(g_knobs[gamma_pins::knobs::CHORD_VOL] > 0.65f) route = "RIGHT ONLY";
        snprintf(line_buf, sizeof(line_buf), "Chan: %-12s", route);
        oled.WriteString(line_buf, Font_6x8, true);
    }

    // Row 4: Knobs & Filters
    oled.SetCursor(0, 45);
    snprintf(line_buf, sizeof(line_buf), "CF:%d%% NF:%d%% Pan:%s",
             (int)(g_knobs[gamma_pins::knobs::CHORD_FILTER] * 99.0f),
             (int)(g_knobs[gamma_pins::knobs::NOTES_FILTER] * 99.0f),
             g_sticks[gamma_pins::joysticks::RIGHT_X] < 0.4f ? "L" : (g_sticks[gamma_pins::joysticks::RIGHT_X] > 0.6f ? "R" : "C"));
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
    hw.SetAudioBlockSize(48); // 1 ms @ 48 kHz
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

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
    hw.PrintLine("                       (Turn K0/K2 clockwise to increase volume)");
    hw.PrintLine("  - Left Stick (LX)  : Pitch bend");
    hw.PrintLine("  - Right Stick (RX) : Stereo pan");
    hw.PrintLine("  - Rotary Encoder   : Turn = Cycle Waveform, Click = Mode Toggle");
    hw.PrintLine("  - USB Commands     : 'm' (mode), 's' (speaker mute), 'w' (wave), 'b' (bootloader)\n");

    // 3. Initialize OLED Display (I2C1 @ 0x3D)
    InitOled();

    // 4. Initialize Speaker Amplifier Enable (PC3) & Battery Fault (PB12)
    // Start with speaker MUTED (LOW) to avoid audible startup transient/pop
    g_spk_en.Init(gamma_pins::system_pins::pin_speaker_en, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL);
    g_spk_en.Write(false); // Held LOW during hardware init
    g_pwr_fault.Init(gamma_pins::system_pins::pin_power_fault, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    // 5. Initialize 14 Key Switches (1 kHz debounce update rate)
    for(int i = 0; i < gamma_pins::chord_keys::COUNT; i++)
        g_chord_keys[i].Init(gamma_pins::chord_keys::pins[i], 1000.0f);
    for(int i = 0; i < gamma_pins::note_keys::COUNT; i++)
        g_note_keys[i].Init(gamma_pins::note_keys::pins[i], 1000.0f);

    // 6. Initialize Rotary Encoder & Click Switch
    g_enc_gpio_a.Init(gamma_pins::encoder::pin_a, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    g_enc_gpio_b.Init(gamma_pins::encoder::pin_b, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    g_enc_click.Init(gamma_pins::encoder::pin_click, 1000.0f);

    // 7. Start 1 kHz Hardware Timer for Encoder & Key Debounce
    TimerHandle::Config tim_cfg;
    tim_cfg.periph     = TimerHandle::Config::Peripheral::TIM_5;
    tim_cfg.dir        = TimerHandle::Config::CounterDir::UP;
    tim_cfg.enable_irq = true;
    tim_cfg.period     = 1000;
    g_timer.Init(tim_cfg);
    g_timer.SetPrescaler(240 - 1);
    g_timer.SetCallback(TimerCallback, nullptr);
    g_timer.Start();

    // 8. Initialize ADC (4 Knobs, 4 Joysticks, 1 Aux)
    AdcChannelConfig adc_cfg[NUM_ADC_CHANNELS];
    for(int i = 0; i < gamma_pins::knobs::COUNT; i++)
        adc_cfg[i].InitSingle(gamma_pins::knobs::pins[i]);
    for(int i = 0; i < gamma_pins::joysticks::COUNT; i++)
        adc_cfg[gamma_pins::knobs::COUNT + i].InitSingle(gamma_pins::joysticks::pins[i]);
    adc_cfg[gamma_pins::knobs::COUNT + gamma_pins::joysticks::COUNT].InitSingle(gamma_pins::aux_adc::pin);
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

    // 11. Allow audio codec output to stabilize, then unmute Speaker Amplifier (PC3 HIGH)
    System::Delay(60);
    g_speaker_enabled = true;
    g_spk_en.Write(g_speaker_enabled);
    hw.PrintLine("[AUDIO] Speaker amplifier enabled (PC3 = HIGH).");

    uint32_t last_screen_time = System::GetNow();
    uint32_t last_blink_time  = System::GetNow();
    int32_t  last_logged_pos  = 0;
    bool     led_state        = false;

    static bool s_note_held[gamma_pins::note_keys::COUNT]   = {false};
    static bool s_chord_held[gamma_pins::chord_keys::COUNT] = {false};

    while(1)
    {
        uint32_t now = System::GetNow();

        // Deferred DFU bootloader reboot (triggered from USB callback)
        if(g_reboot_bootloader)
        {
            hw.PrintLine("\n*** Rebooting into Daisy DFU Bootloader... ***\n");
            g_spk_en.Write(false); // Mute speaker amplifier before reset
            System::Delay(200);
            System::ResetToBootloader(System::DAISY_INFINITE_TIMEOUT);
        }

        // Check power fault / low battery monitor (PB12 active-LOW)
        static bool s_last_fault = false;
        bool pwr_fault = !g_pwr_fault.Read();
        if(pwr_fault != s_last_fault)
        {
            s_last_fault = pwr_fault;
            if(pwr_fault)
            {
                snprintf(g_last_event, sizeof(g_last_event), "LOW BATTERY / FAULT!");
                g_last_event_time = now;
                hw.PrintLine("[PWR] Battery Low / Power Fault Detected (PB12 = LOW)");
            }
        }

        // --- A. Drain Interrupt-Safe Key Event Ring Buffer ---
        while(g_eq_tail != g_eq_head)
        {
            KeyEvent ev = g_event_queue[g_eq_tail];
            __DSB(); // Ensure read of event data before advancing tail pointer
            g_eq_tail   = (g_eq_tail + 1) % kEventQueueSize;

            if(ev.id < gamma_pins::note_keys::COUNT)
            {
                // Note Keys (N1-N7)
                s_note_held[ev.id] = ev.pressed;
                if(ev.pressed)
                {
                    g_active_note_idx = ev.id;
                    snprintf(g_last_event, sizeof(g_last_event), "Note ON: %s", kNoteNames[ev.id]);
                    g_last_event_time = now;
                    hw.PrintLine("[NOTE] %s (%.1f Hz) PRESSED", kNoteNames[ev.id], kNoteFreqs[ev.id]);
                }
                else if(g_active_note_idx == (int)ev.id)
                {
                    // Fall back to most recent remaining held note, if any
                    int next_note = -1;
                    for(int k = gamma_pins::note_keys::COUNT - 1; k >= 0; k--)
                    {
                        if(s_note_held[k]) { next_note = k; break; }
                    }
                    g_active_note_idx = next_note;
                }
            }
            else if(ev.id < gamma_pins::note_keys::COUNT + gamma_pins::chord_keys::COUNT)
            {
                // Chord Keys (C1-C7)
                uint8_t c_idx = ev.id - gamma_pins::note_keys::COUNT;
                s_chord_held[c_idx] = ev.pressed;
                if(ev.pressed)
                {
                    g_active_chord_idx = c_idx;
                    snprintf(g_last_event, sizeof(g_last_event), "Chord ON: %s", kChords[c_idx].name);
                    g_last_event_time = now;
                    hw.PrintLine("[CHORD] %s PRESSED", kChords[c_idx].name);
                }
                else if(g_active_chord_idx == (int)c_idx)
                {
                    // Fall back to most recent remaining held chord, if any
                    int next_chord = -1;
                    for(int k = gamma_pins::chord_keys::COUNT - 1; k >= 0; k--)
                    {
                        if(s_chord_held[k]) { next_chord = k; break; }
                    }
                    g_active_chord_idx = next_chord;
                }
            }
            else if(ev.id == 14 && ev.pressed)
            {
                // Short Encoder Click (< 600ms) -> Toggle Mode
                g_current_mode = (g_current_mode == MODE_SYNTH) ? MODE_TEST_TONE : MODE_SYNTH;
                snprintf(g_last_event, sizeof(g_last_event), "Mode: %s",
                         (g_current_mode == MODE_SYNTH) ? "SYNTH" : "TEST TONE");
                g_last_event_time = now;
                hw.PrintLine("[MODE] Mode toggled to %s",
                             (g_current_mode == MODE_SYNTH) ? "SYNTH" : "TEST TONE");
            }
            else if(ev.id == 16 && ev.pressed)
            {
                // Long Encoder Press (>= 600ms) -> Toggle Speaker Mute
                g_speaker_enabled = !g_speaker_enabled;
                g_spk_en.Write(g_speaker_enabled);
                snprintf(g_last_event, sizeof(g_last_event), "Speaker: %s",
                         g_speaker_enabled ? "ON" : "MUTED");
                g_last_event_time = now;
                hw.PrintLine("[SPK] Manual toggle -> Speaker Amp %s (PC3 = %d)",
                             g_speaker_enabled ? "ENABLED" : "MUTED",
                             g_speaker_enabled ? 1 : 0);
            }
        }

        // --- B. Read & Filter ADC Inputs (Knobs w/ Deadband, Joysticks w/ Responsive IIR) ---
        for(int i = 0; i < gamma_pins::knobs::COUNT; i++)
        {
            float raw_k = hw.adc.GetFloat(i);
            if(gamma_pins::knobs::invert[i])
                raw_k = 1.0f - raw_k;
            raw_k = fmaxf(0.0f, fminf(1.0f, raw_k));
            if(fabsf(raw_k - g_knobs[i]) > 0.005f)
                g_knobs[i] = raw_k;
        }

        for(int i = 0; i < gamma_pins::joysticks::COUNT; i++)
        {
            float raw_s = hw.adc.GetFloat(gamma_pins::knobs::COUNT + i);
            if(gamma_pins::joysticks::invert[i])
                raw_s = 1.0f - raw_s;
            raw_s = fmaxf(0.0f, fminf(1.0f, raw_s));
            g_sticks[i] += gamma_pins::joysticks::iir_coefficient * (raw_s - g_sticks[i]);
        }

        // Note: Voice frequencies, continuous pitch bend (Left Stick X), and test tone
        // frequencies are processed inside AudioCallback at 1 kHz block rate, ensuring
        // jitter-free audio completely independent of OLED I2C update latency.

        // --- D. Rotary Encoder Processing ---
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

        // --- E. OLED Screen Refresh (~30 Hz) ---
        if(now - last_screen_time >= 33)
        {
            last_screen_time = now;
            UpdateScreen();
        }

        // --- F. LED Heartbeat (1 Hz) ---
        if(now - last_blink_time >= 500)
        {
            last_blink_time = now;
            led_state = !led_state;
            hw.SetLed(led_state);
        }

        // Clear stale last event
        if(g_last_event_time > 0 && (now - g_last_event_time > 3000))
        {
            snprintf(g_last_event, sizeof(g_last_event), "Audio (48k) - %s",
                     g_speaker_enabled ? "SPK ON" : "MUTED");
            g_last_event_time = 0;
        }

        System::Delay(1);
    }
}
