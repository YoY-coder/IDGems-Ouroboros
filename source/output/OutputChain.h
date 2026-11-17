#pragma once

#include "core/Params.h"
#include <juce_dsp/juce_dsp.h>

/**
    Chaîne de sortie : passe-haut → grave en mono → OTT (3 bandes) → soft clip → gain → limiteur.
*/
class OutputChain
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* L, float* R, int n, const P::Values& v);

private:
    double sr = 44100.0;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 1> hp;
    juce::dsp::LinkwitzRileyFilter<float> monoSplit;
    juce::dsp::LinkwitzRileyFilter<float> xoLow, xoHigh;
    std::array<float, 3> envDb { -100, -100, -100 };
    std::array<float, 3> gainDb {};
    float limGain = 1.0f;
    juce::AudioBuffer<float> scratch;
    juce::SmoothedValue<float> outGain;
};
