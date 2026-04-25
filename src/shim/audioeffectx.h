// audioeffectx.h — minimal VST2 shim for bare-metal AirWindows
// Maps the VST2 API surface to our platform.
// AirWindows plugins use a small subset: processReplacing, params, sample rate.
// SPDX-License-Identifier: BSD-2-Clause
#ifndef AUDIOEFFECTX_H
#define AUDIOEFFECTX_H

#include <stdint.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef int32_t VstInt32;
typedef intptr_t audioMasterCallback;

// VST2 constants
enum VstPlugCategory {
    kPlugCategUnknown = 0,
    kPlugCategEffect = 1,
    kPlugCategSynth = 2,
    kPlugCategAnalysis = 3,
    kPlugCategMastering = 4,
    kPlugCategSpacializer = 5,
    kPlugCategRoomFx = 6,
    kPlugSurroundFx = 7,
    kPlugCategRestoration = 8,
    kPlugCategGenerator = 11
};

enum {
    kVstMaxProgNameLen = 24,
    kVstMaxParamStrLen = 8,
    kVstMaxProductStrLen = 64,
    kVstMaxVendorStrLen = 64
};

class AudioEffect {
public:
    AudioEffect(audioMasterCallback master, int numPrograms, int numParams)
        : sampleRate_(44100.0), numParams_(numParams) {
        (void)master; (void)numPrograms;
    }
    virtual ~AudioEffect() {}
    virtual void processReplacing(float** inputs, float** outputs, VstInt32 sampleFrames) = 0;
    virtual void processDoubleReplacing(double** inputs, double** outputs, VstInt32 sampleFrames) {
        (void)inputs; (void)outputs; (void)sampleFrames;
    }
    virtual void setParameter(VstInt32 index, float value) { (void)index; (void)value; }
    virtual float getParameter(VstInt32 index) { (void)index; return 0.0f; }
    virtual void getParameterName(VstInt32 index, char* text) { (void)index; text[0] = 0; }
    virtual void getParameterDisplay(VstInt32 index, char* text) { (void)index; text[0] = 0; }
    virtual void getParameterLabel(VstInt32 index, char* text) { (void)index; text[0] = 0; }
    virtual void getProgramName(char* name) { name[0] = 0; }
    virtual void setProgramName(char* name) { (void)name; }
    double getSampleRate() { return sampleRate_; }
    void setSampleRate(double rate) { sampleRate_ = rate; }
    void setNumInputs(int n) { (void)n; }
    void setNumOutputs(int n) { (void)n; }
    void setUniqueID(unsigned long id) { (void)id; }
    void canProcessReplacing() {}
    void canDoubleReplacing() {}
    void programsAreChunks(bool b = true) { (void)b; }
    void vst_strncpy(char* dst, const char* src, int n) {
        for (int i = 0; i < n && src[i]; ++i) dst[i] = src[i];
    }
    void float2string(float value, char* text, int maxLen) {
        // minimal: just write the integer part
        int v = (int)value;
        if (v < 0) { *text++ = '-'; v = -v; maxLen--; }
        char buf[16]; int i = 0;
        do { buf[i++] = '0' + (v % 10); v /= 10; } while (v && i < 15);
        for (int j = i - 1; j >= 0 && maxLen > 1; --j, --maxLen) *text++ = buf[j];
        *text = 0;
    }
    void int2string(int value, char* text, int maxLen) {
        float2string((float)value, text, maxLen);
    }
    void dB2string(float value, char* text, int maxLen) {
        float db = 20.0f * log10f(value > 0.0f ? value : 0.0001f);
        float2string(db, text, maxLen);
    }
protected:
    double sampleRate_;
    int numParams_;
};

typedef AudioEffect AudioEffectX;

#endif
