# DaisySP Audio Callback Recipes & Blueprints

This reference document contains production-ready C++ DSP recipes for `libDaisy` and `DaisySP`.

---

## Recipe 1: Monophonic Subtractive Synth Voice

*Components: `Oscillator`, `LadderFilter`, `Adsr`, and `Overdrive`.*

```cpp
#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

static DaisySeed    hw;
static Oscillator   osc;
static LadderFilter flt;
static Adsr         amp_env;
static Adsr         flt_env;
static Overdrive    drive;

void AudioCallback(AudioHandle::InputBuffer in, 
                   AudioHandle::OutputBuffer out, 
                   size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        // 1. Evaluate control envelopes (gate held true)
        float a_env = amp_env.Process(true);
        float f_env = flt_env.Process(true);

        // 2. Modulate ladder filter cutoff dynamically
        flt.SetFreq(120.0f + f_env * 3800.0f);

        // 3. Audio generation, filtering, and saturation
        float sig = osc.Process();
        sig = flt.Process(sig);
        sig = drive.Process(sig);

        // 4. Output with VCA scaling
        float final_out = sig * a_env;
        out[0][i] = final_out; // Left
        out[1][i] = final_out; // Right
    }
}

int main(void)
{
    hw.Init();
    float sr = hw.AudioSampleRate();

    osc.Init(sr);
    osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
    osc.SetFreq(mtof(36)); // C2 bass
    osc.SetAmp(0.85f);

    flt.Init(sr);
    flt.SetFilterMode(LadderFilter::FilterMode::LP24);
    flt.SetRes(0.65f);
    flt.SetPassbandGain(0.3f);

    drive.Init();
    drive.SetDrive(0.25f);

    amp_env.Init(sr);
    amp_env.SetAttackTime(0.01f);
    amp_env.SetDecayTime(0.25f);
    amp_env.SetSustainLevel(0.3f);
    amp_env.SetReleaseTime(0.3f);

    flt_env.Init(sr);
    flt_env.SetAttackTime(0.04f);
    flt_env.SetDecayTime(0.35f);
    flt_env.SetSustainLevel(0.1f);
    flt_env.SetReleaseTime(0.2f);

    hw.StartAudio(AudioCallback);
    while (1) {}
}
```

---

## Recipe 2: Ambient Stereo Guitar Multi-FX Chain

*Components: `Overdrive`, `Chorus`, `DelayLine`, and `ReverbSc`.*

```cpp
#include "daisy_seed.h"
#include "daisysp.h"

#ifdef USE_DAISYSP_LGPL
#include "daisysp-lgpl.h"
#endif

using namespace daisy;
using namespace daisysp;

static DaisySeed hw;
static Overdrive drive;
static Chorus    chorus;

// High-capacity delay line and Reverb mapped to SDRAM
#define MAX_DELAY (48000 * 2) // 2 seconds @ 48 kHz
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delay_line;

#ifdef USE_DAISYSP_LGPL
static ReverbSc DSY_SDRAM_BSS reverb;
#endif

void AudioCallback(AudioHandle::InputBuffer in, 
                   AudioHandle::OutputBuffer out, 
                   size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        float mono_in = in[0][i];

        // 1. Soft saturation
        float sig = drive.Process(mono_in);

        // 2. Stereo chorus
        chorus.Process(sig);
        float ch_l = chorus.GetLeft();
        float ch_r = chorus.GetRight();

        // 3. Ping-pong delay line with Hermite cubic interpolation
        float del_sig = delay_line.ReadHermite(36000.0f); // 750 ms
        delay_line.Write(ch_l + del_sig * 0.45f);

        float wet_l = ch_l + del_sig * 0.5f;
        float wet_r = ch_r + del_sig * 0.5f;

#ifdef USE_DAISYSP_LGPL
        // 4. Algorithmic stereo reverberation
        float verb_l, verb_r;
        reverb.Process(wet_l, wet_r, &verb_l, &verb_r);
        out[0][i] = wet_l * 0.65f + verb_l * 0.35f;
        out[1][i] = wet_r * 0.65f + verb_r * 0.35f;
#else
        out[0][i] = wet_l;
        out[1][i] = wet_r;
#endif
    }
}
```

---

## Recipe 3: Algorithmic Techno Drum Sequencer

*Components: `Metro`, `Maytrig`, `AnalogBassDrum`, `AnalogSnareDrum`, `HiHat`.*

```cpp
#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

static DaisySeed       hw;
static Metro           clock;
static Maytrig         prob;
static AnalogBassDrum  kick;
static AnalogSnareDrum snare;
static HiHat           hat;
static uint32_t        step = 0;

void AudioCallback(AudioHandle::InputBuffer in, 
                   AudioHandle::OutputBuffer out, 
                   size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        // 16th-note clock tick (8 Hz @ 120 BPM)
        if (clock.Process())
        {
            if (step % 4 == 0)
                kick.Trig();

            if (step % 8 == 4)
                snare.Trig();

            if (step % 2 != 0 || prob.Process(0.35f))
                hat.Process(true);

            step = (step + 1) % 16;
        }

        // Sum drum voices
        float k = kick.Process(false);
        float s = snare.Process(false);
        float h = hat.Process(false);

        float mix = (k * 0.8f) + (s * 0.6f) + (h * 0.4f);
        out[0][i] = SoftClip(mix);
        out[1][i] = SoftClip(mix);
    }
}

int main(void)
{
    hw.Init();
    float sr = hw.AudioSampleRate();

    clock.Init(8.0f, sr); // 120 BPM 16th notes
    kick.Init(sr);
    kick.SetFreq(52.0f);
    kick.SetTone(0.6f);
    kick.SetDecay(0.45f);

    snare.Init(sr);
    snare.SetFreq(185.0f);
    snare.SetSnappy(0.7f);

    hat.Init(sr);
    hat.SetTone(0.85f);
    hat.SetDecay(0.2f);

    hw.StartAudio(AudioCallback);
    while (1) {}
}
```
