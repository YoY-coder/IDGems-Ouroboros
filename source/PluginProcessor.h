#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace ParamIDs
{
    inline constexpr auto outGain = "outGain";
}

/** Niveaux et activité publiés par le thread audio, lus par l'UI (atomiques, sans verrou). */
struct LiveTelemetry
{
    std::atomic<float> inLevel   { 0.0f };   // crête entrée principale (linéaire)
    std::atomic<float> scLevel   { 0.0f };   // crête sidechain
    std::atomic<float> outLevelL { 0.0f };
    std::atomic<float> outLevelR { 0.0f };
    std::atomic<int>   lastNote  { -1 };     // dernière note MIDI reçue
    std::atomic<float> lastVelocity { 0.0f };
    std::atomic<uint32_t> noteCounter { 0 }; // incrémenté à chaque note-on
    std::atomic<bool>  sidechainActive { false };
    std::atomic<double> bpm { 120.0 };
};

class OuroborosProcessor final : public juce::AudioProcessor
{
public:
    OuroborosProcessor();
    ~OuroborosProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    LiveTelemetry telemetry;

    /** Largeur de l'éditeur mémorisée dans le projet (en pixels). */
    std::atomic<int> editorWidth { 0 };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    juce::SmoothedValue<float> outGainSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OuroborosProcessor)
};
