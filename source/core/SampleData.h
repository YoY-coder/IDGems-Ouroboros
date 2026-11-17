#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>

/**
    Budget mémoire global de l'audio du plugin (par instance).
    Chaque SampleData s'y enregistre à sa création et s'en retire à sa destruction :
    on sait donc à tout moment combien d'octets d'audio sont vivants.
*/
class MemoryBudget
{
public:
    std::atomic<int64_t> used { 0 };
    std::atomic<int64_t> limit { 96ll * 1024 * 1024 };

    bool canAllocate (int64_t bytes) const noexcept { return used.load() + bytes <= limit.load(); }
    static int64_t limitForChoice (int choice) noexcept
    {
        static const int64_t mb[] = { 48, 96, 192 };
        return mb[juce::jlimit (0, 2, choice)] * 1024 * 1024;
    }
};

/**
    Bloc audio stéréo immuable, stocké en 16 bits entrelacés (moitié de la mémoire du float).
    Créé uniquement par les threads de fond / UI, jamais par le thread audio.
*/
class SampleData
{
public:
    /** Retourne nullptr si le budget ne le permet pas. */
    static std::shared_ptr<SampleData> create (MemoryBudget& budget, int numFrames, double sampleRate,
                                               const juce::String& name);

    /** Crée à partir d'un buffer float (1 ou 2 canaux). */
    static std::shared_ptr<SampleData> fromBuffer (MemoryBudget& budget, const juce::AudioBuffer<float>& buf,
                                                   int start, int numFrames, double sampleRate,
                                                   const juce::String& name);

    ~SampleData();

    int getNumFrames() const noexcept     { return numFrames; }
    double getSampleRate() const noexcept { return sampleRate; }
    const juce::String& getName() const noexcept { return name; }
    int64_t getBytes() const noexcept     { return (int64_t) data.size() * (int64_t) sizeof (int16_t); }

    inline float getL (int frame) const noexcept { return data[(size_t) frame * 2] * (1.0f / 32768.0f); }
    inline float getR (int frame) const noexcept { return data[(size_t) frame * 2 + 1] * (1.0f / 32768.0f); }

    /** Écriture : seulement avant publication (objet encore privé à son créateur). */
    inline void set (int frame, float l, float r) noexcept
    {
        data[(size_t) frame * 2]     = toInt (l);
        data[(size_t) frame * 2 + 1] = toInt (r);
    }

    /** Copie une zone vers un buffer float stéréo. */
    void copyTo (juce::AudioBuffer<float>& dest, int srcStart, int num) const;

    /** Mono float (moyenne G+D) pour l'analyse. */
    std::vector<float> toMono (int start, int num) const;

    static inline int16_t toInt (float x) noexcept
    {
        return (int16_t) juce::jlimit (-32767, 32767, (int) std::lrint (x * 32767.0f));
    }

private:
    SampleData (MemoryBudget&, int frames, double sr, const juce::String& n);

    MemoryBudget& budget;
    std::vector<int16_t> data;
    int numFrames;
    double sampleRate;
    juce::String name;
};

using SampleDataPtr = std::shared_ptr<const SampleData>;

/**
    Buffer circulaire de capture (entrée ou sidechain), taille fixe.
    Écrit par le thread audio ; lu par un thread de fond lors d'un « Avaler ».
*/
class CaptureRing
{
public:
    CaptureRing (MemoryBudget& b, int frames, double sr);
    ~CaptureRing();

    /** Thread audio. */
    void write (const float* l, const float* r, int num) noexcept;

    /** Thread de fond : copie les `numFrames` dernières trames (ou moins si pas encore rempli). */
    std::shared_ptr<SampleData> snapshot (MemoryBudget& budget, const juce::String& name) const;

    double getSampleRate() const noexcept { return sampleRate; }
    int getCapacity() const noexcept { return capacity; }
    int64_t getBytes() const noexcept { return (int64_t) data.size() * 2; }
    float filledSeconds() const noexcept
    {
        return (float) std::min<int64_t> (written.load(), capacity) / (float) sampleRate;
    }

private:
    MemoryBudget& budget;
    std::vector<int16_t> data;
    int capacity;
    double sampleRate;
    std::atomic<int> writePos { 0 };
    std::atomic<int64_t> written { 0 };
};
