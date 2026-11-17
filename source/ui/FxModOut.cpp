#include <map>
#include <algorithm>
#include "Panels.h"

//==============================================================================
// RACK FX
//==============================================================================
FxPanel::FxPanel (UiContext& c) : AnimatedPanel (c, "FX RACK", Palette::fx, (int) P::Module::Fx)
{
    struct Def { int on; const char* name; std::vector<std::pair<int, const char*>> items; };
    // items : param + libellé ; un libellé "#" = liste, "!" = bouton
    const Def defs[7] =
    {
        { P::crushOn, "CRUSH",   { { P::crushMix, "Mix" }, { P::crushBits, "Bits" }, { P::crushDown, "Down" } } },
        { P::distOn,  "DIST",    { { P::distMix, "Mix" }, { P::distDrive, "Drive" }, { P::distMode, "#" } } },
        { P::filtOn,  "FILTRE",  { { P::filtMix, "Mix" }, { P::filtCutoff, "Coupure" }, { P::filtReso, "Reso" }, { P::filtType, "#" } } },
        { P::shiftOn, "SHIFT",   { { P::shiftMix, "Mix" }, { P::shiftAmount, "Decal." }, { P::shiftMode, "#" } } },
        { P::stutOn,  "STUTTER", { { P::stutMix, "Mix" }, { P::stutChance, "Chance" }, { P::stutRepeats, "Rep." }, { P::stutDiv, "#" } } },
        { P::dlyOn,   "DELAY",   { { P::dlyMix, "Mix" }, { P::dlyFeedback, "Feedb." }, { P::dlyTime, "#" }, { P::dlyPingPong, "!PING" } } },
        { P::verbOn,  "REVERB",  { { P::verbMix, "Mix" }, { P::verbSize, "Taille" }, { P::verbDamp, "Amorti" } } },
    };

    for (int fx = 0; fx < 7; ++fx)
    {
        auto& t = tiles[(size_t) fx];
        t.on = &addToggle (defs[fx].on, defs[fx].name);
        t.on->setTooltip (juce::String ("Activer ") + defs[fx].name + " (glisse la poignee pour changer l'ordre)");
        int k = 0;
        for (auto& [p, label] : defs[fx].items)
        {
            juce::String l (label);
            if (l == "#")       t.c[k++] = &addCombo (p);
            else if (l.startsWith ("!")) t.c[k++] = &addToggle (p, l.substring (1));
            else                t.c[k++] = &addKnob (p, l);
        }
    }
}

juce::Rectangle<int> FxPanel::tileBounds (int position) const
{
    const int w = (getWidth() - 12) / 7;
    return { 6 + position * w, 20, w - 3, getHeight() - 26 };
}

void FxPanel::resized()
{
    AnimatedPanel::resized();
    const auto& order = ctx.ctl.mods.fxOrder;
    for (int pos = 0; pos < 7; ++pos)
    {
        auto& t = tiles[(size_t) order[(size_t) pos]];
        auto r = tileBounds (pos);
        t.on->setBounds (r.getX() + 2, r.getY() + 2, r.getWidth() - 18, 15);
        const int cw = (r.getWidth() - 4) / 2;
        int count = 0;
        for (auto* comp : t.c) if (comp != nullptr) ++count;
        for (int k = 0; k < 4; ++k)
        {
            auto* comp = t.c[k];
            if (comp == nullptr) continue;
            const bool isKnob = dynamic_cast<Knob*> (comp) != nullptr;
            auto cell = juce::Rectangle<int> (r.getX() + 2 + (k % 2) * cw, r.getY() + 20 + (k / 2) * 38, cw, 38);
            if (isKnob)
                comp->setBounds (cell);
            else if (count == 3 && k == 2)
                comp->setBounds (r.getX() + 3, r.getY() + 20 + 38 + 11, r.getWidth() - 6, 16); // liste seule : toute la largeur
            else if (k == 3 && dynamic_cast<Knob*> (t.c[2]) != nullptr)
                comp->setBounds (r.getX() + 2 + cw, r.getY() + 20 + 38 + 11, r.getWidth() - cw - 4, 16);
            else
                comp->setBounds (cell.withSizeKeepingCentre (cw - 2, 16));
        }
    }
}

void FxPanel::paint (juce::Graphics& g)
{
    AnimatedPanel::paint (g);
    for (int pos = 0; pos < 7; ++pos)
    {
        auto r = tileBounds (pos).toFloat();
        g.setColour (pos == dragOver ? colour.withAlpha (0.25f) : Palette::background.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 3.0f);
        // poignée de déplacement
        auto grip = juce::Rectangle<float> (r.getRight() - 15, r.getY() + 3, 12, 13);
        g.setColour (Palette::textDim);
        for (int i = 0; i < 3; ++i)
            g.fillRect (grip.getX() + 2, grip.getY() + 3 + i * 3.5f, 8.0f, 1.5f);
        g.setColour (Palette::textDim.withAlpha (0.4f));
        g.setFont (juce::FontOptions (8.0f));
        g.drawText (juce::String (pos + 1), r.removeFromBottom (10).toNearestInt(), juce::Justification::centredRight);
    }
}

void FxPanel::mouseDown (const juce::MouseEvent& e)
{
    dragFrom = -1;
    for (int pos = 0; pos < 7; ++pos)
    {
        auto r = tileBounds (pos);
        if (juce::Rectangle<int> (r.getRight() - 16, r.getY(), 16, 18).contains (e.getPosition()))
            dragFrom = pos;
    }
}

void FxPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    dragOver = -1;
    for (int pos = 0; pos < 7; ++pos)
        if (tileBounds (pos).getHorizontalRange().contains (e.x)) dragOver = pos;
    repaint();
}

void FxPanel::mouseUp (const juce::MouseEvent&)
{
    if (dragFrom >= 0 && dragOver >= 0 && dragOver != dragFrom)
    {
        ctx.ctl.moveFx (dragFrom, dragOver);
        resized();
    }
    dragFrom = dragOver = -1;
    repaint();
}

//==============================================================================
// MODULATION
//==============================================================================
LfoEditor::LfoEditor (UiContext& c) : ctx (c)
{
    setTooltip ("Crayon : dessine la forme du LFO. Pour moduler un reglage : clic droit sur son potard > Moduler par");
}

void LfoEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (Palette::panelEdge.withAlpha (0.5f));
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    const auto& shape = ctx.ctl.mods.shapes[(size_t) lfo];
    juce::Path p;
    for (int i = 0; i <= kLfoPoints; ++i)
    {
        const float x = r.getX() + r.getWidth() * i / kLfoPoints;
        const float y = r.getBottom() - shape[(size_t) (i % kLfoPoints)] * r.getHeight();
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (Palette::modulation.withAlpha (0.2f));
    juce::Path fill (p);
    fill.lineTo (r.getRight(), r.getBottom());
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();
    g.fillPath (fill);
    g.setColour (Palette::modulation);
    g.strokePath (p, juce::PathStrokeType (1.5f));

    const float ph = ctx.proc.engine.getModEngine().lfoPhases[(size_t) lfo].load();
    g.setColour (Palette::text.withAlpha (0.7f));
    g.drawVerticalLine ((int) (r.getX() + ph * r.getWidth()), r.getY(), r.getBottom());
}

void LfoEditor::draw (const juce::MouseEvent& e)
{
    const int i = juce::jlimit (0, kLfoPoints - 1, (int) (e.position.x / getWidth() * kLfoPoints));
    const float v = juce::jlimit (0.0f, 1.0f, 1.0f - e.position.y / getHeight());
    auto& shape = ctx.ctl.mods.shapes[(size_t) lfo];
    if (lastPoint >= 0 && lastPoint != i)
    {
        // Remplit les points sautés pendant un geste rapide
        const int a = juce::jmin (lastPoint, i), b = juce::jmax (lastPoint, i);
        const float va = shape[(size_t) lastPoint];
        for (int k = a; k <= b; ++k)
            shape[(size_t) k] = juce::jmap ((float) (k - lastPoint) / (float) (i - lastPoint), va, v);
    }
    shape[(size_t) i] = v;
    lastPoint = i;
    repaint();
}

void LfoEditor::mouseDown (const juce::MouseEvent& e) { lastPoint = -1; draw (e); }
void LfoEditor::mouseDrag (const juce::MouseEvent& e) { draw (e); }
void LfoEditor::mouseUp (const juce::MouseEvent&) { ctx.ctl.publishMods(); }

ModPanel::ModPanel (UiContext& c) : AnimatedPanel (c, "MODULATION", Palette::modulation, (int) P::Module::Mod), editor (c)
{
    const char* names[] = { "LFO1", "LFO2", "LFO3", "LFO4", "RND1", "RND2", "MACROS" };
    for (int i = 0; i < 7; ++i)
    {
        auto* b = tabs.add (new juce::TextButton (names[i]));
        styleButton (*b, colour);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (99);
        b->onClick = [this, i] { showPage (i); };
        addAndMakeVisible (b);
    }
    addChildComponent (editor);

    for (int l = 0; l < 4; ++l)
    {
        const int o = l * 4;
        pageComps[l].add (new Knob (ctx, P::lfo1Rate + o, colour, "Vitesse"));
        pageComps[l].add (new Knob (ctx, P::lfo1Stereo + o, colour, "Stereo"));
        pageComps[l].add (new ParamToggle (ctx, P::lfo1Sync + o, "SYNC", colour));
        pageComps[l].add (new ParamCombo (ctx, P::lfo1Div + o));
    }
    for (int r = 0; r < 2; ++r)
    {
        pageComps[4 + r].add (new Knob (ctx, P::rnd1Rate + r * 2, colour, "Vitesse"));
        pageComps[4 + r].add (new Knob (ctx, P::rnd1Smooth + r * 2, colour, "Lissage"));
    }
    pageComps[6].add (new Knob (ctx, P::chaos, Palette::red, "CHAOS"));
    for (int m = 0; m < 8; ++m)
        pageComps[6].add (new Knob (ctx, P::macro1 + m, colour, "Macro " + juce::String (m + 1)));

    for (auto& page : pageComps)
        for (auto* comp : page)
        {
            addChildComponent (comp);
        }

    styleButton (shapeMenu, colour);
    shapeMenu.setTooltip ("Formes predefinies");
    shapeMenu.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Sinus"); m.addItem (2, "Triangle"); m.addItem (3, "Rampe"); m.addItem (4, "Carre");
        m.addItem (5, "Aleatoire (marches)"); m.addItem (6, "Aleatoire (lisse)");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&shapeMenu), [this] (int r)
        {
            if (r == 0) return;
            auto& s = ctx.ctl.mods.shapes[(size_t) editor.lfo];
            juce::Random rng;
            float prev = 0.5f;
            for (int i = 0; i < kLfoPoints; ++i)
            {
                const float ph = (float) i / kLfoPoints;
                float v = 0;
                switch (r)
                {
                    case 1: v = 0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi * ph); break;
                    case 2: v = ph < 0.5f ? ph * 2 : 2 - ph * 2; break;
                    case 3: v = ph; break;
                    case 4: v = ph < 0.5f ? 1.0f : 0.0f; break;
                    case 5: v = (i % 4 == 0) ? rng.nextFloat() : prev; break;
                    default: v = juce::jlimit (0.0f, 1.0f, prev + (rng.nextFloat() - 0.5f) * 0.3f); break;
                }
                s[(size_t) i] = prev = v;
            }
            ctx.ctl.publishMods();
            editor.repaint();
        });
    };
    addChildComponent (shapeMenu);

    tabs[0]->setToggleState (true, juce::dontSendNotification);
    showPage (0);
}

void ModPanel::showPage (int p)
{
    page = p;
    for (int i = 0; i < 7; ++i)
        for (auto* comp : pageComps[i])
            comp->setVisible (i == p);
    editor.setVisible (p < 4);
    shapeMenu.setVisible (p < 4);
    editor.lfo = juce::jlimit (0, 3, p);
    resized();
    repaint();
}

void ModPanel::resized()
{
    AnimatedPanel::resized();
    for (int i = 0; i < tabs.size(); ++i)
        tabs[i]->setBounds (96 + i * 46, 3, 44, 14);

    if (page < 4)
    {
        editor.setBounds (8, 22, 290, getHeight() - 28);
        auto& c = pageComps[page];
        c[0]->setBounds (304, 22, 44, 44);
        c[1]->setBounds (350, 22, 44, 44);
        c[2]->setBounds (400, 24, 64, 16);
        c[3]->setBounds (400, 44, 64, 16);
        shapeMenu.setBounds (304, 72, 90, 16);
    }
    else if (page < 6)
    {
        auto& c = pageComps[page];
        c[0]->setBounds (8, 24, 48, 48);
        c[1]->setBounds (60, 24, 48, 48);
    }
    else
    {
        auto& c = pageComps[6];
        for (int i = 0; i < c.size(); ++i)
            c[i]->setBounds (8 + i * 62, 26, 56, 56);
    }
}

void ModPanel::paint (juce::Graphics& g)
{
    AnimatedPanel::paint (g);
    g.setFont (juce::FontOptions (9.5f));
    g.setColour (Palette::textDim);

    // Cibles de la source affichée
    const int src = page < 4 ? page : (page < 6 ? page : -1);
    if (src >= 0)
    {
        juce::StringArray targets;
        for (auto& r : ctx.ctl.mods.routes)
            if (r.source == src && r.param >= 0)
                targets.add (juce::String (P::info (r.param).name) + " " + juce::String (juce::roundToInt (r.depth * 100)) + "%");
        auto area = page < 4 ? juce::Rectangle<int> (470, 22, getWidth() - 476, getHeight() - 28)
                             : juce::Rectangle<int> (240, 22, getWidth() - 246, getHeight() - 28);
        g.drawFittedText ("Cibles : " + (targets.isEmpty() ? juce::String ("aucune (clic droit sur un potard > Moduler par)")
                                                          : targets.joinIntoString (", ")),
                          area, juce::Justification::topLeft, 7);
    }
    if (page == 4 || page == 5)
    {
        const float v = ctx.proc.engine.getModEngine().sourceValues[(size_t) page].load();
        auto bar = juce::Rectangle<float> (120, 30, 110, 10);
        g.setColour (Palette::background);
        g.fillRect (bar);
        g.setColour (colour);
        const float cx = bar.getCentreX();
        const float x = cx + v * bar.getWidth() * 0.5f;
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, x), bar.getY(), juce::jmax (cx, x), bar.getBottom()));
    }
    if (page == 6)
    {
        g.drawText ("CHAOS pousse mutation, contraste, random et FX d'un coup. Macros : clic droit sur un potard > Moduler par > Macro",
                    8, getHeight() - 16, getWidth() - 16, 12, juce::Justification::centredLeft);
    }
}

void ModPanel::tick()
{
    AnimatedPanel::tick();
    for (auto* comp : pageComps[page])
        if (auto* k = dynamic_cast<Knob*> (comp))
            k->refresh();
    if (page < 4) editor.repaint();
    if (page == 4 || page == 5) repaint (120, 28, 112, 14);
}

//==============================================================================
// SORTIE
//==============================================================================
OutputPanel::OutputPanel (UiContext& c) : AnimatedPanel (c, "SORTIE", Palette::output, (int) P::Module::Out)
{
    for (int p : { (int) P::ottAmount, (int) P::clipDrive, (int) P::limCeiling, (int) P::hpFreq, (int) P::monoFreq,
                   (int) P::dryWet, (int) P::outGain })
        addKnob (p);
    knobs[5]->setTooltip ("Dry/Wet : 0 = son d'entree seul, 1 = Ouroboros seul");

    styleButton (panicBtn, Palette::red);
    panicBtn.setColour (juce::TextButton::buttonColourId, Palette::red.withAlpha (0.25f));
    panicBtn.setTooltip ("Coupe immediatement toutes les voix et les queues d'effets (raccourci : P)");
    panicBtn.onClick = [this] { ctx.proc.commands.push ({ AudioCommand::Panic, 0 }); ctx.ctl.setStatus ("Panic !"); };
    addAndMakeVisible (panicBtn);

    styleButton (exportBtn, Palette::orange);
    exportBtn.setTooltip ("Exporter en WAV 24 bits (+ fichier JSON de metadonnees)");
    exportBtn.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Generation courante (loop)");
        m.addItem (2, "Fragments likes (one-shots)");
        m.addItem (3, "Loop : rendu des reglages actuels");
        juce::PopupMenu batch;
        for (int n : { 4, 8, 16, 32 }) batch.addItem (100 + n, juce::String (n) + " variations");
        m.addSubMenu ("Batch (seeds differentes)", batch);
        m.addSeparator();
        m.addItem (10, "Choisir le dossier d'export...");
        m.addItem (11, "Ouvrir le dossier d'export");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&exportBtn), [this] (int r)
        {
            auto& ctl = ctx.ctl;
            if (r == 1) ctl.exportGeneration();
            else if (r == 2) ctl.exportLiked();
            else if (r == 3) ctl.exportLoop();
            else if (r > 100) ctl.exportBatch (r - 100);
            else if (r == 10)
            {
                auto fc = std::make_shared<juce::FileChooser> ("Dossier d'export", ctl.getExportFolder());
                fc->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                 [this, fc] (const juce::FileChooser& f)
                                 {
                                     if (f.getResult().isDirectory()) ctx.ctl.setExportFolder (f.getResult());
                                 });
            }
            else if (r == 11)
            {
                ctl.getExportFolder().createDirectory();
                ctl.getExportFolder().startAsProcess();
            }
        });
    };
    addAndMakeVisible (exportBtn);
}

void OutputPanel::resized()
{
    AnimatedPanel::resized();
    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (60 + i * 44, 14, 42, 50);
    meterArea = { 380, 24, 240, 30 };
    panicBtn.setBounds (630, 22, 64, 36);
    exportBtn.setBounds (700, 22, 72, 36);
    dragZone = { 778, 22, 170, 36 };
}

void OutputPanel::tick()
{
    AnimatedPanel::tick();
    outL = ctx.proc.telemetry.outLevelL.load();
    outR = ctx.proc.telemetry.outLevelR.load();
    repaint (meterArea);
    repaint (956, 14, getWidth() - 960, getHeight() - 18);
}

void OutputPanel::paint (juce::Graphics& g)
{
    AnimatedPanel::paint (g);

    auto meter = [&] (juce::Rectangle<float> r, float lvl, const juce::String& t)
    {
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (t, r.removeFromLeft (14).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (Palette::background);
        g.fillRect (r);
        const float db = juce::Decibels::gainToDecibels (lvl, -60.0f);
        const float n = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        juce::ColourGradient grad (Palette::blue, r.getX(), 0, Palette::red, r.getRight(), 0, false);
        grad.addColour (0.75, Palette::orange);
        g.setGradientFill (grad);
        g.fillRect (r.withWidth (r.getWidth() * n));
    };
    auto m = meterArea.toFloat();
    meter (m.removeFromTop (12), outL, "G");
    m.removeFromTop (6);
    meter (m.removeFromTop (12), outR, "D");

    // zone de glisser-déposer vers Ableton
    g.setColour (Palette::orange.withAlpha (dragging ? 0.4f : 0.12f));
    g.fillRoundedRectangle (dragZone.toFloat(), 3.0f);
    g.setColour (Palette::orange);
    g.drawRoundedRectangle (dragZone.toFloat().reduced (0.5f), 3.0f, 1.0f);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawFittedText ("GLISSE LA GENERATION\nVERS ABLETON", dragZone, juce::Justification::centred, 2);

    // Statut
    auto st = juce::Rectangle<int> (956, 14, getWidth() - 962, getHeight() - 18);
    const bool recent = juce::Time::getMillisecondCounter() - ctx.ctl.statusTime < 8000;
    g.setColour (recent ? Palette::text : Palette::textDim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawFittedText ((ctx.ctl.isBusy() ? "[travail en cours] " : "") + ctx.ctl.status, st, juce::Justification::centredLeft, 3);
}

void OutputPanel::mouseDown (const juce::MouseEvent& e)
{
    dragging = dragZone.contains (e.getPosition());
}

void OutputPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging && e.getDistanceFromDragStart() > 6)
    {
        dragging = false;
        auto f = ctx.ctl.writeLoopForDrag();
        if (f.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
        repaint();
    }
}

//==============================================================================
// BARRE HAUTE
//==============================================================================
TopBar::TopBar (UiContext& c) : ctx (c)
{
    for (auto* b : { &presetBtn, &prevBtn, &nextBtn, &saveBtn, &abBtn, &copyBtn, &undoBtn, &redoBtn,
                     &historyBtn, &seedBtn, &helpBtn, &sizeBtn })
    {
        styleButton (*b, Palette::orange);
        addAndMakeVisible (b);
    }
    styleButton (randomBtn, Palette::orange);
    randomBtn.setColour (juce::TextButton::buttonColourId, Palette::orange.withAlpha (0.3f));
    addAndMakeVisible (randomBtn);

    presetBtn.setTooltip ("Navigateur de presets (favoris, tags)");
    presetBtn.onClick = [this] { showPresetBrowser(); };
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (1); };
    saveBtn.setTooltip ("Sauvegarder le preset (nom + tags)");
    saveBtn.onClick = [this]
    {
        auto* w = new juce::AlertWindow ("Sauvegarder le preset", "Nom et tags (separes par des virgules)", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", ctx.ctl.currentPresetName, "Nom");
        w->addTextEditor ("tags", "", "Tags");
        w->addButton ("Sauver", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Sauver en favori", 2);
        w->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r > 0)
            {
                auto tags = juce::StringArray::fromTokens (w->getTextEditorContents ("tags"), ",", "");
                tags.trim();
                tags.removeEmptyStrings();
                const auto name = w->getTextEditorContents ("name").trim();
                if (name.isNotEmpty())
                    ctx.ctl.savePreset (name, tags, r == 2);
            }
        }), true);
    };

    abBtn.setTooltip ("Comparer A / B");
    abBtn.onClick = [this] { ctx.ctl.toggleAB(); };
    copyBtn.setTooltip ("Copier le reglage courant dans l'autre memoire");
    copyBtn.onClick = [this] { ctx.ctl.copyAtoB(); };
    undoBtn.setTooltip ("Annuler (Ctrl+Z)");
    undoBtn.onClick = [this] { ctx.ctl.undo(); };
    redoBtn.setTooltip ("Retablir (Ctrl+Y)");
    redoBtn.onClick = [this] { ctx.ctl.redo(); };

    randomBtn.setTooltip ("Random general (raccourci : R). Cadenas et plages respectes.");
    randomBtn.onClick = [this] { ctx.ctl.randomizeAll (ctx.ctl.nextSeed()); };
    seedBtn.setTooltip ("Seed du dernier random : clic pour rejouer ou taper une seed");
    seedBtn.onClick = [this]
    {
        auto* w = new juce::AlertWindow ("Seed", "Rejouer un random avec cette seed", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("seed", juce::String ((int) ctx.ctl.lastSeed));
        w->addButton ("Rejouer", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r == 1)
            {
                const auto s = (uint32_t) w->getTextEditorContents ("seed").getLargeIntValue();
                ctx.ctl.lastSeed = s;
                ctx.ctl.randomizeAll (s);
            }
        }), true);
    };
    historyBtn.setTooltip ("Historique des randoms (retour en arriere, favoris)");
    historyBtn.onClick = [this] { showHistory(); };

    helpBtn.setTooltip ("Bulles d'aide on/off");
    helpBtn.setClickingTogglesState (true);
    helpBtn.setToggleState (true, juce::dontSendNotification);
    helpBtn.onClick = [this] { if (onHelpToggle) onHelpToggle (helpBtn.getToggleState()); };

    sizeBtn.onClick = [this]
    {
        juce::PopupMenu m;
        for (int pct : { 50, 75, 100, 125, 150, 200 })
            m.addItem (pct, juce::String (pct) + " %");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sizeBtn), [this] (int r)
        {
            if (r > 0 && onSizePreset) onSizePreset (r);
        });
    };

    amount = std::make_unique<Knob> (ctx, P::randomAmount, Palette::orange, "Dosage");
    chaos = std::make_unique<Knob> (ctx, P::chaos, Palette::red, "CHAOS");
    addAndMakeVisible (*amount);
    addAndMakeVisible (*chaos);

    ctx.ctl.addChangeListener (this);
    changeListenerCallback (nullptr);
}

TopBar::~TopBar() { ctx.ctl.removeChangeListener (this); }

void TopBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    presetBtn.setButtonText (ctx.ctl.currentPresetName);
    abBtn.setButtonText (ctx.ctl.abIsB ? "B" : "A");
    seedBtn.setButtonText ("seed " + juce::String ((int) ctx.ctl.lastSeed));
}

void TopBar::resized()
{
    presetBtn.setBounds (232, 10, 140, 24);
    prevBtn.setBounds (374, 10, 18, 24);
    nextBtn.setBounds (394, 10, 18, 24);
    saveBtn.setBounds (416, 10, 40, 24);
    abBtn.setBounds (464, 10, 24, 24);
    copyBtn.setBounds (490, 10, 34, 24);
    undoBtn.setBounds (532, 10, 42, 24);
    redoBtn.setBounds (576, 10, 42, 24);
    randomBtn.setBounds (628, 6, 86, 32);
    amount->setBounds (716, 1, 42, 42);
    seedBtn.setBounds (760, 10, 84, 24);
    historyBtn.setBounds (846, 10, 44, 24);
    chaos->setBounds (894, 1, 42, 42);
    helpBtn.setBounds (getWidth() - 96, 10, 22, 24);
    sizeBtn.setBounds (getWidth() - 70, 10, 62, 24);
}

void TopBar::tick()
{
    auto& t = ctx.proc.telemetry;
    const auto counter = t.noteCounter.load();
    if (counter != lastNoteCounter)
    {
        lastNoteCounter = counter;
        lastNote = t.lastNote.load();
        midiFlash = 0.4f + 0.6f * t.lastVelocity.load();
    }
    midiFlash *= 0.88f;
    amount->refresh();
    chaos->refresh();
    repaint (940, 0, 160, getHeight());
}

void TopBar::paint (juce::Graphics& g)
{
    auto title = juce::Rectangle<int> (12, 0, 216, getHeight());
    g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    g.setColour (Palette::red.withAlpha (0.6f));
    g.drawText ("IDGems OUROBOROS", title.translated (-1, 0), juce::Justification::centredLeft);
    g.setColour (Palette::blue.withAlpha (0.6f));
    g.drawText ("IDGems OUROBOROS", title.translated (1, 0), juce::Justification::centredLeft);
    g.setColour (Palette::text);
    g.drawText ("IDGems OUROBOROS", title, juce::Justification::centredLeft);

    auto led = juce::Rectangle<float> (944, 16, 11, 11);
    g.setColour (Palette::panelEdge);
    g.fillEllipse (led);
    g.setColour (Palette::blue.withAlpha (juce::jlimit (0.0f, 1.0f, midiFlash)));
    g.fillEllipse (led);
    g.setColour (Palette::text);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("MIDI " + (lastNote >= 0 ? juce::MidiMessage::getMidiNoteName (lastNote, true, true, 3) : juce::String ("--")),
                960, 0, 70, getHeight(), juce::Justification::centredLeft);
    g.drawText (juce::String (ctx.proc.telemetry.bpm.load(), 1) + " BPM", 1030, 0, 70, getHeight(), juce::Justification::centredLeft);
}

void TopBar::showPresetBrowser()
{
    auto presets = ctx.ctl.listPresets();
    auto shared = std::make_shared<std::vector<PresetInfo>> (presets);

    juce::PopupMenu m, byTag, favs, all, favToggle;
    std::map<juce::String, juce::PopupMenu> tagMenus;
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        const auto& p = presets[(size_t) i];
        const auto label = (p.favourite ? juce::String ("* ") : juce::String()) + p.name + (p.factory ? "  (usine)" : "");
        all.addItem (1 + i, label, true, p.name == ctx.ctl.currentPresetName);
        if (p.favourite) favs.addItem (1 + i, p.name);
        for (auto& t : p.tags) tagMenus[t].addItem (1 + i, p.name);
        favToggle.addItem (1000 + i, p.name, true, p.favourite);
    }
    for (auto& [tag, menu] : tagMenus) byTag.addSubMenu (tag, menu);

    m.addSectionHeader ("PRESETS");
    m.addSubMenu ("Favoris", favs, favs.getNumItems() > 0);
    m.addSubMenu ("Par tag", byTag);
    m.addSeparator();
    m.addSubMenu ("Tous", all);
    m.addSubMenu ("Marquer / retirer des favoris", favToggle);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn), [this, shared] (int r)
    {
        if (r >= 1000) ctx.ctl.setPresetFavourite ((*shared)[(size_t) (r - 1000)], ! (*shared)[(size_t) (r - 1000)].favourite);
        else if (r >= 1) ctx.ctl.loadPreset ((*shared)[(size_t) (r - 1)]);
    });
}

void TopBar::stepPreset (int delta)
{
    auto presets = ctx.ctl.listPresets();
    if (presets.empty()) return;
    int cur = 0;
    for (int i = 0; i < (int) presets.size(); ++i)
        if (presets[(size_t) i].name == ctx.ctl.currentPresetName) cur = i;
    cur = (cur + delta + (int) presets.size()) % (int) presets.size();
    ctx.ctl.loadPreset (presets[(size_t) cur]);
}

void TopBar::showHistory()
{
    juce::PopupMenu m, fav;
    m.addSectionHeader ("Revenir a l'etat AVANT un random");
    const auto& h = ctx.ctl.history;
    if (h.empty())
        m.addItem (-1, "(vide)", false);
    for (int i = 0; i < (int) h.size(); ++i)
    {
        const auto txt = (h[(size_t) i].favourite ? juce::String ("* ") : juce::String()) + h[(size_t) i].label + " - seed " + juce::String ((int) h[(size_t) i].seed);
        m.addItem (1 + i, txt);
        fav.addItem (1000 + i, txt, true, h[(size_t) i].favourite);
    }
    m.addSeparator();
    m.addSubMenu ("Favoris (gardes dans l'historique)", fav);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&historyBtn), [this] (int r)
    {
        if (r >= 1000) ctx.ctl.toggleFavourite (r - 1000);
        else if (r >= 1) ctx.ctl.recallHistory (r - 1);
    });
}
