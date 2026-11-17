#pragma once

#include "core/Library.h"
#include <juce_dsp/juce_dsp.h>

/** Réglages de découpe d'une source. */
struct SliceSettings
{
    enum Mode { Transients = 0, Grid, Bands, Random };
    int mode = Transients;
    float threshold = 0.5f;    // transitoires : seuil 0..1
    float sensitivity = 0.5f;  // transitoires : sensibilité 0..1
    int gridDivision = 2;      // 0=1/4 1=1/8 2=1/16 3=1/32 4=1/64
    float randomMinMs = 60.0f;
    float randomMaxMs = 400.0f;
};

struct SliceRegion { int start; int length; };

namespace Slicer
{
    /** Découpe un signal mono. `maxSlices` borne le résultat. */
    std::vector<SliceRegion> slice (const std::vector<float>& mono, double sampleRate, double bpm,
                                    const SliceSettings&, int maxSlices, juce::Random& rng);

    /** Recale les bords sur le passage par zéro le plus proche (±2 ms). */
    void snapToZeroCrossings (const std::vector<float>& mono, std::vector<SliceRegion>&, double sampleRate);

    /** Sépare un buffer stéréo en 4 bandes (Linkwitz-Riley). */
    std::array<juce::AudioBuffer<float>, 4> splitBands (const juce::AudioBuffer<float>& in, double sampleRate);
}

namespace Analyzer
{
    Descriptors analyse (const std::vector<float>& mono, double sampleRate);
}
