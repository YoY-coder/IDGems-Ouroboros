#pragma once

#include "ui/Panels.h"

/**
    Vue principale dessinée dans un espace logique fixe (kWidth x kHeight),
    mise à l'échelle par l'éditeur : tout reste net à toutes les tailles.
*/
class MainView final : public juce::Component,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
public:
    static constexpr int kWidth  = 1200;
    static constexpr int kHeight = 750;

    explicit MainView (OuroborosProcessor&);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }

    std::function<void (int percent)> onSizePreset;
    std::function<void (bool)> onHelpToggle;

private:
    void timerCallback() override;

    OuroborosProcessor& proc;
    UiContext ctx;
    IDGemsLookAndFeel lnf;

    TopBar top;
    StomachPanel stomach;
    MapPanel map;
    OuroPanel ouro;
    SequencerPanel seq;
    FxPanel fx;
    ModPanel mod;
    OutputPanel out;

    juce::Image texture;
    float glitch = 0;
    juce::Random rng;
    bool dropHover = false;
};
