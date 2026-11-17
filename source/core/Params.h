#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

/**
    Table unique de tous les paramètres automatisables.
    Colonnes : id, nom affiché, module, type, min, max, défaut, skew, choix ("a|b|c"), randomisable.

    Types : F = float, I = entier, B = bouton on/off, C = liste de choix.
*/
#define OUROBOROS_PARAMS(X) \
    /* --- Estomac --- */ \
    X(captureSeconds, "Capture (s)",        Stomach, F, 5,    120,   30,    0.5f, "", false) \
    X(captureSource,  "Capture depuis",     Stomach, C, 0,    1,     0,     1.0f, "Entree|Sidechain", false) \
    X(memoryBudget,   "Memoire max",        Stomach, C, 0,    2,     1,     1.0f, "48 Mo|96 Mo|192 Mo", false) \
    /* --- Séquenceur / moteur --- */ \
    X(seqRun,         "Lecture libre",      Seq, B, 0,    1,     0,     1.0f, "", false) \
    X(seqDivision,    "Pas",                Seq, C, 0,    3,     2,     1.0f, "1/4|1/8|1/16|1/32", true) \
    X(seqLength,      "Longueur",           Seq, I, 1,    64,    16,    1.0f, "", true) \
    X(swing,          "Swing",              Seq, F, 0,    0.75f, 0,     1.0f, "", true) \
    X(ruleRandom,     "Aleatoire",          Seq, F, 0,    1,     1,     1.0f, "", true) \
    X(ruleSimilar,    "Ressemblance",       Seq, F, 0,    1,     0.5f,  1.0f, "", true) \
    X(ruleContrast,   "Contraste",          Seq, F, 0,    1,     0.2f,  1.0f, "", true) \
    X(seqPitch,       "Pitch global",       Seq, F, -24,  24,    0,     1.0f, "", true) \
    X(seqLengthScale, "Longueur frag.",     Seq, F, 0.05f,1,     1,     1.0f, "", true) \
    X(seqReverse,     "Reverse global",     Seq, F, 0,    1,     0,     1.0f, "", true) \
    X(engineLevel,    "Niveau moteur",      Seq, F, -36,  6,     -6,    1.0f, "", false) \
    X(midiMode,       "Mode MIDI",          Seq, C, 0,    1,     0,     1.0f, "Clavier = carte|Chromatique", false) \
    X(velocitySens,   "Velocite",           Seq, F, 0,    1,     0.7f,  1.0f, "", true) \
    X(crossMode,      "Croisement",         Seq, C, 0,    3,     0,     1.0f, "Enveloppe A sur B|Granulaire|Morph A>B|Collage", true) \
    /* --- Ouroboros --- */ \
    X(genMode,        "Mode generations",   Ouro, C, 0,   1,     0,     1.0f, "Manuel|Auto", false) \
    X(genBars,        "Mesures",            Ouro, C, 0,   4,     2,     1.0f, "1|2|4|8|16", true) \
    X(mutation,       "Mutation",           Ouro, F, 0,   1,     0.3f,  1.0f, "", true) \
    X(dna,            "ADN",                Ouro, F, 0,   1,     0.4f,  1.0f, "", true) \
    X(death,          "Mort",               Ouro, F, 0,   1,     0.3f,  1.0f, "", true) \
    X(maxGens,        "Gen. max",           Ouro, I, 2,   32,    12,    1.0f, "", false) \
    /* --- FX : bitcrush --- */ \
    X(crushOn,        "Crush on",           Fx, B, 0,    1,     0,     1.0f, "", true) \
    X(crushMix,       "Crush mix",          Fx, F, 0,    1,     1,     1.0f, "", true) \
    X(crushBits,      "Bits",               Fx, F, 2,    16,    8,     1.0f, "", true) \
    X(crushDown,      "Downsample",         Fx, F, 1,    32,    4,     0.5f, "", true) \
    /* --- FX : distorsion --- */ \
    X(distOn,         "Dist on",            Fx, B, 0,    1,     0,     1.0f, "", true) \
    X(distMix,        "Dist mix",           Fx, F, 0,    1,     1,     1.0f, "", true) \
    X(distDrive,      "Drive",              Fx, F, 0,    1,     0.5f,  1.0f, "", true) \
    X(distMode,       "Type dist",          Fx, C, 0,    2,     0,     1.0f, "Tanh|Repli|Dur", true) \
    /* --- FX : filtre --- */ \
    X(filtOn,         "Filtre on",          Fx, B, 0,    1,     0,     1.0f, "", true) \
    X(filtMix,        "Filtre mix",         Fx, F, 0,    1,     1,     1.0f, "", true) \
    X(filtType,       "Type filtre",        Fx, C, 0,    2,     0,     1.0f, "Passe-bas|Passe-bande|Passe-haut", true) \
    X(filtCutoff,     "Coupure",            Fx, F, 20,   20000, 2000,  0.25f,"", true) \
    X(filtReso,       "Resonance",          Fx, F, 0.5f, 10,    0.9f,  0.4f, "", true) \
    /* --- FX : pitch / freq shift --- */ \
    X(shiftOn,        "Shift on",           Fx, B, 0,    1,     0,     1.0f, "", true) \
    X(shiftMix,       "Shift mix",          Fx, F, 0,    1,     1,     1.0f, "", true) \
    X(shiftMode,      "Type shift",         Fx, C, 0,    1,     0,     1.0f, "Pitch|Frequence", true) \
    X(shiftAmount,    "Decalage",           Fx, F, -1,   1,     0.25f, 1.0f, "", true) \
    /* --- FX : stutter --- */ \
    X(stutOn,         "Stutter on",         Fx, B, 0,    1,     0,     1.0f, "", true) \
    X(stutMix,        "Stutter mix",        Fx, F, 0,    1,     1,     1.0f, "", true) \
    X(stutDiv,        "Taille stutter",     Fx, C, 0,    3,     1,     1.0f, "1/8|1/16|1/32|1/64", true) \
    X(stutChance,     "Chance",             Fx, F, 0,    1,     0.3f,  1.0f, "", true) \
    X(stutRepeats,    "Repetitions",        Fx, I, 1,    8,     3,     1.0f, "", true) \
    /* --- FX : delay --- */ \
    X(dlyOn,          "Delay on",           Fx, B, 0,    1,     0,     1.0f, "", true) \
    X(dlyMix,         "Delay mix",          Fx, F, 0,    1,     0.3f,  1.0f, "", true) \
    X(dlyTime,        "Temps",              Fx, C, 0,    5,     3,     1.0f, "1/16|1/8|3/16|1/4|3/8|1/2", true) \
    X(dlyFeedback,    "Feedback",           Fx, F, 0,    0.95f, 0.4f,  1.0f, "", true) \
    X(dlyPingPong,    "Ping-pong",          Fx, B, 0,    1,     1,     1.0f, "", true) \
    /* --- FX : reverb --- */ \
    X(verbOn,         "Reverb on",          Fx, B, 0,    1,     0,     1.0f, "", true) \
    X(verbMix,        "Reverb mix",         Fx, F, 0,    1,     0.25f, 1.0f, "", true) \
    X(verbSize,       "Taille",             Fx, F, 0,    1,     0.6f,  1.0f, "", true) \
    X(verbDamp,       "Amorti",             Fx, F, 0,    1,     0.5f,  1.0f, "", true) \
    /* --- Sortie --- */ \
    X(ottAmount,      "OTT",                Out, F, 0,   1,     0.3f,  1.0f, "", true) \
    X(clipDrive,      "Soft clip",          Out, F, 0,   1,     0.2f,  1.0f, "", true) \
    X(limCeiling,     "Plafond",            Out, F, -12, 0,     -0.3f, 1.0f, "", false) \
    X(hpFreq,         "Passe-haut",         Out, F, 10,  300,   20,    0.5f, "", true) \
    X(monoFreq,       "Grave mono",         Out, F, 0,   300,   0,     1.0f, "", true) \
    X(dryWet,         "Dry/Wet",            Out, F, 0,   1,     0.5f,  1.0f, "", false) \
    X(outGain,        "Gain",               Out, F, -60, 12,    0,     2.0f, "", false) \
    /* --- Random / macros --- */ \
    X(randomAmount,   "Dosage random",      Rand, F, 0,  1,     0.6f,  1.0f, "", false) \
    X(chaos,          "CHAOS",              Rand, F, 0,  1,     0,     1.0f, "", false) \
    X(macro1,         "Macro 1",            Rand, F, 0,  1,     0,     1.0f, "", false) \
    X(macro2,         "Macro 2",            Rand, F, 0,  1,     0,     1.0f, "", false) \
    X(macro3,         "Macro 3",            Rand, F, 0,  1,     0,     1.0f, "", false) \
    X(macro4,         "Macro 4",            Rand, F, 0,  1,     0,     1.0f, "", false) \
    X(macro5,         "Macro 5",            Rand, F, 0,  1,     0,     1.0f, "", false) \
    X(macro6,         "Macro 6",            Rand, F, 0,  1,     0,     1.0f, "", false) \
    X(macro7,         "Macro 7",            Rand, F, 0,  1,     0,     1.0f, "", false) \
    X(macro8,         "Macro 8",            Rand, F, 0,  1,     0,     1.0f, "", false) \
    /* --- Modulation --- */ \
    X(lfo1Rate,  "LFO1 vitesse", Mod, F, 0.01f, 20, 0.5f, 0.3f, "", true) \
    X(lfo1Sync,  "LFO1 sync",    Mod, B, 0, 1, 1, 1.0f, "", true) \
    X(lfo1Div,   "LFO1 division",Mod, C, 0, 6, 2, 1.0f, "4 mes.|2 mes.|1 mes.|1/2|1/4|1/8|1/16", true) \
    X(lfo1Stereo,"LFO1 stereo",  Mod, F, 0, 1, 0, 1.0f, "", true) \
    X(lfo2Rate,  "LFO2 vitesse", Mod, F, 0.01f, 20, 0.5f, 0.3f, "", true) \
    X(lfo2Sync,  "LFO2 sync",    Mod, B, 0, 1, 1, 1.0f, "", true) \
    X(lfo2Div,   "LFO2 division",Mod, C, 0, 6, 3, 1.0f, "4 mes.|2 mes.|1 mes.|1/2|1/4|1/8|1/16", true) \
    X(lfo2Stereo,"LFO2 stereo",  Mod, F, 0, 1, 0, 1.0f, "", true) \
    X(lfo3Rate,  "LFO3 vitesse", Mod, F, 0.01f, 20, 0.5f, 0.3f, "", true) \
    X(lfo3Sync,  "LFO3 sync",    Mod, B, 0, 1, 1, 1.0f, "", true) \
    X(lfo3Div,   "LFO3 division",Mod, C, 0, 6, 4, 1.0f, "4 mes.|2 mes.|1 mes.|1/2|1/4|1/8|1/16", true) \
    X(lfo3Stereo,"LFO3 stereo",  Mod, F, 0, 1, 0, 1.0f, "", true) \
    X(lfo4Rate,  "LFO4 vitesse", Mod, F, 0.01f, 20, 0.5f, 0.3f, "", true) \
    X(lfo4Sync,  "LFO4 sync",    Mod, B, 0, 1, 1, 1.0f, "", true) \
    X(lfo4Div,   "LFO4 division",Mod, C, 0, 6, 5, 1.0f, "4 mes.|2 mes.|1 mes.|1/2|1/4|1/8|1/16", true) \
    X(lfo4Stereo,"LFO4 stereo",  Mod, F, 0, 1, 0, 1.0f, "", true) \
    X(rnd1Rate,  "RND1 vitesse", Mod, F, 0.01f, 20, 1, 0.3f, "", true) \
    X(rnd1Smooth,"RND1 lissage", Mod, F, 0, 1, 0, 1.0f, "", true) \
    X(rnd2Rate,  "RND2 vitesse", Mod, F, 0.01f, 20, 4, 0.3f, "", true) \
    X(rnd2Smooth,"RND2 lissage", Mod, F, 0, 1, 0.6f, 1.0f, "", true)

namespace P
{
    enum Index : int
    {
       #define OURO_ENUM(id, name, mod, kind, mn, mx, def, skew, choices, rnd) id,
        OUROBOROS_PARAMS (OURO_ENUM)
       #undef OURO_ENUM
        count
    };

    enum class Module { Stomach, Seq, Ouro, Fx, Mod, Out, Rand };
    enum class Kind { F, I, B, C };

    struct Info
    {
        const char* id;
        const char* name;
        Module module;
        Kind kind;
        float min, max, def, skew;
        const char* choices;
        bool randomizable;

        juce::NormalisableRange<float> range() const
        {
            if (kind == Kind::F)
                return { min, max, 0.0f, skew };
            return { min, max, 1.0f };
        }
        float toNorm (float plain) const   { return range().convertTo0to1 (juce::jlimit (min, max, plain)); }
        float fromNorm (float norm) const  { return range().convertFrom0to1 (juce::jlimit (0.0f, 1.0f, norm)); }
        bool isContinuous() const          { return kind == Kind::F; }
    };

    const Info& info (int index);
    int indexOf (const juce::String& id);
    const char* moduleName (Module);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Valeurs "plain" de tous les paramètres (tableau fixe, copiable, utilisable hors temps réel). */
    using Values = std::array<float, (size_t) count>;
    Values defaults();
}
