#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "control/Controller.h"
#include "ui/Palette.h"

/** Style IDGems : boutons, listes, menus, potards. */
class IDGemsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    IDGemsLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

/** Contexte partagé par tous les widgets. */
struct UiContext
{
    OuroborosProcessor& proc;
    Controller& ctl;
};

/**
    Potard attaché à un paramètre : nom, valeur, anneau de modulation, cadenas.
    Clic droit : cadenas, plage de random, modulation (LFO/RND/macros), MIDI learn, valeur par défaut.
*/
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (UiContext&, int paramIndex, juce::Colour accent, const juce::String& label = {});
    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();   // appelé par le timer de la vue (modulation)

    juce::Slider slider;
    int param;

private:
    void showMenu();
    UiContext& ctx;
    juce::Colour accent;
    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    float shownMod = -1.0f;
    bool hasMod = false;
};

/** Bouton on/off attaché à un paramètre booléen. */
class ParamToggle : public juce::TextButton
{
public:
    ParamToggle (UiContext&, int paramIndex, const juce::String& text, juce::Colour accent);
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** Liste attachée à un paramètre de choix. */
class ParamCombo : public juce::ComboBox
{
public:
    ParamCombo (UiContext&, int paramIndex);
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Panneau de module : fond, liseré de couleur, titre, boutons dé (random) et cadenas. */
class ModulePanel : public juce::Component
{
public:
    ModulePanel (UiContext&, const juce::String& title, juce::Colour colour, int module /* -1 = aucun */);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Rectangle<int> content() const { return getLocalBounds().reduced (6).withTrimmedTop (16); }

protected:
    UiContext& ctx;
    juce::String title;
    juce::Colour colour;
    int module;
    juce::TextButton dice { "DE" }, lock { "LOCK" };
};

void styleButton (juce::TextButton&, juce::Colour accent, bool toggle = false);
