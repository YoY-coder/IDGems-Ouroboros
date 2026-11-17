#pragma once

#include "core/Library.h"
#include <juce_dsp/juce_dsp.h>

/**
    Croisement de deux fragments, rendu hors temps réel (thread de fond).
    Le résultat devient un nouveau fragment de la bibliothèque.
*/
namespace CrossRenderer
{
    enum Mode { EnvelopeAonB = 0, Granular, Morph, Collage };

    /** Retourne un buffer stéréo au taux `sampleRate` (vide si échec). */
    juce::AudioBuffer<float> render (const SampleData& a, const Fragment& fa,
                                     const SampleData& b, const Fragment& fb,
                                     int mode, double sampleRate, juce::Random& rng);

    /** Extrait un fragment en float stéréo, rééchantillonné à `sampleRate`. */
    juce::AudioBuffer<float> extract (const SampleData& d, int start, int length, double sampleRate);

    void normalise (juce::AudioBuffer<float>&, float peakDb = -1.0f);
}
