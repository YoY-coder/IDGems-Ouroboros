#pragma once

#include "core/Params.h"
#include "core/Library.h"
#include <juce_dsp/juce_dsp.h>

/**
    Rack de 7 effets, ordre modifiable, dry/wet par effet.
    0 Bitcrush  1 Distorsion  2 Filtre  3 Pitch/Freq shift  4 Stutter  5 Delay  6 Reverb
*/
class FxRack
{
public:
    static constexpr int kNumFx = 7;
    static const char* fxName (int i);

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* L, float* R, int n, const P::Values& v, const std::array<int, kNumFx>& order,
                  double bpm, float cutoffRight);

private:
    void crush (float* L, float* R, int n, const P::Values& v);
    void dist (float* L, float* R, int n, const P::Values& v);
    void filter (float* L, float* R, int n, const P::Values& v, float cutoffRight);
    void shift (float* L, float* R, int n, const P::Values& v);
    void stutter (float* L, float* R, int n, const P::Values& v, double bpm);
    void delay (float* L, float* R, int n, const P::Values& v, double bpm);
    void reverb (float* L, float* R, int n, const P::Values& v);

    double sr = 44100.0;
    int maxBlock = 512;
    std::vector<float> tmpL, tmpR;

    // Bitcrush
    float crushHoldL = 0, crushHoldR = 0, crushCounter = 0;

    // Filtre (un par canal pour la modulation stéréo)
    juce::dsp::StateVariableTPTFilter<float> filtL, filtR;

    // Pitch shift (deux têtes de lecture croisées) + frequency shift (Hilbert)
    std::vector<float> psBufL, psBufR;
    int psWrite = 0;
    double psPhase = 0.0;
    struct Allpass { float a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float process (float x) noexcept { float y = a2 * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; } };
    std::array<std::array<Allpass, 4>, 2> hilA, hilB;
    std::array<float, 2> hilDelay {};
    double fsPhase = 0.0;

    // Stutter
    std::vector<float> stBufL, stBufR;
    int stWrite = 0, stPos = 0, stRemaining = 0, stLoopStart = 0, stSliceLen = 1;
    juce::Random stRng { 99 };

    // Delay
    std::vector<float> dlBufL, dlBufR;
    int dlWrite = 0;
    juce::SmoothedValue<float> dlTime;
    float dlLpL = 0, dlLpR = 0;

    // Reverb
    juce::Reverb verb;
};
