#include "MainView.h"

MainView::MainView (OuroborosProcessor& p)
    : proc (p), ctx { p, *p.controller },
      top (ctx), stomach (ctx), map (ctx), ouro (ctx), seq (ctx), fx (ctx), mod (ctx), out (ctx)
{
    setLookAndFeel (&lnf);
    for (juce::Component* c : { (juce::Component*) &top, (juce::Component*) &stomach, (juce::Component*) &map,
                                (juce::Component*) &ouro, (juce::Component*) &seq, (juce::Component*) &fx,
                                (juce::Component*) &mod, (juce::Component*) &out })
        addAndMakeVisible (c);

    top.onSizePreset = [this] (int pct) { if (onSizePreset) onSizePreset (pct); };
    top.onHelpToggle = [this] (bool on) { if (onHelpToggle) onHelpToggle (on); };

    // Texture "noir texturé" pré-calculée (bruit + rayures), dessinée une fois
    texture = juce::Image (juce::Image::ARGB, kWidth, kHeight, true);
    {
        juce::Graphics g (texture);
        juce::Random r (2024);
        for (int i = 0; i < 9000; ++i)
        {
            g.setColour (juce::Colours::white.withAlpha (r.nextFloat() * 0.035f));
            g.fillRect (r.nextInt (kWidth), r.nextInt (kHeight), 1 + r.nextInt (2), 1);
        }
        for (int y = 0; y < kHeight; y += 3)
        {
            g.setColour (juce::Colours::white.withAlpha (0.012f));
            g.fillRect (0, y, kWidth, 1);
        }
        // fractures diagonales façon Altered Brother
        for (int i = 0; i < 6; ++i)
        {
            const float x = r.nextFloat() * kWidth;
            g.setColour (Palette::orange.withAlpha (0.05f));
            g.drawLine (x, 0, x + 300 - r.nextFloat() * 600, (float) kHeight, 1.0f);
        }
    }

    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MainView::resized()
{
    top.setBounds (0, 0, kWidth, 44);
    stomach.setBounds (8, 52, 240, 378);
    map.setBounds (256, 52, 544, 378);
    ouro.setBounds (808, 52, 384, 378);
    seq.setBounds (8, 438, 1184, 100);
    fx.setBounds (8, 546, 588, 120);
    mod.setBounds (604, 546, 588, 120);
    out.setBounds (8, 674, 1184, 68);
}

void MainView::timerCallback()
{
    top.tick();
    stomach.tick();
    map.tick();
    ouro.tick();
    seq.tick();
    fx.tick();
    mod.tick();
    out.tick();

    // L'interface réagit au son : bandes de glitch quand ça tape fort
    const float lvl = proc.telemetry.wetLevel.load();
    glitch = juce::jmax (glitch * 0.8f, lvl > 0.5f ? lvl : 0.0f);
    if (glitch > 0.05f)
        repaint();
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
    g.drawImageAt (texture, 0, 0);
}

void MainView::paintOverChildren (juce::Graphics& g)
{
    if (glitch > 0.05f)
    {
        const int bands = 1 + (int) (glitch * 4);
        for (int i = 0; i < bands; ++i)
        {
            const int y = rng.nextInt (kHeight);
            const int h = 1 + rng.nextInt (3);
            g.setColour ((rng.nextBool() ? Palette::red : Palette::blue).withAlpha (0.08f + glitch * 0.12f));
            g.fillRect (rng.nextInt (200), y, kWidth - rng.nextInt (400), h);
        }
    }
    if (dropHover)
    {
        g.setColour (Palette::orange.withAlpha (0.15f));
        g.fillAll();
        g.setColour (Palette::orange);
        g.drawRect (getLocalBounds(), 3);
        g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
        g.drawText ("LACHE : L'OUROBOROS AVALE", getLocalBounds(), juce::Justification::centred);
    }
}

bool MainView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3"))
            return true;
    return false;
}

void MainView::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    juce::StringArray audio;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3"))
            audio.add (f);
    ctx.ctl.loadFiles (audio);
    repaint();
}

bool MainView::keyPressed (const juce::KeyPress& k)
{
    auto& c = ctx.ctl;
    const auto mods = k.getModifiers();
    if (mods.isCommandDown() && k.getKeyCode() == 'Z') { mods.isShiftDown() ? c.redo() : c.undo(); return true; }
    if (mods.isCommandDown() && k.getKeyCode() == 'Y') { c.redo(); return true; }
    if (mods.isAnyModifierKeyDown())
        return false;

    switch (juce::CharacterFunctions::toUpperCase (k.getTextCharacter()))
    {
        case 'R': c.randomizeAll (c.nextSeed()); return true;
        case 'G': c.newGeneration(); return true;
        case 'S': c.swallow(); return true;
        case 'P': proc.commands.push ({ AudioCommand::Panic, 0 }); return true;
        case 'X': c.crossSelected(); return true;
        case 'B': c.toggleAB(); return true;
        default: break;
    }
    return false;
}
