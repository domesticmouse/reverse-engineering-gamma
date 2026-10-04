// ============================================================================
// Gamma Drum Synth
// ============================================================================
// Seven-voice drum synthesizer for the this.is.NOISE Gamma (Daisy Seed 2 DFM).
//
// Controls
//   Right keypad C1..C7 : Kick, Snare, Clap, Tom / Closed Hat, Open Hat, Cymbal
//   Encoder turn        : Select the drum being edited (shown on OLED)
//   Encoder click       : Audition the selected drum
//   Encoder hold 2 s    : Reboot into the Daisy bootloader for a firmware update
//   Knobs 1..4          : Level, Tune, Decay, Tone of the selected drum
//                         (soft takeover: a knob only takes effect once it
//                          passes the stored value, so sounds never jump)
//   Right stick X       : Master DJ filter (left = low-pass, right = high-pass)
//   Right stick Y       : Master drive (push up)
//
// USB serial (CDC on the USB-C port): 1-7 trigger, p params, s speaker, b bootloader, h help
// ============================================================================

#include "daisy_seed.h"
#include "daisysp.h"
#include "gamma_pins.h"
#include "drum_voices.h"
#include "dev/oled_ssd130x.h"
#include "util/oled_fonts.h"
#include "util/CpuLoadMeter.h"
#include <cstdio>
#include <cstdarg>
#include <cmath>

using namespace daisy;
using namespace daisysp;
using namespace drums;

static DaisySeed hw;

// ========================================================================
// Timing constants
// ========================================================================
static constexpr uint32_t kDfuHoldMs        = 2000; // Encoder hold time to enter bootloader
static constexpr uint32_t kDfuShowMs        = 400;  // Show the hold progress bar after this
static constexpr uint32_t kClickMaxMs       = 400;  // Release before this = audition click
static constexpr uint8_t  kKeyRearmMs       = 15;   // Key must be released this long to re-arm
static constexpr uint32_t kHitFlashMs       = 90;   // OLED hit-strip flash duration
static constexpr float    kKnobDeadband     = 0.004f;
static constexpr float    kKnobPickupWindow = 0.02f;

// ========================================================================
// Non-blocking, deadlock-immune USB CDC logger
// ========================================================================
// libDaisy's Logger<LOGGER_EXTERNAL> spins forever in TransmitSync() once a host
// terminal closes. This 4-buffer pool waits at most 500 us, then backs off for
// 200 ms, so logging can never stall the UI loop. (Same pattern as phase5_audio.)
struct UsbLog
{
    static void PrintLine(const char* format, ...)
    {
        static constexpr size_t kNumBufs = 4;
        static constexpr size_t kBufSize = 256;
        static char             s_bufs[kNumBufs][kBufSize];
        static size_t           s_cur             = 0;
        static uint32_t         s_last_timeout_ms = 0;

        char*   buf = s_bufs[s_cur];
        va_list args;
        va_start(args, format);
        int len = vsnprintf(buf, kBufSize - 3, format, args);
        va_end(args);
        if(len <= 0)
            return;
        if(len > (int)(kBufSize - 3))
            len = kBufSize - 3;
        buf[len++] = '\r';
        buf[len++] = '\n';
        buf[len]   = '\0';

        if(s_last_timeout_ms > 0 && (System::GetNow() - s_last_timeout_ms < 200))
        {
            if(hw.usb_handle.TransmitExternal((uint8_t*)buf, len) == UsbHandle::Result::OK)
            {
                s_last_timeout_ms = 0;
                s_cur             = (s_cur + 1) % kNumBufs;
            }
            return;
        }

        uint32_t start_us = System::GetUs();
        while(hw.usb_handle.TransmitExternal((uint8_t*)buf, len) != UsbHandle::Result::OK)
        {
            if(System::GetUs() - start_us >= 500)
            {
                s_last_timeout_ms = System::GetNow();
                return;
            }
        }
        s_last_timeout_ms = 0;
        s_cur             = (s_cur + 1) % kNumBufs;
    }
};

// USB RX runs in the USB ISR: only enqueue bytes here, never print.
static constexpr size_t kRxQueueSize = 64;
static volatile char    g_rx_queue[kRxQueueSize];
static volatile size_t  g_rx_head = 0;
static volatile size_t  g_rx_tail = 0;

static void UsbRxCallback(uint8_t* buff, uint32_t* length)
{
    if(!buff || !length)
        return;
    for(uint32_t i = 0; i < *length; i++)
    {
        size_t next = (g_rx_head + 1) % kRxQueueSize;
        if(next == g_rx_tail)
            break;
        g_rx_queue[g_rx_head] = (char)buff[i];
        __DSB();
        g_rx_head = next;
    }
}

// ========================================================================
// Shared state (ISR <-> audio <-> main loop)
// ========================================================================
// Trigger bits set by the 1 kHz key ISR (or USB / encoder click), consumed by audio
static volatile uint32_t g_pending_trigs = 0;
// Hit bits for the UI (flash + log), consumed by the main loop
static volatile uint32_t g_ui_hits = 0;

static inline void RequestTrigger(int drum)
{
    __atomic_fetch_or(&g_pending_trigs, 1u << drum, __ATOMIC_RELEASE);
    __atomic_fetch_or(&g_ui_hits, 1u << drum, __ATOMIC_RELEASE);
}

// Drum parameters: written by the main loop, read by the audio callback
static volatile float g_params[NUM_DRUMS][NUM_PARAMS];

static volatile float g_sticks[gamma_pins::joysticks::COUNT] = {0.5f, 0.5f, 0.5f, 0.5f};
static volatile float g_peak = 0.0f;

static volatile bool     g_reboot_bootloader = false;
static volatile bool     g_enc_click_event   = false;
static volatile uint32_t g_enc_hold_ms       = 0;
static volatile int32_t  g_enc_pos           = 0;

// ========================================================================
// Hardware objects
// ========================================================================
static GPIO        g_drum_keys[gamma_pins::chord_keys::COUNT];
static GPIO        g_enc_a, g_enc_b;
static Switch      g_enc_click;
static GPIO        g_spk_en;
static GPIO        g_pwr_fault;
static TimerHandle g_timer;
static bool        g_speaker_enabled = false;

constexpr size_t NUM_ADC_CHANNELS = gamma_pins::knobs::COUNT + gamma_pins::joysticks::COUNT;

// ========================================================================
// Audio engine
// ========================================================================
static Kick                 g_kick;
static Snare                g_snare;
static Clap                 g_clap;
static Tom                  g_tom;
static Metal<SquareNoise>   g_chat;
static Metal<SquareNoise>   g_ohat;
static Metal<RingModNoise>  g_cymbal;
static Svf                  g_master_filt;
static float                g_cutoff_smooth = 0.0f;
static float                g_pan_l[NUM_DRUMS], g_pan_r[NUM_DRUMS];
static CpuLoadMeter         g_cpu;

static void ApplyParams()
{
    auto p = [](int d, int i) { return g_params[d][i]; };
    g_kick.SetParams(p(KICK, TUNE), p(KICK, DECAY), p(KICK, TONE));
    g_snare.SetParams(p(SNARE, TUNE), p(SNARE, DECAY), p(SNARE, TONE));
    g_clap.SetParams(p(CLAP, TUNE), p(CLAP, DECAY), p(CLAP, TONE));
    g_tom.SetParams(p(TOM, TUNE), p(TOM, DECAY), p(TOM, TONE));
    g_chat.SetParams(p(CLOSED_HAT, TUNE), p(CLOSED_HAT, DECAY), p(CLOSED_HAT, TONE));
    g_ohat.SetParams(p(OPEN_HAT, TUNE), p(OPEN_HAT, DECAY), p(OPEN_HAT, TONE));
    g_cymbal.SetParams(p(CYMBAL, TUNE), p(CYMBAL, DECAY), p(CYMBAL, TONE));
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    g_cpu.OnBlockStart();

    // 1. Parameters (block rate)
    ApplyParams();
    float gain[NUM_DRUMS];
    for(int d = 0; d < NUM_DRUMS; d++)
    {
        float lvl = g_params[d][LEVEL];
        gain[d]   = lvl * lvl * kDrumInfo[d].gain;
    }

    // 2. Triggers (closed hat chokes open hat)
    uint32_t trigs = __atomic_exchange_n(&g_pending_trigs, 0u, __ATOMIC_ACQUIRE);
    if(trigs)
    {
        if(trigs & (1u << KICK))       g_kick.Trigger();
        if(trigs & (1u << SNARE))      g_snare.Trigger();
        if(trigs & (1u << CLAP))       g_clap.Trigger();
        if(trigs & (1u << TOM))        g_tom.Trigger();
        if(trigs & (1u << CLOSED_HAT)) { g_chat.Trigger(); g_ohat.Choke(); }
        if(trigs & (1u << OPEN_HAT))   g_ohat.Trigger();
        if(trigs & (1u << CYMBAL))     g_cymbal.Trigger();
    }

    // 3. Master DJ filter on right stick X (centre deadzone = bypass)
    float rx   = g_sticks[gamma_pins::joysticks::RIGHT_X];
    int   mode = 0; // 0 = bypass, -1 = low-pass, +1 = high-pass
    float target_cut;
    if(rx < 0.45f)
    {
        mode       = -1;
        target_cut = LogMap(rx / 0.45f, 120.0f, 16000.0f);
    }
    else if(rx > 0.55f)
    {
        mode       = 1;
        target_cut = LogMap((rx - 0.55f) / 0.45f, 20.0f, 6000.0f);
    }
    else
    {
        target_cut = g_cutoff_smooth > 0.0f ? g_cutoff_smooth : 1000.0f;
    }
    // Snap (rather than glide) when entering a filter mode, e.g. from bypass
    static int s_prev_mode = 0;
    if(mode != s_prev_mode)
        g_cutoff_smooth = target_cut;
    s_prev_mode = mode;
    fonepole(g_cutoff_smooth, target_cut, 0.2f);
    g_master_filt.SetFreq(g_cutoff_smooth);

    // 4. Master drive on right stick Y (push up)
    float ry    = g_sticks[gamma_pins::joysticks::RIGHT_Y];
    float drive = ry > 0.55f ? (ry - 0.55f) / 0.45f : 0.0f;
    float pre   = 1.0f + 5.0f * drive;
    float post  = 0.6f / (1.0f + 1.5f * drive);

    float peak = 0.0f;
    for(size_t i = 0; i < size; i++)
    {
        float v[NUM_DRUMS] = {};
        if(g_kick.IsActive())   v[KICK]       = g_kick.Process();
        if(g_snare.IsActive())  v[SNARE]      = g_snare.Process();
        if(g_clap.IsActive())   v[CLAP]       = g_clap.Process();
        if(g_tom.IsActive())    v[TOM]        = g_tom.Process();
        if(g_chat.IsActive())   v[CLOSED_HAT] = g_chat.Process();
        if(g_ohat.IsActive())   v[OPEN_HAT]   = g_ohat.Process();
        if(g_cymbal.IsActive()) v[CYMBAL]     = g_cymbal.Process();

        float l = 0.0f, r = 0.0f;
        for(int d = 0; d < NUM_DRUMS; d++)
        {
            float s = v[d] * gain[d];
            l += s * g_pan_l[d];
            r += s * g_pan_r[d];
        }

        // One mono filter instance: filter the mid signal; the side signal is
        // small (pans are near centre) and is just attenuated in low-pass mode.
        float mid  = 0.5f * (l + r);
        float side = 0.5f * (l - r);
        g_master_filt.Process(mid);
        if(mode < 0)
            mid = g_master_filt.Low(), side *= 0.5f;
        else if(mode > 0)
            mid = g_master_filt.High();
        l = mid + side;
        r = mid - side;

        l = SoftClip(l * pre) * post;
        r = SoftClip(r * pre) * post;

        out[0][i] = l;
        out[1][i] = r;
        float a   = fmaxf(fabsf(l), fabsf(r));
        if(a > peak)
            peak = a;
    }
    g_peak = peak > g_peak ? peak : g_peak * 0.93f;

    g_cpu.OnBlockEnd();
}

// ========================================================================
// 1 kHz timer ISR: encoder, encoder switch, low-latency drum keys
// ========================================================================
void TimerCallback(void* data)
{
    // --- Encoder quadrature FSM (from phase4/phase5) ---
    enum : uint8_t
    {
        R_START = 0, R_CW_FINAL, R_CW_BEGIN, R_CW_NEXT, R_CCW_BEGIN, R_CCW_FINAL, R_CCW_NEXT,
        DIR_CW = 0x10, DIR_CCW = 0x20
    };
    static const uint8_t kStateTable[7][4] = {
        {R_START, R_CW_BEGIN, R_CCW_BEGIN, R_START},
        {R_CW_NEXT, R_START, R_CW_FINAL, R_START | DIR_CW},
        {R_CW_NEXT, R_CW_BEGIN, R_START, R_START},
        {R_CW_NEXT, R_CW_BEGIN, R_CW_FINAL, R_START},
        {R_CCW_NEXT, R_START, R_CCW_BEGIN, R_START},
        {R_CCW_NEXT, R_CCW_FINAL, R_START, R_START | DIR_CCW},
        {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
    };
    static uint8_t s_state     = R_START;
    static uint8_t s_prev_quad = 0x03;
    uint8_t        quad        = (g_enc_a.Read() << 1) | g_enc_b.Read();
    if(quad != s_prev_quad)
    {
        s_prev_quad    = quad;
        s_state        = kStateTable[s_state & 0x0F][quad];
        uint8_t result = s_state & 0x30;
        if(result == DIR_CW)
            g_enc_pos = g_enc_pos + 1;
        else if(result == DIR_CCW)
            g_enc_pos = g_enc_pos - 1;
    }

    // --- Encoder switch: short click = audition, 2 s hold = bootloader ---
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

    // --- Drum keys: fire on the first pressed sample (<= 1 ms latency) ---
    // A key re-arms only after being continuously released for kKeyRearmMs,
    // which absorbs contact bounce on both press and release.
    static uint8_t s_release_ms[gamma_pins::chord_keys::COUNT] = {};
    static bool    s_armed[gamma_pins::chord_keys::COUNT]      = {};
    for(int i = 0; i < gamma_pins::chord_keys::COUNT; i++)
    {
        bool pressed = !g_drum_keys[i].Read(); // Active-low
        if(pressed)
        {
            s_release_ms[i] = 0;
            if(s_armed[i])
            {
                s_armed[i] = false;
                RequestTrigger(i);
            }
        }
        else if(s_release_ms[i] < kKeyRearmMs)
        {
            if(++s_release_ms[i] >= kKeyRearmMs)
                s_armed[i] = true;
        }
    }
}

// ========================================================================
// OLED (SSD1306 128x64, I2C1 @ 0x3D)
// ========================================================================
using MyOled = OledDisplay<SSD130xI2c128x64Driver>;
static MyOled oled;

static void HLine(int x0, int x1, int y, bool on)
{
    for(int x = x0; x <= x1; x++)
        oled.DrawPixel(x, y, on);
}
static void FillRect(int x0, int y0, int x1, int y1, bool on)
{
    for(int y = y0; y <= y1; y++)
        HLine(x0, x1, y, on);
}
static void Rect(int x0, int y0, int x1, int y1, bool on)
{
    HLine(x0, x1, y0, on);
    HLine(x0, x1, y1, on);
    for(int y = y0; y <= y1; y++)
    {
        oled.DrawPixel(x0, y, on);
        oled.DrawPixel(x1, y, on);
    }
}

static void InitOled()
{
    // Force-reset I2C1 so a soft reboot never leaves the bus locked
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
    gpio_init.Pin       = (1U << gamma_pins::display::pin_scl.pin)
                    | (1U << gamma_pins::display::pin_sda.pin);
    HAL_GPIO_Init(GPIOB, &gpio_init);

    MyOled::Config cfg;
    auto&          t = cfg.driver_config.transport_config;
    t.i2c_address               = gamma_pins::display::i2c_address;
    t.i2c_config.periph         = I2CHandle::Config::Peripheral::I2C_1;
    t.i2c_config.speed          = I2CHandle::Config::Speed::I2C_1MHZ;
    t.i2c_config.mode           = I2CHandle::Config::Mode::I2C_MASTER;
    t.i2c_config.pin_config.scl = gamma_pins::display::pin_scl;
    t.i2c_config.pin_config.sda = gamma_pins::display::pin_sda;
    oled.Init(cfg);
}

// UI state owned by the main loop
static int      g_selected = KICK;
static bool     g_latched[NUM_PARAMS];
static float    g_knob_pos[NUM_PARAMS];
static int8_t   g_knob_side[NUM_PARAMS]; // Which side of the stored value the knob started on
static uint32_t g_hit_time[NUM_DRUMS];

static void UpdateScreen(uint32_t now)
{
    char buf[24];
    oled.Fill(false);

    // Header
    oled.SetCursor(0, 0);
    oled.WriteString("GAMMA DRUMS", Font_6x8, true);
    oled.SetCursor(104, 0);
    oled.WriteString(g_speaker_enabled ? "SPK" : "MUT", Font_6x8, true);
    // Output meter between title and speaker status
    int meter = (int)(fminf(g_peak, 1.0f) * 30.0f);
    Rect(70, 1, 101, 6, true);
    if(meter > 0)
        FillRect(71, 2, 71 + meter, 5, true);
    HLine(0, 127, 9, true);

    // Selected drum
    oled.SetCursor(0, 12);
    oled.WriteString(kDrumInfo[g_selected].name, Font_11x18, true);
    snprintf(buf, sizeof(buf), "%d/7", g_selected + 1);
    oled.SetCursor(110, 12);
    oled.WriteString(buf, Font_6x8, true);

    uint32_t hold = g_enc_hold_ms;
    if(hold >= kDfuShowMs)
    {
        // Firmware-update hold progress replaces the parameter area
        Rect(0, 32, 127, 52, true);
        oled.SetCursor(4, 34);
        oled.WriteString("HOLD FOR UPDATE", Font_6x8, true);
        int w = (int)((hold - kDfuShowMs) * 119 / (kDfuHoldMs - kDfuShowMs));
        if(w > 119)
            w = 119;
        Rect(3, 44, 124, 50, true);
        if(w > 0)
            FillRect(4, 45, 4 + w, 49, true);
    }
    else
    {
        // Four parameter columns: label + bar. Unlatched knobs show a tick at
        // the knob's physical position; the label gets a '*' until picked up.
        for(int p = 0; p < NUM_PARAMS; p++)
        {
            int x = p * 32;
            snprintf(buf, sizeof(buf), "%s%s", kParamNames[p], g_latched[p] ? "" : "*");
            oled.SetCursor(x + 2, 32);
            oled.WriteString(buf, Font_6x8, true);

            int x0 = x + 1, x1 = x + 29, y0 = 42, y1 = 49;
            Rect(x0, y0, x1, y1, true);
            int fill = x0 + 1 + (int)(g_params[g_selected][p] * (x1 - x0 - 2));
            if(fill > x0 + 1)
                FillRect(x0 + 1, y0 + 1, fill, y1 - 1, true);
            if(!g_latched[p])
            {
                int kx = x0 + 1 + (int)(g_knob_pos[p] * (x1 - x0 - 2));
                for(int y = y0 - 2; y <= y1 + 2; y++)
                    oled.DrawPixel(kx, y, !(kx <= fill && y > y0 && y < y1));
            }
        }
    }

    // Hit strip: 7 cells, flash on hit, underline the selected drum
    for(int d = 0; d < NUM_DRUMS; d++)
    {
        int  x   = d * 18 + 1;
        bool hit = (now - g_hit_time[d]) < kHitFlashMs && g_hit_time[d] != 0;
        if(hit)
            FillRect(x, 54, x + 16, 62, true);
        oled.SetCursor(x + 3, 55);
        oled.WriteString(kDrumInfo[d].abbrev, Font_6x8, !hit);
        if(d == g_selected)
            HLine(x, x + 16, 63, true);
    }

    oled.Update();
}

static void ShowBootloaderScreen()
{
    oled.Fill(false);
    Rect(0, 0, 127, 63, true);
    oled.SetCursor(10, 8);
    oled.WriteString("FIRMWARE UPDATE", Font_6x8, true);
    oled.SetCursor(31, 24);
    oled.WriteString("DFU MODE", Font_7x10, true);
    oled.SetCursor(10, 42);
    oled.WriteString("Connect USB and", Font_6x8, true);
    oled.SetCursor(10, 52);
    oled.WriteString("run flash tool", Font_6x8, true);
    oled.Update();
}

// ========================================================================
// USB command handling (main loop context)
// ========================================================================
static void PrintHelp()
{
    UsbLog::PrintLine("\n--- Gamma Drum Synth USB Commands ---");
    UsbLog::PrintLine("  1-7 : Trigger Kick, Snare, Clap, Tom, C.Hat, O.Hat, Cymbal");
    UsbLog::PrintLine("  p   : Print all drum parameters and CPU load");
    UsbLog::PrintLine("  s   : Toggle internal speaker");
    UsbLog::PrintLine("  b   : Reboot into Daisy bootloader (firmware update)");
    UsbLog::PrintLine("  h/? : This help\n");
}

static void PrintParams()
{
    UsbLog::PrintLine("Drum        LVL  TUN  DEC  TON");
    for(int d = 0; d < NUM_DRUMS; d++)
        UsbLog::PrintLine("%-10s  %3d  %3d  %3d  %3d%s",
                          kDrumInfo[d].name,
                          (int)(g_params[d][LEVEL] * 100.0f),
                          (int)(g_params[d][TUNE] * 100.0f),
                          (int)(g_params[d][DECAY] * 100.0f),
                          (int)(g_params[d][TONE] * 100.0f),
                          d == g_selected ? "  <" : "");
    UsbLog::PrintLine("CPU avg %d%%  max %d%%",
                      (int)(g_cpu.GetAvgCpuLoad() * 100.0f),
                      (int)(g_cpu.GetMaxCpuLoad() * 100.0f));
}

static void ProcessUsbCommands()
{
    while(g_rx_tail != g_rx_head)
    {
        char c = g_rx_queue[g_rx_tail];
        __DSB();
        g_rx_tail = (g_rx_tail + 1) % kRxQueueSize;

        if(c >= '1' && c <= '7')
            RequestTrigger(c - '1');
        else if(c == 'b' || c == 'B')
            g_reboot_bootloader = true;
        else if(c == 's' || c == 'S')
        {
            g_speaker_enabled = !g_speaker_enabled;
            g_spk_en.Write(g_speaker_enabled);
            UsbLog::PrintLine("[SPK] Speaker %s", g_speaker_enabled ? "ON" : "MUTED");
        }
        else if(c == 'p' || c == 'P')
            PrintParams();
        else if(c == 'h' || c == 'H' || c == '?')
            PrintHelp();
    }
}

// ========================================================================
// Knob soft takeover
// ========================================================================
static void UnlatchKnobs()
{
    for(int p = 0; p < NUM_PARAMS; p++)
    {
        g_latched[p]   = false;
        g_knob_side[p] = (g_knob_pos[p] < g_params[g_selected][p]) ? -1 : 1;
    }
}

static void ReadKnobs()
{
    for(int p = 0; p < NUM_PARAMS; p++)
    {
        float raw = hw.adc.GetFloat(p);
        if(gamma_pins::knobs::invert[p])
            raw = 1.0f - raw;
        raw = fclamp(raw, 0.0f, 1.0f);
        if(fabsf(raw - g_knob_pos[p]) <= kKnobDeadband)
            continue;
        g_knob_pos[p] = raw;

        float stored = g_params[g_selected][p];
        if(!g_latched[p])
        {
            int8_t side = (raw < stored) ? -1 : 1;
            if(fabsf(raw - stored) < kKnobPickupWindow || side != g_knob_side[p])
                g_latched[p] = true;
        }
        if(g_latched[p])
            g_params[g_selected][p] = raw;
    }
}

// ========================================================================
// Main
// ========================================================================
int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(48); // 1 ms @ 48 kHz
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    // USB CDC on the external (USB-C) port
    hw.usb_handle.Init(UsbHandle::FS_EXTERNAL);
    hw.usb_handle.SetReceiveCallback(UsbRxCallback, UsbHandle::FS_EXTERNAL);

    // Speaker amp held muted during init to avoid pops
    g_spk_en.Init(gamma_pins::system_pins::pin_speaker_en, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL);
    g_spk_en.Write(false);
    g_pwr_fault.Init(gamma_pins::system_pins::pin_power_fault, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

    InitOled();

    // Drum parameters
    for(int d = 0; d < NUM_DRUMS; d++)
    {
        for(int p = 0; p < NUM_PARAMS; p++)
            g_params[d][p] = kDrumInfo[d].defaults[p];
        g_pan_l[d] = cosf(kDrumInfo[d].pan * 1.5707963f) * 1.4142136f;
        g_pan_r[d] = sinf(kDrumInfo[d].pan * 1.5707963f) * 1.4142136f;
    }

    // Controls
    for(int i = 0; i < gamma_pins::chord_keys::COUNT; i++)
        g_drum_keys[i].Init(gamma_pins::chord_keys::pins[i], GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    g_enc_a.Init(gamma_pins::encoder::pin_a, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    g_enc_b.Init(gamma_pins::encoder::pin_b, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    g_enc_click.Init(gamma_pins::encoder::pin_click, 1000.0f);

    AdcChannelConfig adc_cfg[NUM_ADC_CHANNELS];
    for(int i = 0; i < gamma_pins::knobs::COUNT; i++)
        adc_cfg[i].InitSingle(gamma_pins::knobs::pins[i]);
    for(int i = 0; i < gamma_pins::joysticks::COUNT; i++)
        adc_cfg[gamma_pins::knobs::COUNT + i].InitSingle(gamma_pins::joysticks::pins[i]);
    hw.adc.Init(adc_cfg, NUM_ADC_CHANNELS);
    hw.adc.Start();

    // 1 kHz control timer (TIM5, 240 MHz / 240 / 1000)
    TimerHandle::Config tim_cfg;
    tim_cfg.periph     = TimerHandle::Config::Peripheral::TIM_5;
    tim_cfg.dir        = TimerHandle::Config::CounterDir::UP;
    tim_cfg.enable_irq = true;
    tim_cfg.period     = 1000;
    g_timer.Init(tim_cfg);
    g_timer.SetPrescaler(240 - 1);
    g_timer.SetCallback(TimerCallback, nullptr);
    g_timer.Start();

    // Audio engine
    float sr = hw.AudioSampleRate();
    g_kick.Init(sr);
    g_snare.Init(sr);
    g_clap.Init(sr);
    g_tom.Init(sr);
    g_chat.Init(sr, 0.4f, 0.02f, 0.30f);
    g_ohat.Init(sr, 0.4f, 0.15f, 2.0f);
    g_cymbal.Init(sr, 0.3f, 0.5f, 6.0f);
    g_master_filt.Init(sr);
    g_master_filt.SetRes(0.25f);
    g_cpu.Init(sr, hw.AudioBlockSize());
    ApplyParams();

    // Knobs start unlatched so stored defaults are kept until each knob is moved through them
    System::Delay(5);
    for(int p = 0; p < NUM_PARAMS; p++)
    {
        float raw     = hw.adc.GetFloat(p);
        g_knob_pos[p] = gamma_pins::knobs::invert[p] ? 1.0f - raw : raw;
    }
    UnlatchKnobs();

    hw.StartAudio(AudioCallback);
    System::Delay(60);
    g_speaker_enabled = true;
    g_spk_en.Write(true);

    UsbLog::PrintLine("\n=== Gamma Drum Synth ===");
    PrintHelp();

    uint32_t last_screen = 0, last_blink = 0;
    int32_t  last_enc    = g_enc_pos;
    bool     led         = false;
    bool     last_fault  = false;

    while(1)
    {
        uint32_t now = System::GetNow();

        ProcessUsbCommands();

        // Firmware update: encoder held 2 s or USB 'b'
        if(g_reboot_bootloader)
        {
            UsbLog::PrintLine("\n*** Rebooting into Daisy bootloader for firmware update ***");
            g_spk_en.Write(false);
            hw.StopAudio();
            ShowBootloaderScreen();
            System::Delay(200);
            System::ResetToBootloader(System::DAISY_INFINITE_TIMEOUT);
        }

        // Hits -> UI flash + log
        uint32_t hits = __atomic_exchange_n(&g_ui_hits, 0u, __ATOMIC_ACQUIRE);
        for(int d = 0; hits && d < NUM_DRUMS; d++)
        {
            if(hits & (1u << d))
            {
                g_hit_time[d] = now ? now : 1;
                UsbLog::PrintLine("[HIT] %s", kDrumInfo[d].name);
            }
        }

        // Encoder: select drum
        int32_t enc = g_enc_pos;
        if(enc != last_enc)
        {
            int delta = (int)(enc - last_enc);
            last_enc  = enc;
            g_selected = ((g_selected + delta) % NUM_DRUMS + NUM_DRUMS) % NUM_DRUMS;
            UnlatchKnobs();
            UsbLog::PrintLine("[SEL] %s", kDrumInfo[g_selected].name);
        }
        if(g_enc_click_event)
        {
            g_enc_click_event = false;
            RequestTrigger(g_selected);
        }

        // Analog controls
        ReadKnobs();
        for(int i = 0; i < gamma_pins::joysticks::COUNT; i++)
        {
            float raw = hw.adc.GetFloat(gamma_pins::knobs::COUNT + i);
            if(gamma_pins::joysticks::invert[i])
                raw = 1.0f - raw;
            raw = fclamp(raw, 0.0f, 1.0f);
            g_sticks[i] += gamma_pins::joysticks::iir_coefficient * (raw - g_sticks[i]);
        }

        // Battery / power fault (PB12 active-low)
        bool fault = !g_pwr_fault.Read();
        if(fault != last_fault)
        {
            last_fault = fault;
            if(fault)
                UsbLog::PrintLine("[PWR] Battery low / power fault");
        }

        // OLED ~30 Hz (blocking ~10 ms; keys/encoder run in the timer ISR so nothing is missed)
        if(now - last_screen >= 33)
        {
            last_screen = now;
            UpdateScreen(now);
        }

        if(now - last_blink >= 500)
        {
            last_blink = now;
            led        = !led;
            hw.SetLed(led);
        }

        System::Delay(1);
    }
}
