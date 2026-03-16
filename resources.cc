// Copyright 2014 Emilie Gillet.
// Adapted for Daisy Petal port of Clouds.
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// Lookup table definitions and runtime initialisation.

#include "resources.h"

#include <stdint.h>
#include <math.h>

#ifndef PI_F
#define PI_F   3.14159265358979323846f
#endif
#ifndef TWOPI_F
#define TWOPI_F 6.28318530717958647692f
#endif

// ---------------------------------------------------------------------------
// Table storage
// ---------------------------------------------------------------------------

int16_t  lut_ulaw[LUT_ULAW_SIZE];
float    lut_pitch_ratio_high[LUT_PITCH_RATIO_HIGH_SIZE];
float    lut_pitch_ratio_low[LUT_PITCH_RATIO_LOW_SIZE];
uint16_t atan_lut[ATAN_LUT_SIZE];

float lut_sin[LUT_SIN_SIZE];
float lut_window[LUT_WINDOW_SIZE];
float lut_xfade_in[LUT_XFADE_IN_SIZE];
float lut_xfade_out[LUT_XFADE_OUT_SIZE];
float lut_sine_window_4096[LUT_SINE_WINDOW_4096_SIZE];
float lut_grain_size[LUT_GRAIN_SIZE_SIZE];

// FIR half-band filter for 2× sample-rate conversion (45 taps, symmetric).
const float src_filter_1x_2_45[SRC_FILTER_1X_2_45_SIZE] = {
    -6.928606892e-04f, -5.894682972e-03f,  4.393903915e-04f,  5.352009980e-03f,
     1.833575577e-03f, -7.103853054e-03f, -5.275577768e-03f,  7.999060050e-03f,
     1.029879712e-02f, -7.191125897e-03f, -1.675763381e-02f,  3.628265970e-03f,
     2.423749384e-02f,  4.020326715e-03f, -3.208822586e-02f, -1.775516900e-02f,
     3.947412082e-02f,  4.200610725e-02f, -4.553678524e-02f, -9.270618476e-02f,
     4.952442102e-02f,  3.157869177e-01f,  4.528032253e-01f,  3.157869177e-01f,
     4.952442102e-02f, -9.270618476e-02f, -4.553678524e-02f,  4.200610725e-02f,
     3.947412082e-02f, -1.775516900e-02f, -3.208822586e-02f,  4.020326715e-03f,
     2.423749384e-02f,  3.628265970e-03f, -1.675763381e-02f, -7.191125897e-03f,
     1.029879712e-02f,  7.999060050e-03f, -5.275577768e-03f, -7.103853054e-03f,
     1.833575577e-03f,  5.352009980e-03f,  4.393903915e-04f, -5.894682972e-03f,
    -6.928606892e-04f,
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline float fclamp(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

// Compensate the sine-window so that overlapping grains sum to a flat envelope.
static void sum_window(float* window, int steps, float* output)
{
    int n      = LUT_SINE_WINDOW_4096_SIZE;
    int stride = n / steps;
    int start  = 0;

    for(int i = 0; i < LUT_SINE_WINDOW_4096_SIZE; i++)
        output[i] = 0.f;

    for(int i = 0; i < steps; i++)
    {
        for(int j = start; j < start + stride; j++)
        {
            output[j - start] += powf(window[j], 2.f);
            output[j]          = output[j - start];
        }
        start += stride;
    }
}

// μ-law decoder (for 8-bit buffer mode)
static inline int16_t MuLaw2Lin(uint8_t u_val)
{
    int16_t t;
    u_val = ~u_val;
    t     = ((u_val & 0xf) << 3) + 0x84;
    t <<= ((unsigned)u_val & 0x70) >> 4;
    return (u_val & 0x80) ? (int16_t)(0x84 - t) : (int16_t)(t - 0x84);
}

// ---------------------------------------------------------------------------
// InitResources
// ---------------------------------------------------------------------------

void InitResources(float /*sample_rate*/)
{
    // lut_ulaw
    for(int i = 0; i < LUT_ULAW_SIZE; i++)
        lut_ulaw[i] = MuLaw2Lin((uint8_t)i);

    // lut_pitch_ratio_high  — semitone ratios centred on index 128 (= 0 semitones)
    // lut_pitch_ratio_low   — fractional semitone fine-tune (1/256 semitone steps)
    for(int i = 0; i < LUT_PITCH_RATIO_HIGH_SIZE; i++)
    {
        lut_pitch_ratio_high[i] = powf(2.f, ((float)i - 128.f) / 12.f);
        lut_pitch_ratio_low[i]  = powf(2.f, (float)i / 256.0f / 12.0f);
    }

    // atan_lut  (used by stmlib fast atan approximation)
    for(size_t i = 0; i < ATAN_LUT_SIZE; ++i)
        atan_lut[i] = (uint16_t)(65536.0 / (2.0 * PI_F) * asinf((float)i / 512.0f));

    // lut_sin  — full cycle, 1024 samples, plus one guard point
    for(int i = 0; i < LUT_SIN_SIZE; i++)
        lut_sin[i] = sinf((float)i / 1024.f * TWOPI_F);

    // lut_window  — raised-cosine (Hann) window, 4096 samples, plus guard
    for(int i = 0; i < LUT_WINDOW_SIZE; i++)
    {
        float t      = (float)i / (float)(LUT_WINDOW_SIZE - 1);
        lut_window[i] = 1.f - ((cosf(t * PI_F) + 1.f) * 0.5f);
    }

    // lut_xfade_in / lut_xfade_out  — equal-power crossfade curves (17 points)
    {
        const float two_neg_half = powf(2.f, -0.5f);
        for(int i = 0; i < LUT_XFADE_IN_SIZE; i++)
        {
            float t        = (float)i / (float)(LUT_XFADE_IN_SIZE - 1);
            t              = fclamp(1.04f * t - 0.02f, 0.f, 1.f);
            t             *= PI_F / 2.f;
            lut_xfade_in[i]  = sinf(t) * two_neg_half;
            lut_xfade_out[i] = cosf(t) * two_neg_half;
        }
    }

    // lut_sine_window_4096  — raised-power sine window with overlap compensation
    for(int i = 0; i < LUT_SINE_WINDOW_4096_SIZE; i++)
    {
        float t = (float)i / (float)LUT_SINE_WINDOW_4096_SIZE;
        lut_sine_window_4096[i] = powf(1.f - powf(2.f * t - 1.f, 2.f), 1.25f);
    }
    {
        float compensation[LUT_SINE_WINDOW_4096_SIZE];
        sum_window(lut_sine_window_4096, 2, compensation);
        for(int i = 0; i < LUT_SINE_WINDOW_4096_SIZE; i++)
        {
            compensation[i] = powf(compensation[i], 0.5f);
            lut_sine_window_4096[i] /= compensation[i];
        }
    }

    // lut_grain_size  — grain size in samples as a function of the Size knob
    // Maps [0,1] → [1024, 16384] samples on a 2^4 exponential scale.
    for(int i = 0; i < LUT_GRAIN_SIZE_SIZE; i++)
    {
        float size     = ((float)i / (float)LUT_GRAIN_SIZE_SIZE) * 4.f;
        lut_grain_size[i] = floorf(1024.f * powf(2.f, size));
    }
}
