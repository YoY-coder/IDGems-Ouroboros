#include "Panels.h"

//==============================================================================
void AnimatedPanel::tick()
{
    for (auto* k : knobs)
        k->refresh();
}

Knob& AnimatedPanel::addKnob (int param, const juce::String& label)
{
    auto* k = knobs.add (new Knob (ctx, param, colour, label));
    addAndMakeVisible (k);
    return *k;
}

ParamToggle& AnimatedPanel::addToggle (int param, const juce::String& text)
{
    auto* t = toggles.add (new ParamToggle (ctx, param, text, colour));
    addAndMakeVisible (t);
    return *t;
}

ParamCombo& AnimatedPanel::addCombo (int param)
{
    auto* c = combos.add (new ParamCombo (ctx, param));
    addAndMakeVisible (c);
    return *c;
}

//==============================================================================
// ESTOMAC
//==============================================================================
static void setupMiniSlider (juce::Slider& s, juce::Label& l, const juce::String& text, juce::Colour c, juce::Component& parent)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.setColour (juce::Slider::rotarySliderFillColourId, c);
    s.setPopupDisplayEnabled (true, true, nullptr);
    parent.addAndMakeVisible (s);
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (9.5f));
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, Palette::textDim);
    parent.addAndMakeVisible (l);
}

StomachPanel::StomachPanel (UiContext& c) : AnimatedPanel (c, "ESTOMAC", Palette::stomach, -1)
{
    styleButton (swallowBtn, colour);
    swallowBtn.setColour (juce::TextButton::buttonColourId, colour.withAlpha (0.25f));
    swallowBtn.setTooltip ("Fige les dernieres secondes captees et en fait une source (raccourci : S)");
    swallowBtn.onClick = [this] { ctx.ctl.swallow (ctx.ctl.selectedSlot >= 0 && ! ctx.ctl.slots[(size_t) ctx.ctl.selectedSlot].used ? ctx.ctl.selectedSlot : -1); };
    addAndMakeVisible (swallowBtn);

    addKnob (P::captureSeconds, "Capture");
    addCombo (P::captureSource);
    addCombo (P::memoryBudget).setTooltip ("Memoire maximale de cette instance (jamais depassee)");

    for (auto* b : { &loadBtn, &idgemsBtn, &folderBtn, &clearBtn })
    {
        styleButton (*b, colour);
        addAndMakeVisible (b);
    }
    loadBtn.setTooltip ("Charger des fichiers audio (WAV, AIFF, FLAC, MP3). Tu peux aussi les glisser sur l'interface.");
    loadBtn.onClick = [this] { openChooser (juce::File::getSpecialLocation (juce::File::userMusicDirectory)); };
    idgemsBtn.setTooltip ("Ouvrir le dossier d'enregistrement d'IDGems / IDGems Synth");
    idgemsBtn.onClick = [this] { openChooser (ctx.ctl.getIdgemsFolder()); };
    folderBtn.setTooltip ("Choisir le dossier d'enregistrement d'IDGems");
    folderBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Dossier d'enregistrement IDGems", ctx.ctl.getIdgemsFolder());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().isDirectory())
                                  {
                                      ctx.ctl.setIdgemsFolder (fc.getResult());
                                      ctx.ctl.setStatus ("Dossier IDGems : " + fc.getResult().getFullPathName());
                                  }
                              });
    };
    clearBtn.setTooltip ("Vider la source selectionnee (libere sa memoire)");
    clearBtn.onClick = [this] { ctx.ctl.clearSlot (ctx.ctl.selectedSlot); };

    modeBox.addItemList ({ "Transitoires", "Grille (tempo)", "Bandes de freq.", "Aleatoire" }, 1);
    modeBox.setTooltip ("Mode de decoupe de la source selectionnee");
    modeBox.onChange = [this]
    {
        if (updating) return;
        auto s = ctx.ctl.slots[(size_t) ctx.ctl.selectedSlot].slice;
        s.mode = modeBox.getSelectedItemIndex();
        ctx.ctl.setSlotSlice (ctx.ctl.selectedSlot, s);
        updateDetail();
    };
    addAndMakeVisible (modeBox);

    gridBox.addItemList ({ "1/4", "1/8", "1/16", "1/32", "1/64" }, 1);
    gridBox.onChange = [this]
    {
        if (updating) return;
        auto s = ctx.ctl.slots[(size_t) ctx.ctl.selectedSlot].slice;
        s.gridDivision = gridBox.getSelectedItemIndex();
        ctx.ctl.setSlotSlice (ctx.ctl.selectedSlot, s);
    };
    addChildComponent (gridBox);

    setupMiniSlider (p1, p1Label, "Seuil", colour, *this);
    setupMiniSlider (p2, p2Label, "Sensib.", colour, *this);
    setupMiniSlider (gain, gainLabel, "Gain", colour, *this);
    setupMiniSlider (weight, weightLabel, "Poids", colour, *this);
    gain.setRange (0.0, 2.0);
    weight.setRange (0.0, 4.0);
    gain.setTooltip ("Gain de la source");
    weight.setTooltip ("Poids : probabilite que cette source soit piochee");

    auto applySlice = [this]
    {
        if (updating) return;
        auto s = ctx.ctl.slots[(size_t) ctx.ctl.selectedSlot].slice;
        if (s.mode == SliceSettings::Random)
        {
            s.randomMinMs = (float) p1.getValue();
            s.randomMaxMs = (float) p2.getValue();
        }
        else
        {
            s.threshold = (float) p1.getValue();
            s.sensitivity = (float) p2.getValue();
        }
        ctx.ctl.setSlotSlice (ctx.ctl.selectedSlot, s);
    };
    p1.onDragEnd = applySlice;
    p2.onDragEnd = applySlice;
    gain.onValueChange = [this] { if (! updating) ctx.ctl.setSlotGain (ctx.ctl.selectedSlot, (float) gain.getValue()); };
    weight.onValueChange = [this] { if (! updating) ctx.ctl.setSlotWeight (ctx.ctl.selectedSlot, (float) weight.getValue()); };

    ctx.ctl.addChangeListener (this);
    updateDetail();
}

StomachPanel::~StomachPanel()
{
    ctx.ctl.removeChangeListener (this);
}

void StomachPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateDetail();
    repaint();
}

void StomachPanel::openChooser (const juce::File& start)
{
    chooser = std::make_unique<juce::FileChooser> ("Charger des sons", start, "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              juce::StringArray paths;
                              for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                              if (! paths.isEmpty())
                                  ctx.ctl.loadFiles (paths, ctx.ctl.slots[(size_t) ctx.ctl.selectedSlot].used ? -1 : ctx.ctl.selectedSlot);
                          });
}

void StomachPanel::updateDetail()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    const auto& s = ctx.ctl.slots[(size_t) ctx.ctl.selectedSlot];
    modeBox.setSelectedItemIndex (s.slice.mode, juce::dontSendNotification);
    gridBox.setSelectedItemIndex (s.slice.gridDivision, juce::dontSendNotification);
    const bool grid = s.slice.mode == SliceSettings::Grid;
    const bool rnd = s.slice.mode == SliceSettings::Random;
    gridBox.setVisible (grid);
    p1.setVisible (! grid);
    p2.setVisible (! grid);
    p1Label.setVisible (! grid);
    p2Label.setVisible (! grid);
    if (rnd)
    {
        p1.setRange (5.0, 2000.0); p1.setSkewFactor (0.4); p1.setValue (s.slice.randomMinMs, juce::dontSendNotification);
        p2.setRange (5.0, 4000.0); p2.setSkewFactor (0.4); p2.setValue (s.slice.randomMaxMs, juce::dontSendNotification);
        p1.setTextValueSuffix (" ms"); p2.setTextValueSuffix (" ms");
        p1Label.setText ("Min", juce::dontSendNotification);
        p2Label.setText ("Max", juce::dontSendNotification);
    }
    else
    {
        p1.setRange (0.0, 1.0); p1.setSkewFactor (1.0); p1.setValue (s.slice.threshold, juce::dontSendNotification);
        p2.setRange (0.0, 1.0); p2.setSkewFactor (1.0); p2.setValue (s.slice.sensitivity, juce::dontSendNotification);
        p1.setTextValueSuffix (""); p2.setTextValueSuffix ("");
        p1Label.setText ("Seuil", juce::dontSendNotification);
        p2Label.setText ("Sensib.", juce::dontSendNotification);
    }
    gain.setValue (s.gain, juce::dontSendNotification);
    weight.setValue (s.weight, juce::dontSendNotification);
}

juce::Rectangle<int> StomachPanel::slotRow (int i) const
{
    return { 6, 108 + i * 16, getWidth() - 12, 15 };
}

void StomachPanel::resized()
{
    AnimatedPanel::resized();
    swallowBtn.setBounds (8, 22, 84, 30);
    knobs[0]->setBounds (96, 18, 40, 40);
    combos[0]->setBounds (140, 24, 92, 18);
    combos[1]->setBounds (164, 84, 68, 16);

    const int y = 108 + 8 * 16 + 4;
    modeBox.setBounds (8, y, 128, 18);
    loadBtn.setBounds (140, y, 92, 18);
    gridBox.setBounds (8, y + 26, 84, 18);
    p1.setBounds (8, y + 22, 40, 30);     p1Label.setBounds (4, y + 52, 48, 10);
    p2.setBounds (52, y + 22, 40, 30);    p2Label.setBounds (48, y + 52, 48, 10);
    gain.setBounds (100, y + 22, 40, 30); gainLabel.setBounds (96, y + 52, 48, 10);
    weight.setBounds (144, y + 22, 40, 30); weightLabel.setBounds (140, y + 52, 48, 10);
    clearBtn.setBounds (190, y + 26, 42, 18);
    idgemsBtn.setBounds (8, y + 68, 180, 18);
    folderBtn.setBounds (192, y + 68, 40, 18);
}

void StomachPanel::tick()
{
    AnimatedPanel::tick();
    inLevel = ctx.proc.telemetry.inLevel.load();
    scLevel = ctx.proc.telemetry.scLevel.load();
    repaint (0, 56, getWidth(), 50);
}

static void drawBar (juce::Graphics& g, juce::Rectangle<float> r, float norm, juce::Colour c)
{
    g.setColour (Palette::background);
    g.fillRect (r);
    g.setColour (c);
    g.fillRect (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, norm)));
}

void StomachPanel::paint (juce::Graphics& g)
{
    AnimatedPanel::paint (g);
    g.setFont (juce::FontOptions (10.0f));

    auto meter = [&] (int y, float lvl, juce::Colour c, const juce::String& txt)
    {
        g.setColour (Palette::textDim);
        g.drawText (txt, 8, y, 60, 10, juce::Justification::centredLeft);
        const float db = juce::Decibels::gainToDecibels (lvl, -60.0f);
        drawBar (g, { 70.0f, (float) y + 1, (float) getWidth() - 78, 8.0f }, (db + 60.0f) / 60.0f, c);
    };
    meter (58, inLevel, colour, "Entree");
    meter (70, scLevel, Palette::blue, ctx.proc.telemetry.sidechainActive.load() ? "Sidechain" : "SC off");

    // Mémoire
    const float used = (float) ctx.ctl.memoryUsed() / (1024.0f * 1024.0f);
    const float lim = (float) ctx.ctl.memoryLimit() / (1024.0f * 1024.0f);
    g.setColour (Palette::textDim);
    g.drawText ("Memoire " + juce::String (used, 1) + " / " + juce::String ((int) lim) + " Mo", 8, 84, 150, 10, juce::Justification::centredLeft);
    drawBar (g, { 8.0f, 96.0f, (float) getWidth() - 16, 4.0f }, used / juce::jmax (1.0f, lim),
             used / lim > 0.85f ? Palette::red : colour.withAlpha (0.8f));
    g.setColour (Palette::textDim);
    g.drawText ("rempli " + juce::String ((int) ctx.ctl.captureFilledSeconds()) + " s", 140, 44, 92, 10, juce::Justification::centredLeft);

    // Sources
    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto& s = ctx.ctl.slots[(size_t) i];
        auto r = slotRow (i).toFloat();
        if (i == ctx.ctl.selectedSlot)
        {
            g.setColour (s.colour.withAlpha (0.15f));
            g.fillRoundedRectangle (r, 2.0f);
        }
        const auto dot = juce::Rectangle<float> (r.getX() + 3, r.getCentreY() - 4, 8, 8);
        g.setColour (s.colour.withAlpha (s.used && s.active ? 1.0f : 0.25f));
        g.fillEllipse (dot);
        g.setColour (s.used ? Palette::text : Palette::textDim.withAlpha (0.5f));
        g.setFont (juce::FontOptions (10.5f));
        const juce::String name = s.busy ? "... analyse" : (s.used ? s.name : "vide");
        g.drawText (juce::String (i + 1) + "  " + name, r.withTrimmedLeft (14).withTrimmedRight (60), juce::Justification::centredLeft, true);
        if (s.used)
        {
            g.setColour (Palette::textDim);
            g.drawText (juce::String ((int) s.frags.size()) + " frag", r.withTrimmedRight (4), juce::Justification::centredRight);
        }
    }
}

void StomachPanel::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumSlots; ++i)
    {
        if (! slotRow (i).contains (e.getPosition()))
            continue;
        auto& s = ctx.ctl.slots[(size_t) i];
        if (e.x < 20 && s.used)
            ctx.ctl.setSlotActive (i, ! s.active);
        ctx.ctl.selectedSlot = i;
        updateDetail();
        repaint();
        return;
    }
}

//==============================================================================
// CARTE
//==============================================================================
MapView::MapView (UiContext& c) : ctx (c)
{
    maskImage = juce::Image (juce::Image::ARGB, Controller::kMaskSize, Controller::kMaskSize, true);
    flashFrag.fill (-1);
}

juce::Point<float> MapView::posOf (const Fragment& f) const
{
    auto r = getLocalBounds().toFloat().reduced (8.0f);
    return { r.getX() + f.norm[(size_t) ctx.ctl.axisX] * r.getWidth(),
             r.getBottom() - f.norm[(size_t) ctx.ctl.axisY] * r.getHeight() };
}

int MapView::hitTest (juce::Point<float> p) const
{
    if (lib == nullptr) return -1;
    int best = -1;
    float bestD = 64.0f;
    for (int i = 0; i < lib->size(); ++i)
    {
        const float d = posOf (lib->frags[(size_t) i]).getDistanceSquaredFrom (p);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void MapView::tick()
{
    lib = ctx.ctl.getLibrary();

    // Flash des fragments joués
    for (auto& f : flash) f *= 0.85f;
    for (int v = 0; v < 32; ++v)
    {
        const int fr = ctx.proc.engine.voiceFrags[(size_t) v].load();
        if (fr >= 0) { flash[(size_t) v] = 1.0f; flashFrag[(size_t) v] = fr; }
    }
    repaint();
}

void MapView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (r, 3.0f);

    // grille
    g.setColour (Palette::panelEdge.withAlpha (0.35f));
    for (int i = 1; i < 8; ++i)
    {
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * i / 8), r.getY(), r.getBottom());
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 8), r.getX(), r.getRight());
    }

    // zone peinte
    if (! ctx.ctl.maskEmpty)
    {
        const int n = Controller::kMaskSize;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                maskImage.setPixelAt (x, y, Palette::stomach.withAlpha (ctx.ctl.mask[(size_t) (y * n + x)] * 0.45f));
        auto inner = r.reduced (8.0f);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImage (maskImage, inner);
    }

    g.setColour (Palette::textDim.withAlpha (0.6f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (juce::String (axisName (ctx.ctl.axisX)) + " >", r.reduced (6).toNearestInt(), juce::Justification::bottomRight);
    g.drawText ("^ " + juce::String (axisName (ctx.ctl.axisY)), r.reduced (6).toNearestInt(), juce::Justification::topLeft);

    if (lib == nullptr || lib->size() == 0)
    {
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Glisse des sons ici ou clique AVALER\npour nourrir l'Ouroboros", getLocalBounds(), juce::Justification::centred, 2);
        return;
    }

    for (int i = 0; i < lib->size(); ++i)
    {
        const auto& f = lib->frags[(size_t) i];
        const auto p = posOf (f);
        const float size = 3.0f + juce::jlimit (0.0f, 1.0f, f.norm[3]) * 5.0f;
        const float w = lib->weights[(size_t) i];
        g.setColour (f.colour.withAlpha (w > 0 ? 0.85f : 0.25f));
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (p));
        if (f.liked)
        {
            g.setColour (Palette::red);
            g.drawEllipse (juce::Rectangle<float> (size + 6, size + 6).withCentre (p), 1.5f);
        }
        if (f.id == ctx.ctl.selA || f.id == ctx.ctl.selB)
        {
            g.setColour (f.id == ctx.ctl.selA ? Palette::text : Palette::blue);
            g.drawRect (juce::Rectangle<float> (size + 8, size + 8).withCentre (p), 1.0f);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (f.id == ctx.ctl.selA ? "A" : "B", juce::Rectangle<float> (10, 10).withCentre (p.translated (size, -size)),
                        juce::Justification::centred);
        }
    }

    // fragments en lecture
    for (int v = 0; v < 32; ++v)
    {
        const int fr = flashFrag[(size_t) v];
        if (fr < 0 || fr >= lib->size() || flash[(size_t) v] < 0.05f) continue;
        const auto p = posOf (lib->frags[(size_t) fr]);
        const float s = 10.0f + (1.0f - flash[(size_t) v]) * 14.0f;
        g.setColour (Palette::text.withAlpha (flash[(size_t) v] * 0.8f));
        g.drawEllipse (juce::Rectangle<float> (s, s).withCentre (p), 1.2f);
    }

    if (hover >= 0 && hover < lib->size())
    {
        const auto& f = lib->frags[(size_t) hover];
        const auto& d = f.desc;
        juce::String info;
        info << (f.slot >= 0 ? "Source " + juce::String (f.slot + 1) : "Gen " + juce::String (f.generation))
             << "  |  " << juce::String (d.lengthSec * 1000.0f, 0) << " ms  |  "
             << (d.pitchMidi >= 0 ? juce::MidiMessage::getMidiNoteName ((int) std::round (d.pitchMidi), true, true, 3) : juce::String ("atonal"))
             << "  |  " << juce::String ((int) d.centroidHz) << " Hz  |  " << juce::String (d.rmsDb, 0) << " dB";
        g.setColour (Palette::panel.withAlpha (0.9f));
        auto tr = juce::Rectangle<int> (6, getHeight() - 20, getWidth() - 12, 16);
        g.fillRect (tr);
        g.setColour (Palette::text);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (info, tr, juce::Justification::centred);
    }

    if (tool == Brush || tool == Eraser)
    {
        const auto m = getMouseXYRelative().toFloat();
        if (getLocalBounds().contains (m.toInt()))
        {
            const float rad = brushSize * (getWidth() - 16);
            g.setColour ((tool == Brush ? Palette::stomach : Palette::text).withAlpha (0.6f));
            g.drawEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (m), 1.0f);
        }
    }
}

void MapView::paintAt (juce::Point<float> p)
{
    auto r = getLocalBounds().toFloat().reduced (8.0f);
    const float x = (p.x - r.getX()) / r.getWidth();
    const float y = (p.y - r.getY()) / r.getHeight();
    ctx.ctl.paintMask (x, y, brushSize, tool == Brush ? 0.25f : -0.35f);
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastPublish > 120)
    {
        lastPublish = now;
        ctx.ctl.publishLibrary();
    }
}

void MapView::mouseDown (const juce::MouseEvent& e)
{
    downPos = e.position;
    dragStarted = false;
    if (tool == Brush || tool == Eraser)
    {
        paintAt (e.position);
        return;
    }
    const int i = hitTest (e.position);
    if (i < 0 || lib == nullptr)
        return;
    const auto id = lib->frags[(size_t) i].id;
    if (tool == Like)
        ctx.ctl.toggleLike (id);
    else
    {
        if (e.mods.isShiftDown()) ctx.ctl.selB = id;
        else ctx.ctl.selA = id;
        ctx.ctl.audition (i);
    }
    repaint();
}

void MapView::mouseDrag (const juce::MouseEvent& e)
{
    if (tool == Brush || tool == Eraser)
    {
        paintAt (e.position);
        return;
    }
    // Glisser un fragment hors du plugin = vers Ableton
    if (tool == Listen && ! dragStarted && e.getDistanceFromDragStart() > 8 && lib != nullptr)
    {
        const int i = hitTest (downPos);
        if (i >= 0)
        {
            dragStarted = true;
            auto file = ctx.ctl.writeFragmentForDrag (lib->frags[(size_t) i].id);
            if (file.existsAsFile())
                juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
        }
    }
}

void MapView::mouseUp (const juce::MouseEvent&)
{
    if (tool == Brush || tool == Eraser)
        ctx.ctl.publishLibrary();
}

void MapView::mouseMove (const juce::MouseEvent& e)
{
    hover = (tool == Listen || tool == Like) ? hitTest (e.position) : -1;
}

//==============================================================================
MapPanel::MapPanel (UiContext& c) : AnimatedPanel (c, "CARTE DES FRAGMENTS", Palette::map, -1), map (c)
{
    addAndMakeVisible (map);
    for (auto* b : { &axisX, &axisY })
    {
        for (int a = 0; a < kNumAxes; ++a) b->addItem (axisName (a), a + 1);
        addAndMakeVisible (b);
        b->onChange = [this] { ctx.ctl.setAxes (axisX.getSelectedItemIndex(), axisY.getSelectedItemIndex()); };
    }
    axisX.setTooltip ("Axe horizontal");
    axisY.setTooltip ("Axe vertical");
    axisX.setSelectedItemIndex (ctx.ctl.axisX, juce::dontSendNotification);
    axisY.setSelectedItemIndex (ctx.ctl.axisY, juce::dontSendNotification);

    juce::TextButton* tools[] = { &listenBtn, &brushBtn, &eraserBtn, &likeBtn };
    const char* tips[] = { "Ecoute : clic = ecouter + choisir A, Maj+clic = choisir B, glisser = exporter vers Ableton (1)",
                           "Pinceau : peins la zone ou piocher (densite = probabilite) (2)",
                           "Gomme (3)", "Like : les fragments likes survivent aux generations et sont favorises (4)" };
    for (int i = 0; i < 4; ++i)
    {
        styleButton (*tools[i], Palette::orange, false);
        tools[i]->setRadioGroupId (77);
        tools[i]->setClickingTogglesState (true);
        tools[i]->setTooltip (tips[i]);
        tools[i]->onClick = [this, i] { map.tool = i; };
        addAndMakeVisible (tools[i]);
    }
    listenBtn.setToggleState (true, juce::dontSendNotification);

    styleButton (clearBtn, colour);
    clearBtn.setTooltip ("Effacer la zone peinte");
    clearBtn.onClick = [this] { ctx.ctl.clearMask(); };
    addAndMakeVisible (clearBtn);

    brush.setSliderStyle (juce::Slider::LinearHorizontal);
    brush.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    brush.setRange (0.03, 0.4);
    brush.setValue (map.brushSize);
    brush.setColour (juce::Slider::trackColourId, Palette::orange);
    brush.setColour (juce::Slider::thumbColourId, Palette::text);
    brush.setTooltip ("Taille du pinceau");
    brush.onValueChange = [this] { map.brushSize = (float) brush.getValue(); };
    addAndMakeVisible (brush);

    ctx.ctl.addChangeListener (this);
}

MapPanel::~MapPanel() { ctx.ctl.removeChangeListener (this); }

void MapPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    axisX.setSelectedItemIndex (ctx.ctl.axisX, juce::dontSendNotification);
    axisY.setSelectedItemIndex (ctx.ctl.axisY, juce::dontSendNotification);
}

void MapPanel::resized()
{
    AnimatedPanel::resized();
    const int y = 3;
    axisX.setBounds (140, y, 78, 15);
    axisY.setBounds (222, y, 78, 15);
    int x = 304;
    for (auto* b : { &listenBtn, &brushBtn, &eraserBtn, &likeBtn })
    {
        b->setBounds (x, y, 46, 15);
        x += 48;
    }
    clearBtn.setBounds (getWidth() - 40, y, 34, 15);
    map.setBounds (getLocalBounds().reduced (6).withTrimmedTop (16));
    brush.setBounds (getWidth() - 96, getHeight() - 18, 84, 12);
}

void MapPanel::tick()
{
    AnimatedPanel::tick();
    map.tick();
}
