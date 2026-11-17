#pragma once

#include "SampleData.h"
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <vector>

/** Descripteurs d'un fragment (bruts). */
struct Descriptors
{
    float centroidHz = 0;   // brillance
    float flatness   = 0;   // bruit (0 = tonal, 1 = bruit blanc)
    float pitchMidi  = -1;  // hauteur détectée, -1 = atonal
    float rmsDb      = -100;
    float peakDb     = -100;
    float lengthSec  = 0;
};

/** Axes possibles de la carte / dimensions de ressemblance. */
enum class Axis : int { Brightness = 0, Noise, Pitch, Energy, Length, NumAxes };
constexpr int kNumAxes = (int) Axis::NumAxes;

inline const char* axisName (int a)
{
    static const char* names[] = { "Brillance", "Bruit", "Hauteur", "Energie", "Longueur" };
    return names[juce::jlimit (0, kNumAxes - 1, a)];
}

/** Valeurs normalisées 0..1 pour la carte et les distances. */
inline std::array<float, kNumAxes> normalise (const Descriptors& d)
{
    auto lg = [] (float v, float lo, float hi)
    {
        v = juce::jlimit (lo, hi, v);
        return std::log (v / lo) / std::log (hi / lo);
    };
    std::array<float, kNumAxes> n {};
    n[0] = lg (juce::jmax (50.0f, d.centroidHz), 50.0f, 12000.0f);
    n[1] = juce::jlimit (0.0f, 1.0f, std::pow (d.flatness, 0.5f));
    n[2] = d.pitchMidi < 0 ? 0.0f : juce::jlimit (0.02f, 1.0f, (d.pitchMidi - 24.0f) / 72.0f);
    n[3] = juce::jlimit (0.0f, 1.0f, (d.rmsDb + 60.0f) / 60.0f);
    n[4] = lg (juce::jmax (0.01f, d.lengthSec), 0.01f, 4.0f);
    return n;
}

constexpr int kMaxFragments = 512;
constexpr int kNumSlots = 8;
constexpr int kNeighbours = 8;

/** Un fragment = une zone d'un buffer (pas de copie de l'audio). */
struct Fragment
{
    int buffer = 0;          // index dans LibrarySnapshot::buffers
    int start = 0;           // trame de départ
    int length = 0;          // nombre de trames
    Descriptors desc;
    std::array<float, kNumAxes> norm {};
    int slot = -1;           // source d'origine (0..7), -1 = génération / croisement
    int generation = 0;
    uint32_t id = 0;         // identifiant stable
    bool liked = false;
    float gain = 1.0f;
    juce::Colour colour { 0xffff6a1a };
};

/** Bibliothèque immuable publiée vers le thread audio. */
struct LibrarySnapshot
{
    uint32_t version = 0;
    std::vector<SampleDataPtr> buffers;
    std::vector<Fragment> frags;
    std::vector<float> weights;                        // probabilité de pioche (zone peinte, poids, ADN)
    std::vector<float> cumulative;                     // somme cumulée pour tirage pondéré
    std::vector<std::array<int16_t, kNeighbours>> near; // voisins les plus proches
    std::vector<std::array<int16_t, kNeighbours>> far;  // fragments les plus éloignés
    int axisX = 0, axisY = 1;                          // axes de la carte (mode MIDI clavier = carte)

    int size() const noexcept { return (int) frags.size(); }
};

/** Un pas du séquenceur. */
struct StepData
{
    bool on = true;
    float prob = 1.0f;     // 0..1
    int ratchet = 1;       // 1..4
    float reverse = 0.0f;  // probabilité de lecture à l'envers
    float pitch = 0.0f;    // demi-tons
    float length = 1.0f;   // part du fragment jouée (0.05..1)
};

constexpr int kMaxSteps = 64;

struct Pattern
{
    std::array<StepData, kMaxSteps> steps {};
};

/** Modulations : formes de LFO dessinées + routages. */
constexpr int kLfoPoints = 32;
constexpr int kNumLfos = 4;
constexpr int kNumRnd = 2;
constexpr int kNumMacros = 8;
constexpr int kMaxRoutes = 64;

enum ModSource : int
{
    srcLfo1 = 0, srcLfo2, srcLfo3, srcLfo4, srcRnd1, srcRnd2,
    srcMacro1, srcMacro2, srcMacro3, srcMacro4, srcMacro5, srcMacro6, srcMacro7, srcMacro8,
    numModSources
};

inline juce::String modSourceName (int s)
{
    if (s < 4)  return "LFO " + juce::String (s + 1);
    if (s < 6)  return "RND " + juce::String (s - 3);
    return "Macro " + juce::String (s - 5);
}

struct ModRoute
{
    int source = -1;     // ModSource, -1 = vide
    int param = -1;      // P::Index
    float depth = 0.0f;  // -1..1 (en valeur normalisée)
};

struct ModSnapshot
{
    std::array<std::array<float, kLfoPoints>, kNumLfos> shapes {};
    std::array<ModRoute, kMaxRoutes> routes {};
    std::array<int, 7> fxOrder { 0, 1, 2, 3, 4, 5, 6 };
};
