#pragma once

// ============================================================================
// Gamma Drum Synth - Voice Definitions
// ============================================================================
// Seven single-voice drum instruments built on DaisySP's Plaits-derived drum
// models plus two small custom voices (clap and enveloped metallic noise).
//
// Every voice exposes the same interface so the engine can treat them uniformly:
//   Init(sample_rate)
//   SetParams(tune, decay, tone)   - all normalised 0..1, safe to call per block
//   Trigger()
//   Process()                      - returns one mono sample
//   IsActive()                     - false once the voice has rung out (CPU gate)
// ============================================================================

#include "daisysp.h"
#include <cmath>
#include <cstdint>

namespace drums
{

using namespace daisysp;

enum DrumId
{
    KICK = 0,
    SNARE,
    CLAP,
    TOM,
    CLOSED_HAT,
    OPEN_HAT,
    CYMBAL,
    NUM_DRUMS
};

enum ParamId
{
    LEVEL = 0,
    TUNE,
    DECAY,
    TONE,
    NUM_PARAMS
};

struct DrumInfo
{
    const char* name;               // Full name for the OLED (max 10 chars @ 11x18)
    const char* abbrev;             // 2-char name for the hit strip
    float       defaults[NUM_PARAMS]; // Level, Tune, Decay, Tone
    float       pan;                // 0 = left, 0.5 = centre, 1 = right
    float       gain;               // Make-up gain to roughly balance the voices
};

// Order matches the right-hand keypad (C1..C7):
//   Top row:    C1 Kick   C2 Snare   C3 Clap     C4 Tom
//   Bottom row: C5 C.Hat  C6 O.Hat   C7 Cymbal
constexpr DrumInfo kDrumInfo[NUM_DRUMS] = {
    {"KICK", "BD", {0.85f, 0.35f, 0.55f, 0.40f}, 0.50f, 1.00f},
    {"SNARE", "SD", {0.75f, 0.45f, 0.40f, 0.60f}, 0.45f, 1.00f},
    {"CLAP", "CP", {0.70f, 0.45f, 0.35f, 0.50f}, 0.55f, 1.40f},
    {"TOM", "TM", {0.75f, 0.40f, 0.50f, 0.40f}, 0.40f, 1.00f},
    {"CLOSED HAT", "CH", {0.60f, 0.55f, 0.30f, 0.65f}, 0.62f, 1.60f},
    {"OPEN HAT", "OH", {0.55f, 0.55f, 0.40f, 0.65f}, 0.62f, 1.60f},
    {"CYMBAL", "CY", {0.50f, 0.50f, 0.55f, 0.70f}, 0.35f, 1.40f},
};

constexpr const char* kParamNames[NUM_PARAMS] = {"LVL", "TUN", "DEC", "TON"};

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------
// Exponential mapping of 0..1 onto [lo, hi]
inline float LogMap(float x, float lo, float hi)
{
    return lo * powf(hi / lo, x);
}

// Per-sample multiplier for an exponential decay reaching -60 dB in `seconds`
inline float DecayCoef(float seconds, float sample_rate)
{
    return expf(-6.9078f / (seconds * sample_rate));
}

// Tracks whether a voice is still sounding so idle voices cost no CPU.
class ActivityGate
{
  public:
    void  Wake() { active_ = true; quiet_samples_ = 0; active_samples_ = 0; }
    bool  IsActive() const { return active_; }
    float Track(float s)
    {
        if(!active_)
            return 0.0f;
        if(++active_samples_ > kMaxActiveSamples)
        {
            active_ = false;
            return 0.0f;
        }
        if(fabsf(s) > 1.0e-4f)
            quiet_samples_ = 0;
        else if(++quiet_samples_ > kQuietLimit)
            active_ = false;
        return s;
    }

  private:
    static constexpr uint32_t kQuietLimit        = 4800;  // 100 ms of near-silence
    static constexpr uint32_t kMaxActiveSamples = 72000; // 1.5 s maximum lifetime ceiling
    bool                      active_            = false;
    uint32_t                  quiet_samples_     = 0;
    uint32_t                  active_samples_    = 0;
};

// ----------------------------------------------------------------------------
// Kick: 808-style bridged-T resonator (AnalogBassDrum)
//   Tune 30-90 Hz, Decay = resonator ring, Tone = click + attack FM punch
// ----------------------------------------------------------------------------
class Kick
{
  public:
    void Init(float sr)
    {
        bd_.Init(sr);
        bd_.SetAccent(0.8f);
        bd_.SetSelfFmAmount(0.25f);
    }
    void SetParams(float tune, float decay, float tone)
    {
        bd_.SetFreq(LogMap(tune, 30.0f, 90.0f));
        bd_.SetDecay(decay * 0.85f);
        bd_.SetTone(tone);
        bd_.SetAttackFmAmount(0.1f + 0.6f * tone);
    }
    void  Trigger() { bd_.Trig(), gate_.Wake(); }
    bool  IsActive() const { return gate_.IsActive(); }
    float Process() { return gate_.Track(bd_.Process()); }

  private:
    AnalogBassDrum bd_;
    ActivityGate   gate_;
};

// ----------------------------------------------------------------------------
// Tom: punchy analog tom with exponential pitch drop and body decay.
//   Replaces AnalogBassDrum whose SVF resonator enters perpetual undamped
//   self-oscillation above 100 Hz and exhausts CPU / starves control ISRs.
//   Tune 70-300 Hz, Decay 70-650 ms, Tone = pitch drop depth & attack punch
// ----------------------------------------------------------------------------
class Tom
{
  public:
    void Init(float sr)
    {
        sr_         = sr;
        phase_      = 0.0f;
        amp_env_    = 0.0f;
        pitch_env_  = 0.0f;
        click_env_  = 0.0f;
        SetParams(0.40f, 0.50f, 0.40f);
    }
    void SetParams(float tune, float decay, float tone)
    {
        base_freq_  = LogMap(tune, 70.0f, 300.0f);
        amp_coef_   = DecayCoef(LogMap(decay, 0.07f, 0.65f), sr_);
        pitch_coef_ = DecayCoef(0.022f, sr_); // Fast 22 ms pitch drop
        click_coef_ = DecayCoef(0.005f, sr_); // 5 ms attack transient
        fm_depth_   = 0.3f + 1.2f * tone;     // Pitch sweep depth
        tone_       = tone;
    }
    void Trigger()
    {
        amp_env_   = 1.0f;
        pitch_env_ = 1.0f;
        click_env_ = 1.0f;
        phase_     = 0.0f;
        gate_.Wake();
    }
    bool  IsActive() const { return gate_.IsActive(); }
    float Process()
    {
        if(!IsActive())
            return 0.0f;

        // Pitch envelope sweeps down to base frequency
        float inst_freq = base_freq_ * (1.0f + pitch_env_ * fm_depth_);
        phase_ += inst_freq / sr_;
        if(phase_ >= 1.0f)
            phase_ -= 1.0f;

        // Sine body with mild saturation for warm analog body
        float body = sinf(phase_ * 6.2831853f);
        body = body * (1.2f - 0.2f * body * body);

        // Click / transient punch
        float click = (phase_ < 0.5f ? 1.0f : -1.0f) * click_env_ * (0.1f + 0.35f * tone_);

        float out = (body + click) * amp_env_;

        // Decay envelopes
        amp_env_   *= amp_coef_;
        pitch_env_ *= pitch_coef_;
        click_env_ *= click_coef_;

        return gate_.Track(out);
    }

  private:
    float        sr_;
    float        phase_;
    float        base_freq_;
    float        amp_env_, amp_coef_;
    float        pitch_env_, pitch_coef_;
    float        click_env_, click_coef_;
    float        fm_depth_, tone_;
    ActivityGate gate_;
};

// ----------------------------------------------------------------------------
// Snare: 808-style resonators + noise (AnalogSnareDrum)
//   Tune 120-400 Hz, Decay = body/noise length, Tone = snappy (noise amount)
// ----------------------------------------------------------------------------
class Snare
{
  public:
    void Init(float sr)
    {
        sd_.Init(sr);
        sd_.SetAccent(0.8f);
        sd_.SetTone(0.5f);
    }
    void SetParams(float tune, float decay, float tone)
    {
        sd_.SetFreq(LogMap(tune, 120.0f, 400.0f));
        sd_.SetDecay(decay);
        sd_.SetSnappy(tone);
    }
    void  Trigger() { sd_.Trig(), gate_.Wake(); }
    bool  IsActive() const { return gate_.IsActive(); }
    float Process() { return gate_.Track(sd_.Process()); }

  private:
    AnalogSnareDrum sd_;
    ActivityGate    gate_;
};

// ----------------------------------------------------------------------------
// Clap: band-passed white noise with three fast "hand" bursts and a tail
//   Tune 600-2500 Hz band centre, Decay 50-600 ms tail, Tone = band -> bright mix
// ----------------------------------------------------------------------------
class Clap
{
  public:
    void Init(float sr)
    {
        sr_ = sr;
        noise_.Init();
        filt_.Init(sr);
        filt_.SetRes(0.35f);
        burst_coef_   = DecayCoef(0.012f, sr);
        burst_period_ = static_cast<uint32_t>(0.009f * sr);
        tail_coef_    = DecayCoef(0.25f, sr);
        t_            = kIdle;
    }
    void SetParams(float tune, float decay, float tone)
    {
        filt_.SetFreq(LogMap(tune, 600.0f, 2500.0f));
        tail_coef_ = DecayCoef(LogMap(decay, 0.05f, 0.6f), sr_);
        tone_      = tone;
    }
    void Trigger()
    {
        t_          = 0;
        burst_env_  = 1.0f;
        tail_env_   = 0.0f;
        gate_.Wake();
    }
    bool  IsActive() const { return gate_.IsActive(); }
    float Process()
    {
        if(t_ == kIdle)
            return gate_.Track(0.0f);

        // Re-strike the burst envelope at each "hand", then start the tail
        if(t_ == burst_period_ || t_ == 2 * burst_period_)
            burst_env_ = 1.0f;
        if(t_ == 3 * burst_period_)
            tail_env_ = 1.0f;
        if(t_ < 4 * burst_period_)
            t_++;

        burst_env_ *= burst_coef_;
        tail_env_ *= tail_coef_;
        float env = (t_ < 3 * burst_period_) ? burst_env_ : tail_env_ + burst_env_;

        filt_.Process(noise_.Process());
        float s = filt_.Band() * (1.0f - 0.5f * tone_) + filt_.High() * 0.6f * tone_;
        return gate_.Track(s * env);
    }

  private:
    static constexpr uint32_t kIdle = 0xFFFFFFFF;
    float                     sr_;
    WhiteNoise                noise_;
    Svf                       filt_;
    float                     burst_env_ = 0.0f, tail_env_ = 0.0f;
    float                     burst_coef_, tail_coef_, tone_ = 0.5f;
    uint32_t                  burst_period_, t_;
    ActivityGate              gate_;
};

// ----------------------------------------------------------------------------
// Metal: 808 metallic noise (HiHat in sustain mode) with our own envelope so
// decay can range from a tight closed hat to a multi-second cymbal wash.
// Supports choking (closed hat cuts the open hat).
// ----------------------------------------------------------------------------
template <typename NoiseSource>
class Metal
{
  public:
    void Init(float sr, float noisiness, float min_decay_s, float max_decay_s)
    {
        sr_          = sr;
        min_decay_s_ = min_decay_s;
        max_decay_s_ = max_decay_s;
        hh_.Init(sr);
        hh_.SetSustain(true); // Disable internal envelope; constant gain = accent * 0.5
        hh_.SetDecay(1.0f);
        hh_.SetAccent(1.0f);
        hh_.SetNoisiness(noisiness);
        coef_       = DecayCoef(min_decay_s, sr);
        choke_coef_ = DecayCoef(0.015f, sr);
    }
    void SetParams(float tune, float decay, float tone)
    {
        hh_.SetFreq(LogMap(tune, 1500.0f, 6000.0f));
        hh_.SetTone(tone);
        coef_ = DecayCoef(LogMap(decay, min_decay_s_, max_decay_s_), sr_);
    }
    void Trigger()
    {
        env_    = 1.0f;
        choked_ = false;
    }
    void  Choke() { choked_ = true; }
    bool  IsActive() const { return env_ > 1.0e-4f; }
    float Process()
    {
        if(!IsActive())
            return 0.0f;
        env_ *= choked_ ? choke_coef_ : coef_;
        return hh_.Process() * env_;
    }

  private:
    HiHat<NoiseSource> hh_;
    float              sr_;
    float              min_decay_s_, max_decay_s_;
    float              env_ = 0.0f, coef_, choke_coef_;
    bool               choked_ = false;
};

} // namespace drums
