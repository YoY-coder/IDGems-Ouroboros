#pragma once

#include "ui/Widgets.h"

/** Base commune : liste des potards à rafraîchir (anneaux de modulation). */
class AnimatedPanel : public ModulePanel
{
public:
    using ModulePanel::ModulePanel;
    virtual void tick();

protected:
    Knob& addKnob (int param, const juce::String& label = {});
    ParamToggle& addToggle (int param, const juce::String& text);
    ParamCombo& addCombo (int param);
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<ParamToggle> toggles;
    juce::OwnedArray<ParamCombo> combos;
};

//==============================================================================
class StomachPanel : public AnimatedPanel, private juce::ChangeListener
{
public:
    explicit StomachPanel (UiContext&);
    ~StomachPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateDetail();
    void openChooser (const juce::File& start);
    juce::Rectangle<int> slotRow (int i) const;

    juce::TextButton swallowBtn { "AVALER" }, loadBtn { "Charger..." }, idgemsBtn { "IDGems" }, folderBtn { "..." }, clearBtn { "Vider" };
    juce::ComboBox modeBox;
    juce::Slider p1, p2, gain, weight;
    juce::Label p1Label, p2Label, gainLabel, weightLabel;
    juce::ComboBox gridBox;
    std::unique_ptr<juce::FileChooser> chooser;
    float inLevel = 0, scLevel = 0;
    bool updating = false;
};

//==============================================================================
class MapView : public juce::Component, public juce::SettableTooltipClient, public juce::DragAndDropContainer
{
public:
    explicit MapView (UiContext&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void tick();

    enum Tool { Listen = 0, Brush, Eraser, Like };
    int tool = Listen;
    float brushSize = 0.12f;

private:
    int hitTest (juce::Point<float>) const;
    juce::Point<float> posOf (const Fragment&) const;
    void paintAt (juce::Point<float>);

    UiContext& ctx;
    std::shared_ptr<const LibrarySnapshot> lib;
    int hover = -1;
    juce::Point<float> downPos;
    bool dragStarted = false;
    juce::Image maskImage;
    juce::uint32 lastPublish = 0;
    std::array<float, 32> flash {};
    std::array<int, 32> flashFrag {};
    uint32_t lastTrigger = 0;
};

class MapPanel : public AnimatedPanel, private juce::ChangeListener
{
public:
    explicit MapPanel (UiContext&);
    ~MapPanel() override;
    void resized() override;
    void tick() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    MapView map;
    juce::ComboBox axisX, axisY;
    juce::TextButton listenBtn { "ECOUTE" }, brushBtn { "PINCEAU" }, eraserBtn { "GOMME" }, likeBtn { "LIKE" }, clearBtn { "X zone" };
    juce::Slider brush;
};

//==============================================================================
class OuroPanel : public AnimatedPanel, private juce::ChangeListener
{
public:
    explicit OuroPanel (UiContext&);
    ~OuroPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void drawRing (juce::Graphics&, juce::Rectangle<float>);
    void drawTree (juce::Graphics&, juce::Rectangle<int>);
    juce::TextButton genBtn { "NOUVELLE GENERATION" };
    juce::Rectangle<int> ringArea, treeArea;
    std::vector<std::pair<int, juce::Rectangle<float>>> nodeHits;
    float angle = 0, energy = 0, crackFlash = 0;
    int lastCounter = 0;
    juce::Random crackRng;
};

//==============================================================================
class StepGrid : public juce::Component
{
public:
    explicit StepGrid (UiContext&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    int lane = 1; // 0 on/off, 1 proba, 2 ratchet, 3 reverse, 4 pitch, 5 longueur
    int playing = -1;

private:
    void edit (const juce::MouseEvent&, bool first);
    UiContext& ctx;
    bool paintOn = true;
};

class SequencerPanel : public AnimatedPanel
{
public:
    explicit SequencerPanel (UiContext&);
    void resized() override;
    void tick() override;

private:
    StepGrid grid;
    juce::OwnedArray<juce::TextButton> laneBtns;
    juce::TextButton crossBtn { "CROISER" }, clearSteps { "RAZ" };
};

//==============================================================================
class FxPanel : public AnimatedPanel
{
public:
    explicit FxPanel (UiContext&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> tileBounds (int position) const;
    struct Tile { juce::Component* c[4] = {}; ParamToggle* on = nullptr; };
    std::array<Tile, 7> tiles;
    int dragFrom = -1, dragOver = -1;
};

//==============================================================================
class LfoEditor : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LfoEditor (UiContext&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    int lfo = 0;

private:
    void draw (const juce::MouseEvent&);
    UiContext& ctx;
    int lastPoint = -1;
};

class ModPanel : public AnimatedPanel
{
public:
    explicit ModPanel (UiContext&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    void showPage (int page);
    int page = 0;
    juce::OwnedArray<juce::TextButton> tabs;
    LfoEditor editor;
    juce::OwnedArray<juce::Component> pageComps[7];
    juce::TextButton shapeMenu { "Formes" };
};

//==============================================================================
class OutputPanel : public AnimatedPanel
{
public:
    explicit OutputPanel (UiContext&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void tick() override;

private:
    juce::TextButton panicBtn { "PANIC" }, exportBtn { "EXPORT" };
    juce::Rectangle<int> dragZone, meterArea;
    float outL = 0, outR = 0;
    bool dragging = false;
};

//==============================================================================
class TopBar : public juce::Component, private juce::ChangeListener
{
public:
    explicit TopBar (UiContext&);
    ~TopBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
    std::function<void (int)> onSizePreset;
    std::function<void (bool)> onHelpToggle;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showPresetBrowser();
    void showHistory();
    void stepPreset (int delta);

    UiContext& ctx;
    juce::TextButton presetBtn, prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAVE" },
                     abBtn { "A" }, copyBtn { "A>B" }, undoBtn { "UNDO" }, redoBtn { "REDO" },
                     randomBtn { "RANDOM" }, historyBtn { "HIST." }, seedBtn, helpBtn { "?" }, sizeBtn { "TAILLE" };
    std::unique_ptr<Knob> amount, chaos;
    float midiFlash = 0;
    uint32_t lastNoteCounter = 0;
    int lastNote = -1;
};
