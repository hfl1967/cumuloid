// Mutable Instruments Clouds -> Daisy Petal port
// Control layout v3 — fixed control processing

// ============================================================
// Single translation unit build — all Clouds DSP sources
// ============================================================
namespace stmlib {
    extern const float lut_pitch_ratio_high[257];
    extern const float lut_pitch_ratio_low[257];
}
#include "stmlib/dsp/units.cpp"
#include "stmlib/dsp/atan.cpp"
#include "stmlib/utils/random.cpp"
#include "resources.cpp"
#include "dsp/correlator.cpp"
#include "dsp/mu_law.cpp"
#include "dsp/pvoc/stft.cpp"
#include "dsp/pvoc/frame_transformation.cpp"
#include "dsp/pvoc/phase_vocoder.cpp"
#include "dsp/granular_processor.cpp"

// ============================================================
// Daisy + Clouds headers
// ============================================================
#include "daisy_petal.h"
#include "daisysp.h"
#include "dsp/granular_processor.h"

using namespace daisy;
using namespace daisysp;
using namespace clouds;

// ============================================================
// Hardware
// ============================================================
DaisyPetal hw;

// ============================================================
// Grain buffers — must live in SDRAM
// ============================================================
#define LARGE_BUFFER_SIZE (118784)
#define SMALL_BUFFER_SIZE (65536)
uint8_t DSY_SDRAM_BSS large_buffer[LARGE_BUFFER_SIZE];
uint8_t DSY_SDRAM_BSS small_buffer[SMALL_BUFFER_SIZE];

// ============================================================
// Clouds processor
// ============================================================
GranularProcessor processor;

// High pass filter on wet signal
daisysp::Svf hp_filter;

// Low pass filter on wet signal
daisysp::Svf lp_filter;

// ============================================================
// Output level (shift-controlled, not a Clouds parameter)
// ============================================================
float output_level = 1.2f;

// ============================================================
// Mode state
// ============================================================
int  mode      = 0;   // 0=Granular 1=Stretch 2=Looping 3=Spectral
bool bypass    = false;
bool freeze    = false;
bool reverb_on = true;
bool deterministic_seed = false;  // false = randomized grain positions

// ============================================================
// Knob parameters — all 6 initialized
// ============================================================
Parameter p_position, p_size, p_texture, p_density, p_pitch, p_blend;

// ============================================================
// Tap tempo state
// ============================================================
uint32_t last_tap_time  = 0;
float    tap_tempo_size = 0.5f;
bool     tap_active     = false;

// Blink state
uint32_t blink_interval  = 0;
uint32_t last_blink_time = 0;
bool     blink_state     = false;

// ============================================================
// Shift mode (encoder button held)
// ============================================================
bool shift_held = false;

// Secondary parameter values (controlled in shift mode)
float shift_reverb   = 0.5f;
float shift_feedback = 0.6f;
float shift_level    = 1.2f;
float shift_dry      = 1.3f;
float shift_hp_cutoff = 40.0f;  // Hz, start nearly flat
float shift_lp_cutoff = 12000.0f;  // Hz, start open

// Per-knob catch state for shift mode
struct KnobCatch {
    float secondary_value;
    float last_raw;
    bool  caught;
    bool  primary_valid;
    float primary_exit_raw;
};

KnobCatch catch_k1 = {0.5f, 0.0f, false, true, 0.0f};  // reverb
KnobCatch catch_k2 = {0.6f, 0.0f, false, true, 0.0f};  // feedback
KnobCatch catch_k6 = {0.6f, 0.0f, false, true, 0.0f};  // level
KnobCatch catch_k5 = {1.3f, 0.0f, false, true, 0.0f};  // dry level
KnobCatch catch_k4 = {0.0f, 0.0f, false, true, 0.0f};  // hp cutoff
KnobCatch catch_k3 = {1.0f, 0.0f, false, true, 0.0f};  // lp cutoff

// ============================================================
// Pitch snap intervals
// -24, -12, -7, 0, +7, +12, +24 semitones
// ============================================================
const float kSnapSemitones[] = {-24.f, -12.f, -7.f, 0.f, 7.f, 12.f, 24.f};
const int   kNumSnaps        = 7;

inline float SemitonesToKnob(float semitones) {
    return semitones / 96.0f + 0.5f;
}

inline float SnapPitch(float raw) {
    float best_dist = 1000.f;
    float best_knob = 0.5f;
    for (int i = 0; i < kNumSnaps; i++) {
        float knob_pos = SemitonesToKnob(kSnapSemitones[i]);
        float dist = fabsf(raw - knob_pos);
        if (dist < best_dist) {
            best_dist = dist;
            best_knob = knob_pos;
        }
    }
    return best_knob;
}

// ============================================================
// Ring LED mode colors (R, G, B) 0..1
// ============================================================
struct RgbColor { float r, g, b; };
const RgbColor kModeColors[4] = {
    {1.0f, 0.4f, 0.0f},   // Granular  — amber/orange
    {0.0f, 0.9f, 0.9f},   // Stretch   — cyan
    {0.0f, 0.9f, 0.2f},   // Looping   — green
    {0.7f, 0.0f, 1.0f},   // Spectral  — purple
};

// ============================================================
// Catch helper
// ============================================================
float ProcessCatch(KnobCatch& c, float raw, bool shift_active) {
    if (shift_active) {
        if (!c.caught) {
            bool passed = (raw > c.last_raw)
                ? (raw >= c.secondary_value)
                : (raw <= c.secondary_value);
            if (passed) c.caught = true;
        }
        c.last_raw = raw;
        if (c.caught) c.secondary_value = raw;
    } else {
        c.caught           = false;
        c.primary_valid    = false;
        c.primary_exit_raw = raw;
    }
    return c.secondary_value;
}

bool PrimaryActive(KnobCatch& c, float raw) {
    if (c.primary_valid) return true;
    if (fabsf(raw - c.primary_exit_raw) > 0.02f) c.primary_valid = true;
    return c.primary_valid;
}

// ============================================================
// Audio callback
// ============================================================
void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t                                size)
{
    // Separate analog + digital processing
    hw.ProcessAnalogControls();
    hw.ProcessDigitalControls();

    Parameters* params = processor.mutable_parameters();

    // ----------------------------------------------------------
    // Encoder — turn to change mode, hold for shift
    // ----------------------------------------------------------
    int enc_inc = hw.encoder.Increment();
    if (enc_inc != 0) {
        mode = (mode + enc_inc + PLAYBACK_MODE_LAST) % PLAYBACK_MODE_LAST;
        processor.set_playback_mode(static_cast<PlaybackMode>(mode));
    }
    shift_held = hw.encoder.Pressed();

    // ----------------------------------------------------------
    // Read all knobs via initialized Parameter objects
    // ----------------------------------------------------------
    float raw1      = p_position.Process();
    float raw2      = p_size.Process();
    float raw3      = p_texture.Process();
    float raw4      = p_density.Process();
    // Expand pitch knob center region with cubic curve
    float raw_pitch_lin = p_pitch.Process();
    float raw_pitch_centered = (raw_pitch_lin - 0.5f) * 2.0f;  // -1..1
    float raw_pitch_curved = raw_pitch_centered * raw_pitch_centered * raw_pitch_centered;  // cubic
    float raw_pitch = (raw_pitch_curved * 0.5f) + 0.5f;  // back to 0..1
    float raw6      = p_blend.Process();

    // ----------------------------------------------------------
    // Shift mode — secondary parameters with catch behavior
    // ----------------------------------------------------------
    if (shift_held) {
        shift_reverb   = ProcessCatch(catch_k1, raw1, true);
        shift_feedback = ProcessCatch(catch_k2, raw2, true);
        shift_level    = ProcessCatch(catch_k6, raw6, true);
        shift_dry      = ProcessCatch(catch_k5, raw_pitch, true);
        float hp_raw   = ProcessCatch(catch_k4, raw4, true);
        shift_hp_cutoff = 40.0f + hp_raw * 360.0f;  // 40-400Hz
        float lp_raw   = ProcessCatch(catch_k3, raw3, true);
        shift_lp_cutoff = 1000.0f + lp_raw * 11000.0f;  // 1k-12kHz
    } else {
        ProcessCatch(catch_k1, raw1, false);
        ProcessCatch(catch_k2, raw2, false);
        ProcessCatch(catch_k6, raw6, false);
        ProcessCatch(catch_k5, raw_pitch, false);
        ProcessCatch(catch_k4, raw4, false);
        ProcessCatch(catch_k3, raw3, false);
    }

    // ----------------------------------------------------------
    // Normal knob assignments
    // ----------------------------------------------------------

    // Position — catch-on-release after shift
    params->position = raw1;

    // Size — knob active unless tap tempo set; catch-on-release
    if (tap_active) {
            params->size = tap_tempo_size;
        } else {
            params->size = raw2;
        }

    // Texture and Density — always active
    params->texture = raw3;
    params->density = raw4;

    // Pitch — SW_7 (microswitch 3) up = snap, down = smooth
    bool  pitch_snap = hw.switches[DaisyPetal::SW_7].Pressed();
    float pitch_knob = pitch_snap ? SnapPitch(raw_pitch) : raw_pitch;
    params->pitch    = (pitch_knob - 0.5f) * 96.0f;  // -48 to +48 semitones

    // Blend — catch-on-release after shift
    params->dry_wet = raw6;  // full range 0-100%

    // ----------------------------------------------------------
    // Apply shift-controlled parameters
    // ----------------------------------------------------------
    output_level = shift_level;
    float dry_level = shift_dry;

    // Reverb: FS2 toggle gates it, amount from shift+knob1
    params->reverb = reverb_on ? shift_reverb : 0.0f;
    if (reverb_on) output_level *= 1.3f;  // compensate for reverb attenuation

    // Feedback: always on at default level
    params->feedback  = shift_feedback * 0.5f;

    // SW_6 (microswitch 2): deterministic seed toggle
    // Up = randomized grain positions, Down = locked/deterministic
    deterministic_seed = hw.switches[DaisyPetal::SW_6].Pressed();
    params->granular.use_deterministic_seed = deterministic_seed;
    params->granular.overlap = 0.5f;

    // Stereo spread — SW_5 (microswitch 1)
    params->stereo_spread = hw.switches[DaisyPetal::SW_5].Pressed() ? 1.0f : 0.0f;

    // ----------------------------------------------------------
    // Footswitches
    // ----------------------------------------------------------

    // FS1 (SW_1) — Freeze toggle
    if (hw.switches[DaisyPetal::SW_1].RisingEdge()) {
        freeze = !freeze;
        processor.set_freeze(freeze);
    }

    // FS2 (SW_2) — Reverb toggle
    if (hw.switches[DaisyPetal::SW_2].RisingEdge())
        reverb_on = !reverb_on;

    // FS3 (SW_3) — Bypass toggle
    if (hw.switches[DaisyPetal::SW_3].RisingEdge())
        bypass = !bypass;

    // FS4 (SW_4) — Tap tempo; hold 1s to clear
    if (hw.switches[DaisyPetal::SW_4].RisingEdge()) {
        uint32_t now      = System::GetNow();
        uint32_t interval = now - last_tap_time;
        last_tap_time     = now;
        if (interval > 100 && interval < 3000) {
            tap_tempo_size = (interval - 100.0f) / 2900.0f;
            tap_active     = true;
            blink_interval = interval;
        }
    }
    if (hw.switches[DaisyPetal::SW_4].TimeHeldMs() > 1000) {
        tap_active     = false;
        blink_interval = 0;
        blink_state    = false;
    }

    // ----------------------------------------------------------
    // Bypass
    // ----------------------------------------------------------
    if (bypass) {
        for (size_t i = 0; i < size; i++)
            out[i] = in[i];
        return;
    }

    // ----------------------------------------------------------
    // Convert float -> ShortFrame, run Clouds, convert back
    // ----------------------------------------------------------
    const size_t block_size = size / 2;
    ShortFrame input_frames[32];
    ShortFrame output_frames[32];

    for (size_t i = 0; i < block_size; i++) {
        input_frames[i].l = (int16_t)(in[i * 2]     * 32767.f);
        input_frames[i].r = (int16_t)(in[i * 2 + 1] * 32767.f);
    }

    processor.Prepare();
    processor.Process(input_frames, output_frames, block_size);

    // Update HP filter cutoff
    hp_filter.SetFreq(shift_hp_cutoff);
    hp_filter.SetRes(0.5f);
    lp_filter.SetFreq(shift_lp_cutoff);
    lp_filter.SetRes(0.5f);

    for (size_t i = 0; i < block_size; i++) {
        float wetl = (output_frames[i].l / 32767.f) * output_level;
        float wetr = (output_frames[i].r / 32767.f) * output_level;

        // High pass filter on wet signal
        hp_filter.Process(wetl);
        wetl = hp_filter.High();
        hp_filter.Process(wetr);
        wetr = hp_filter.High();

        // Low pass filter on wet signal
        lp_filter.Process(wetl);
        wetl = lp_filter.Low();
        lp_filter.Process(wetr);
        wetr = lp_filter.Low();

        // Soft clip to prevent first repeat from exceeding unity
        out[i * 2]     = tanhf(wetl * 0.6f) * 1.45f;
        out[i * 2 + 1] = tanhf(wetr * 0.6f) * 1.45f;
    }
}

// ============================================================
// Main
// ============================================================
int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(32);

    // Init all 6 knob parameters
    p_position.Init(hw.knob[DaisyPetal::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
    p_size.Init    (hw.knob[DaisyPetal::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
    p_texture.Init (hw.knob[DaisyPetal::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);
    p_density.Init (hw.knob[DaisyPetal::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);
    p_pitch.Init   (hw.knob[DaisyPetal::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
    p_blend.Init   (hw.knob[DaisyPetal::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);

    hw.StartAdc();
    
    // Read initial knob positions so parameters start at correct values
    // Must call StartAdc first, then delay briefly for ADC to settle
    hw.StartAdc();
    System::Delay(100);
    hw.ProcessAnalogControls();

    // Seed all parameters from current knob positions
    float init_pos   = p_position.Process();
    float init_size  = p_size.Process();
    float init_tex   = p_texture.Process();
    float init_den   = p_density.Process();
    float init_pitch = p_pitch.Process();
    float init_blend = p_blend.Process();

    // Pre-set catch structs so primary is immediately valid
    catch_k1.primary_exit_raw = init_pos;
    catch_k1.primary_valid    = true;
    catch_k2.primary_exit_raw = init_size;
    catch_k2.primary_valid    = true;
    catch_k6.primary_exit_raw = init_blend;
    catch_k6.primary_valid    = true;

    // Pre-set Clouds parameters from knob positions
    // (processor.Init happens next, these get applied on first audio block)
    output_level = 1.0f;
    
    // Init Clouds processor
    processor.Init(large_buffer, LARGE_BUFFER_SIZE,
                   small_buffer, SMALL_BUFFER_SIZE);
    processor.set_playback_mode(PLAYBACK_MODE_GRANULAR);
    processor.set_quality(0);  // 16-bit stereo

    // Init high pass filter
    float sample_rate = hw.AudioSampleRate();
    hp_filter.Init(sample_rate);
    hp_filter.SetFreq(40.0f);
    hp_filter.SetRes(0.5f);
    hp_filter.SetDrive(0.0f);

    lp_filter.Init(sample_rate);
    lp_filter.SetFreq(12000.0f);
    lp_filter.SetRes(0.5f);
    lp_filter.SetDrive(0.0f);

    hw.StartAudio(AudioCallback);

    // ----------------------------------------------------------
    // Main loop — LEDs only
    // ----------------------------------------------------------
    while (1) {
        System::Delay(10);
        hw.ClearLeds();

        uint32_t now = System::GetNow();

        // Tap tempo blink — short 80ms pulse
        if (tap_active && blink_interval > 0) {
            if (now - last_blink_time >= blink_interval) {
                last_blink_time = now;
                blink_state     = true;
            }
            if (blink_state && (now - last_blink_time) > 80)
                blink_state = false;
        }

        // Ring LEDs — mode color, blue tint when frozen, dim in shift
        float dim = shift_held ? 0.3f : 1.0f;
        for (int i = 0; i < 4; i++) {
            RgbColor c = kModeColors[i];
            if (i == mode) {
                if (freeze) {
                    c.r *= 0.3f;
                    c.g *= 0.3f;
                    c.b  = 1.0f;
                }
                hw.SetRingLed(static_cast<DaisyPetal::RingLed>(i),
                              c.r * dim, c.g * dim, c.b * dim);
            } else {
                hw.SetRingLed(static_cast<DaisyPetal::RingLed>(i),
                              0.03f * dim, 0.03f * dim, 0.03f * dim);
            }
        }

        // Footswitch LEDs
        hw.SetFootswitchLed(DaisyPetal::FOOTSWITCH_LED_1, freeze      ? 1.0f : 0.0f);
        hw.SetFootswitchLed(DaisyPetal::FOOTSWITCH_LED_2, reverb_on   ? 1.0f : 0.0f);
        hw.SetFootswitchLed(DaisyPetal::FOOTSWITCH_LED_3, bypass      ? 0.0f : 1.0f);
        hw.SetFootswitchLed(DaisyPetal::FOOTSWITCH_LED_4, blink_state ? 1.0f : 0.0f);

        hw.UpdateLeds();
    }
}
