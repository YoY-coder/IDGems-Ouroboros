#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

/**
    Vue principale dessinée dans un espace logique fixe (kWidth x kHeight).
    L'éditeur la met à l'échelle avec une transformation : tout reste net à toutes les tailles.

    Phase 1 : panneaux vides selon la maquette, mètres, témoin MIDI, anneau animé qui réagit au son.
*/
class MainView final : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth  = 1200;
    static constexpr int kHeight = 750;

    explicit MainView (OuroborosProcessor&);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Appelé quand l'utilisateur choisit une taille prédéfinie (en %). */
    std::function<void (int percent)> onSizePreset;

private:
    void timerCallback() override;

    void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, juce::Colour) const;
    void drawRing (juce::Graphics&, juce::Rectangle<float>) const;
    void drawMeter (juce::Graphics&, juce::Rectangle<float>, float level, juce::Colour, const juce::String& label) const;

    OuroborosProcessor& proc;

    juce::Slider outGain;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outGainAttachment;
    juce::TextButton sizeButton { "Taille" };

    // Zones (coordonnées logiques)
    juce::Rectangle<int> topBar, stomachArea, mapArea, ouroArea, seqArea, fxArea, modArea, outArea;

    // État animé (thread UI uniquement)
    float inLevel = 0, scLevel = 0, outL = 0, outR = 0;
    float ringAngle = 0, ringEnergy = 0, midiFlash = 0;
    uint32_t lastNoteCounter = 0;
    int lastNote = -1;
    bool sidechainOn = false;
    double bpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
