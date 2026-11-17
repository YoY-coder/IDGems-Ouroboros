#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>

#include "core/Params.h"
#include "core/LockFree.h"
#include "core/SampleData.h"
#include "engine/EngineCore.h"
#include "output/OutputChain.h"

class Controller;

/** Niveaux et activité publiés par le thread audio, lus par l'UI (atomiques, sans verrou). */
struct LiveTelemetry
{
    std::atomic<float> inLevel   { 0.0f };
    std::atomic<float> scLevel   { 0.0f };
    std::atomic<float> outLevelL { 0.0f };
    std::atomic<float> outLevelR { 0.0f };
    std::atomic<float> wetLevel  { 0.0f };
    std::atomic<int>   lastNote  { -1 };
    std::atomic<float> lastVelocity { 0.0f };
    std::atomic<uint32_t> noteCounter { 0 };
    std::atomic<bool>  sidechainActive { false };
    std::atomic<bool>  playing { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<double> ppq { 0.0 };
    std::atomic<int>   beatsPerBar { 4 };
    std::atomic<double> sampleRate { 44100.0 };
};

/** Commandes UI → audio. */
struct AudioCommand
{
    enum Type { None, Audition, Panic, ResetSequencer } type = None;
    int value = 0;
};

class OuroborosProcessor final : public juce::AudioProcessor
{
public:
    OuroborosProcessor();
    ~OuroborosProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Ordre de déclaration important : le budget doit survivre à tout l'audio ---
    MemoryBudget budget;
    Trash trash;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    LiveTelemetry telemetry;

    Mailbox<LibrarySnapshot> libraryBox;
    Mailbox<Pattern> patternBox;
    Mailbox<ModSnapshot> modBox;
    SharedPtrQueue<CaptureRing, 8> ringQueue;
    SpscQueue<AudioCommand, 256> commands;

    /** Buffer de capture courant, partagé avec les threads de fond (accès protégé par ringLock). */
    std::shared_ptr<CaptureRing> getRing() const;
    void setRing (std::shared_ptr<CaptureRing>);

    EngineCore engine;

    // MIDI learn
    std::atomic<int> midiLearnTarget { -1 };
    std::array<std::atomic<int>, 128> ccMap;

    // Valeurs modulées (affichage des anneaux de modulation sur les boutons)
    std::array<std::atomic<float>, P::count> modulatedDisplay;

    std::atomic<int> editorWidth { 0 };

    juce::RangedAudioParameter* param (int index) const { return params[(size_t) index]; }

    std::unique_ptr<Controller> controller;

private:
    std::array<juce::RangedAudioParameter*, P::count> params {};
    std::array<std::atomic<float>*, P::count> raw {};
    P::Values base {};

    std::shared_ptr<CaptureRing> ringAudio;   // côté audio
    std::shared_ptr<CaptureRing> ringShared;  // côté UI / fond
    juce::SpinLock ringLock;

    OutputChain output;
    juce::AudioBuffer<float> wet, dry;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OuroborosProcessor)
};
