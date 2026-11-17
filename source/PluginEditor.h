#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/MainView.h"

/**
    Éditeur redimensionnable à ratio fixe (16:10).
    La vue principale est dessinée en 1200x750 "logiques" puis mise à l'échelle.
*/
class OuroborosEditor final : public juce::AudioProcessorEditor
{
public:
    explicit OuroborosEditor (OuroborosProcessor&);
    ~OuroborosEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void applySizePreset (int percent);

    OuroborosProcessor& proc;
    MainView view;
    std::unique_ptr<juce::TooltipWindow> tooltips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OuroborosEditor)
};
