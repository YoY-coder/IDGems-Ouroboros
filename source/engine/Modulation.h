#pragma once

#include "core/Params.h"
#include "core/Library.h"
#include <atomic>

struct Transport
{
    bool playing = false;  // le séquenceur avance
    double ppq = 0.0;      // position en noires au début du bloc
    double bpm = 120.0;
};

/**
    4 LFO à forme dessinée + 2 modulateurs aléatoires + 8 macros + macro Chaos.
    Calcule, une fois par bloc, la valeur modulée de chaque paramètre.
*/
class ModEngine
{
public:
    ModEngine() { reset(); }
    void prepare (double sampleRate);
    void reset();

    /** Calcule `out` (valeurs plain modulées) à partir de `base`. Temps réel : sans allocation. */
    void compute (const P::Values& base, const ModSnapshot& mods, const Transport& t, int numSamples,
                  P::Values& out, float& filterCutoffRight);

    /** Valeurs actuelles des sources (-1..1 pour LFO/RND, 0..1 macros), pour l'affichage. */
    std::array<std::atomic<float>, numModSources> sourceValues;
    std::array<std::atomic<float>, kNumLfos> lfoPhases;

private:
    double sr = 44100.0;
    std::array<double, kNumLfos> phase {};
    std::array<float, kNumRnd> rndValue {}, rndTarget {};
    std::array<double, kNumRnd> rndCounter {};
    juce::Random rng { 1234 };

    static float readShape (const std::array<float, kLfoPoints>& s, double phase) noexcept;
};
