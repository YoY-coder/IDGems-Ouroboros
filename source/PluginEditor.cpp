#include "PluginEditor.h"
#include "ui/Palette.h"

OuroborosEditor::OuroborosEditor (OuroborosProcessor& p)
    : AudioProcessorEditor (p), proc (p), view (p)
{
    addAndMakeVisible (view);
    view.onSizePreset = [this] (int percent) { applySizePreset (percent); };
    tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
    view.onHelpToggle = [this] (bool on)
    {
        if (on) tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
        else tooltips.reset();
    };

    constexpr double ratio = (double) MainView::kWidth / (double) MainView::kHeight;

    setResizable (true, true); // coin de redimensionnement
    setResizeLimits (MainView::kWidth / 2, MainView::kHeight / 2,
                     MainView::kWidth * 2, MainView::kHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (ratio);

    const int savedWidth = proc.editorWidth.load();
    const int w = savedWidth > 0 ? juce::jlimit (MainView::kWidth / 2, MainView::kWidth * 2, savedWidth)
                                 : MainView::kWidth;
    setSize (w, juce::roundToInt (w / ratio));
}

void OuroborosEditor::applySizePreset (int percent)
{
    setSize (MainView::kWidth * percent / 100, MainView::kHeight * percent / 100);
}

void OuroborosEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
}

void OuroborosEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainView::kWidth;
    view.setBounds (0, 0, MainView::kWidth, MainView::kHeight);
    view.setTransform (juce::AffineTransform::scale (scale));
    proc.editorWidth.store (getWidth());
}
