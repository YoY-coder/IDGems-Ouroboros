#pragma once

#include "core/LockFree.h"
#include "core/Library.h"
#include "engine/Modulation.h"
#include "fx/FxRack.h"
#include <juce_audio_basics/juce_audio_basics.h>

/** Une voix qui lit un fragment (pitch par rééchantillonnage, reverse, fondus anti-clic). */
struct Voice
{
    SampleDataPtr data;
    int fragIndex = -1;
    int start = 0, length = 0;
    double pos = 0.0, inc = 1.0;
    int outTotal = 0, outDone = 0, fade = 64, delay = 0;
    float gainL = 1.0f, gainR = 1.0f;
    bool reverse = false;
    bool active = false;
};

/**
    Cœur du moteur : modulation + séquenceur + voix + rack FX.
    Utilisé en temps réel (dans le processeur) ET hors ligne (rendu des générations,
    loops, batch) : même son dans les deux cas.
*/
class EngineCore
{
public:
    static constexpr int kMaxVoices = 32;

    EngineCore();

    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** Poubelle pour les données relâchées (temps réel). nullptr = libération directe (hors ligne). */
    void setTrash (Trash* t) noexcept { trash = t; }

    /** Données courantes. En temps réel, mises à jour via Mailbox::fetch(). */
    std::shared_ptr<const LibrarySnapshot> library;
    std::shared_ptr<const Pattern> pattern;
    std::shared_ptr<const ModSnapshot> mods;

    /** Rend `n` échantillons du signal "wet" du moteur dans L/R (écrase le contenu). */
    void process (float* L, float* R, int n, const P::Values& base, const Transport& transport,
                  const juce::MidiBuffer* midi);

    /** Écoute directe d'un fragment (clic sur la carte). */
    void audition (int fragIndex);
    void panic();

    /** Graine des décisions de probabilité du séquenceur. */
    void setSeed (uint32_t s) noexcept { runSeed = s; rng.setSeed ((juce::int64) s); }

    const P::Values& getModulatedValues() const noexcept { return values; }
    ModEngine& getModEngine() noexcept { return mod; }

    // Télémétrie pour l'UI (lue par le thread UI)
    std::atomic<int> currentStep { -1 };
    std::atomic<int> lastFragment { -1 };
    std::atomic<uint32_t> triggerCounter { 0 };
    std::array<std::atomic<int>, kMaxVoices> voiceFrags;

private:
    void runSequencer (int n, const Transport& t);
    void triggerStep (int64_t stepAbs, int sub, int offset);
    void handleMidi (const juce::MidiBuffer& midi, int n);
    int pickFragment (int prev);
    int pickNearMap (float x, float y);
    void startVoice (int frag, int offset, float pitchSemis, bool reverse, float lengthFrac, float gain);
    void releaseVoice (Voice& v);
    void renderVoices (float* L, float* R, int n);

    double sr = 44100.0;
    int maxBlock = 512;
    std::array<Voice, kMaxVoices> voices;
    Trash* trash = nullptr;

    ModEngine mod;
    FxRack fx;
    P::Values values {};
    float cutoffRight = 2000.0f;

    juce::Random rng { 42 };
    uint32_t runSeed = 1;
    double internalPpq = 0.0;
    bool wasRunning = false;
    int64_t lastStepAbs = -1;
    int lastPicked = -1;
    int64_t pickedForStep = -1;
    int pickedFragForStep = -1;
};
