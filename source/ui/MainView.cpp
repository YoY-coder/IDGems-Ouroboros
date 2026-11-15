#include "MainView.h"
#include "Palette.h"

MainView::MainView (OuroborosProcessor& p) : proc (p)
{
    outGain.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    outGain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 18);
    outGain.setColour (juce::Slider::rotarySliderFillColourId, Palette::orange);
    outGain.setColour (juce::Slider::rotarySliderOutlineColourId, Palette::panelEdge);
    outGain.setColour (juce::Slider::thumbColourId, Palette::text);
    outGain.setColour (juce::Slider::textBoxTextColourId, Palette::text);
    outGain.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    outGain.setTextValueSuffix (" dB");
    outGain.setTooltip ("Gain de sortie");
    addAndMakeVisible (outGain);
    outGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, ParamIDs::outGain, outGain);

    sizeButton.setColour (juce::TextButton::buttonColourId, Palette::panel);
    sizeButton.setColour (juce::TextButton::textColourOffId, Palette::text);
    sizeButton.onClick = [this]
    {
        juce::PopupMenu m;
        for (int pct : { 50, 75, 100, 125, 150, 200 })
            m.addItem (pct, juce::String (pct) + " %");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sizeButton),
                         [this] (int result)
                         {
                             if (result > 0 && onSizePreset)
                                 onSizePreset (result);
                         });
    };
    addAndMakeVisible (sizeButton);

    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
}

void MainView::resized()
{
    // Disposition fixe en coordonnées logiques (voir docs/ARCHITECTURE.md, maquette).
    topBar      = { 0, 0, kWidth, 44 };
    stomachArea = { 8, 52, 240, 378 };
    mapArea     = { 256, 52, 544, 378 };
    ouroArea    = { 808, 52, 384, 378 };
    seqArea     = { 8, 438, 1184, 100 };
    fxArea      = { 8, 546, 588, 120 };
    modArea     = { 604, 546, 588, 120 };
    outArea     = { 8, 674, 1184, 68 };

    sizeButton.setBounds (kWidth - 88, 10, 76, 24);
    outGain.setBounds (outArea.getRight() - 340, outArea.getY() + 6, 140, 56);
}

void MainView::timerCallback()
{
    auto& t = proc.telemetry;
    inLevel = t.inLevel.load();
    scLevel = t.scLevel.load();
    outL    = t.outLevelL.load();
    outR    = t.outLevelR.load();
    sidechainOn = t.sidechainActive.load();
    bpm = t.bpm.load();

    const auto counter = t.noteCounter.load();
    if (counter != lastNoteCounter)
    {
        lastNoteCounter = counter;
        lastNote = t.lastNote.load();
        midiFlash = 0.4f + 0.6f * t.lastVelocity.load();
    }
    midiFlash *= 0.88f;

    // L'anneau tourne doucement, plus vite et plus "chargé" quand le son est fort.
    const float target = juce::jlimit (0.0f, 1.0f, juce::jmax (inLevel, scLevel) + midiFlash * 0.5f);
    ringEnergy += (target - ringEnergy) * 0.25f;
    ringAngle  += 0.01f + ringEnergy * 0.08f;
    if (ringAngle > juce::MathConstants<float>::twoPi)
        ringAngle -= juce::MathConstants<float>::twoPi;

    repaint();
}

void MainView::drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour c) const
{
    auto rf = r.toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (rf, 4.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (rf.reduced (0.5f), 4.0f, 1.0f);

    // Liseré de couleur du module
    g.setColour (c);
    g.fillRect (rf.removeFromTop (2.0f).reduced (4.0f, 0.0f));

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (title, r.reduced (10, 6).removeFromTop (16), juce::Justification::topLeft);
}

void MainView::drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float level, juce::Colour c,
                          const juce::String& label) const
{
    auto labelArea = r.removeFromLeft (70.0f);
    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (label, labelArea, juce::Justification::centredLeft);

    g.setColour (Palette::background);
    g.fillRect (r);
    const float db = juce::Decibels::gainToDecibels (level, -60.0f);
    const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
    g.setColour (level >= 1.0f ? Palette::red : c);
    g.fillRect (r.withWidth (r.getWidth() * norm));
}

void MainView::drawRing (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto centre = area.getCentre();
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.36f;
    const float thickness = 10.0f + ringEnergy * 10.0f;
    const float gap = 0.35f; // espace entre la tête et la queue (radians)

    auto ringPath = [&] (juce::Point<float> offset)
    {
        juce::Path p;
        p.addCentredArc (centre.x + offset.x, centre.y + offset.y, radius, radius, 0.0f,
                         ringAngle + gap, ringAngle + juce::MathConstants<float>::twoPi, true);
        return p;
    };
    const juce::PathStrokeType stroke (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // Aberration chromatique : copies rouge et bleue décalées selon l'énergie.
    const float shift = 1.5f + ringEnergy * 6.0f;
    g.setColour (Palette::red.withAlpha (0.45f));
    g.strokePath (ringPath ({ -shift, 0.0f }), stroke);
    g.setColour (Palette::blue.withAlpha (0.45f));
    g.strokePath (ringPath ({ shift, 0.0f }), stroke);

    g.setColour (Palette::orange.interpolatedWith (Palette::text, ringEnergy * 0.4f));
    g.strokePath (ringPath ({}), stroke);

    // Tête du serpent (à l'extrémité de l'arc), qui "mord" la queue.
    const float headAngle = ringAngle + juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
    const juce::Point<float> head (centre.x + radius * std::cos (headAngle),
                                   centre.y + radius * std::sin (headAngle));
    const float hs = thickness * 1.25f;
    g.setColour (Palette::text);
    g.fillEllipse (juce::Rectangle<float> (hs, hs).withCentre (head));
    g.setColour (Palette::background);
    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (head));

    // Génération courante (placeholder phase 1)
    g.setColour (Palette::text);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("GEN", juce::Rectangle<float> (120.0f, 16.0f).withCentre (centre.translated (0, -16)),
                juce::Justification::centred);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText ("00", juce::Rectangle<float> (120.0f, 34.0f).withCentre (centre.translated (0, 8)),
                juce::Justification::centred);
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    // Texture : fines lignes de balayage façon écran
    g.setColour (juce::Colours::white.withAlpha (0.015f));
    for (int y = 0; y < kHeight; y += 3)
        g.fillRect (0, y, kWidth, 1);

    // --- Barre haute --------------------------------------------------------------------
    {
        auto r = topBar.reduced (14, 0);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        auto titleArea = r.removeFromLeft (300);
        g.setColour (Palette::red.withAlpha (0.6f));
        g.drawText ("IDGems OUROBOROS", titleArea.translated (-1, 0), juce::Justification::centredLeft);
        g.setColour (Palette::blue.withAlpha (0.6f));
        g.drawText ("IDGems OUROBOROS", titleArea.translated (1, 0), juce::Justification::centredLeft);
        g.setColour (Palette::text);
        g.drawText ("IDGems OUROBOROS", titleArea, juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (12.0f));
        g.setColour (Palette::textDim);
        g.drawText ("v" JucePlugin_VersionString "  -  phase 1 : squelette", r.removeFromLeft (260),
                    juce::Justification::centredLeft);

        // Tempo hôte + témoin MIDI
        auto right = topBar.withTrimmedRight (100).removeFromRight (300).reduced (0, 12);
        auto led = right.removeFromLeft (20).toFloat().withSizeKeepingCentre (12.0f, 12.0f);
        g.setColour (Palette::panelEdge);
        g.fillEllipse (led);
        g.setColour (Palette::blue.withAlpha (juce::jlimit (0.0f, 1.0f, midiFlash)));
        g.fillEllipse (led);
        g.setColour (Palette::text);
        const juce::String noteText = lastNote >= 0
            ? juce::MidiMessage::getMidiNoteName (lastNote, true, true, 3)
            : juce::String ("--");
        g.drawText ("MIDI " + noteText, right.removeFromLeft (110), juce::Justification::centredLeft);
        g.drawText (juce::String (bpm, 1) + " BPM", right, juce::Justification::centredLeft);
    }

    // --- Panneaux (vides pour l'instant) ----------------------------------------------
    drawPanel (g, stomachArea, "ESTOMAC", Palette::stomach);
    drawPanel (g, mapArea,     "CARTE DES FRAGMENTS", Palette::map);
    drawPanel (g, ouroArea,    "OUROBOROS", Palette::ouroboros);
    drawPanel (g, seqArea,     "SEQUENCEUR", Palette::sequencer);
    drawPanel (g, fxArea,      "FX RACK", Palette::fx);
    drawPanel (g, modArea,     "MODULATION", Palette::modulation);
    drawPanel (g, outArea,     "SORTIE", Palette::output);

    auto comingSoon = [&] (juce::Rectangle<int> r, const juce::String& txt)
    {
        g.setColour (Palette::textDim.withAlpha (0.6f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (txt, r, juce::Justification::centred, 3);
    };

    // Estomac : mètres entrée + sidechain
    {
        auto r = stomachArea.reduced (12).withTrimmedTop (28).toFloat();
        drawMeter (g, r.removeFromTop (12.0f), inLevel, Palette::stomach, "Entree");
        r.removeFromTop (8.0f);
        drawMeter (g, r.removeFromTop (12.0f), scLevel, Palette::blue,
                   sidechainOn ? "Sidechain" : "Sidechain off");
        comingSoon (stomachArea.withTrimmedTop (80), "Capture + glisser-deposer\n(phase 2)");
    }

    // Carte : grille discrète
    {
        auto r = mapArea.reduced (12).withTrimmedTop (24);
        g.setColour (Palette::panelEdge.withAlpha (0.5f));
        for (int i = 1; i < 8; ++i)
        {
            const int x = r.getX() + r.getWidth() * i / 8;
            const int y = r.getY() + r.getHeight() * i / 8;
            g.drawVerticalLine (x, (float) r.getY(), (float) r.getBottom());
            g.drawHorizontalLine (y, (float) r.getX(), (float) r.getRight());
        }
        comingSoon (r, "Carte 2D (phase 3)");
    }

    drawRing (g, ouroArea.withTrimmedTop (24).toFloat());

    comingSoon (seqArea,  "Sequenceur + regles (phase 4)");
    comingSoon (fxArea,   "Rack FX (phase 6)");
    comingSoon (modArea,  "LFO, random, macros (phase 7)");

    // Sortie : mètres
    {
        auto r = outArea.reduced (12).withTrimmedTop (18).withWidth (500).toFloat();
        drawMeter (g, r.removeFromTop (10.0f), outL, Palette::output, "Sortie G");
        r.removeFromTop (4.0f);
        drawMeter (g, r.removeFromTop (10.0f), outR, Palette::output, "Sortie D");
    }
}
