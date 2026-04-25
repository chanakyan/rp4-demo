// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// lv2_amp.c — LV2 amplifier plugin on bare-metal rp4
//
// The simplest possible LV2 plugin: gain control on stereo audio.
// Demonstrates that the LV2 spec compiles and runs on our platform
// with zero modifications to the LV2 headers.
//
// Ports:
//   0: gain (control, input) — dB
//   1: input_L (audio, input)
//   2: input_R (audio, input)
//   3: output_L (audio, output)
//   4: output_R (audio, output)

#include <lv2/core/lv2.h>
#include <math.h>
#include <stdlib.h>

#define AMP_URI "http://rp4.tba/plugins/amp"

typedef enum {
    AMP_GAIN    = 0,
    AMP_INPUT_L = 1,
    AMP_INPUT_R = 2,
    AMP_OUTPUT_L = 3,
    AMP_OUTPUT_R = 4
} PortIndex;

typedef struct {
    const float* gain;
    const float* input_L;
    const float* input_R;
    float*       output_L;
    float*       output_R;
} Amp;

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor,
            double                rate,
            const char*           bundle_path,
            const LV2_Feature* const* features)
{
    (void)descriptor; (void)rate; (void)bundle_path; (void)features;
    Amp* amp = (Amp*)calloc(1, sizeof(Amp));
    return (LV2_Handle)amp;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    Amp* amp = (Amp*)instance;
    switch ((PortIndex)port) {
    case AMP_GAIN:     amp->gain     = (const float*)data; break;
    case AMP_INPUT_L:  amp->input_L  = (const float*)data; break;
    case AMP_INPUT_R:  amp->input_R  = (const float*)data; break;
    case AMP_OUTPUT_L: amp->output_L = (float*)data;       break;
    case AMP_OUTPUT_R: amp->output_R = (float*)data;       break;
    }
}

static void
activate(LV2_Handle instance)
{
    (void)instance;
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
    Amp* amp = (Amp*)instance;
    const float  gain_db = *(amp->gain);
    const float  coef    = (gain_db > -90.0f)
                           ? powf(10.0f, gain_db * 0.05f)
                           : 0.0f;

    for (uint32_t i = 0; i < n_samples; ++i) {
        amp->output_L[i] = amp->input_L[i] * coef;
        amp->output_R[i] = amp->input_R[i] * coef;
    }
}

static void
deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void
cleanup(LV2_Handle instance)
{
    free(instance);
}

static const LV2_Descriptor descriptor = {
    AMP_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    NULL  /* extension_data */
};

const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    return (index == 0) ? &descriptor : NULL;
}
