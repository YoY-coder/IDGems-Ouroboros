#include <map>
#include <algorithm>
#include "Panels.h"

//==============================================================================
// OUROBOROS : anneau + générations + arbre
//==============================================================================
OuroPanel::OuroPanel (UiContext& c) : AnimatedPanel (c, "OUROBOROS", Palette::ouroboros, (int) P::Module::Ouro)
{
    addCombo (P::genMode).setTooltip ("Manuel : bouton Nouvelle generation. Auto : une generation toutes les N mesures (lecture en cours)");
    addCombo (P::genBars).setTooltip ("Longueur rendue de chaque generation (et intervalle en mode Auto), en mesures");
    addKnob (P::mutation).setTooltip ("Intensite des changements entre generations");
    addKnob (P::dna).setTooltip ("Part de la matiere d'origine conservee (evite la bouillie)");
    addKnob (P::death).setTooltip ("Probabilite qu'un fragment non like meure a la generation suivante");
    addKnob (P::maxGens).setTooltip ("Nombre de generations gardees avec leur audio (les plus vieilles fanent)");

    styleButton (genBtn, colour);
    genBtn.setColour (juce::TextButton::buttonColourId, colour.withAlpha (0.22f));
    genBtn.setTooltip ("La sortie du moteur est re-avalee : nouvelle generation (raccourci : G)");
    genBtn.onClick = [this] { ctx.ctl.newGeneration(); };
    addAndMakeVisible (genBtn);

    crackRng.setSeed (7);
    ctx.ctl.addChangeListener (this);
}

OuroPanel::~OuroPanel() { ctx.ctl.removeChangeListener (this); }

void OuroPanel::changeListenerCallback (juce::ChangeBroadcaster*) { repaint(); }

void OuroPanel::resized()
{
    AnimatedPanel::resized();
    ringArea = { 6, 20, getWidth() - 12, 172 };
    combos[0]->setBounds (8, 196, 78, 18);
    combos[1]->setBounds (90, 196, 46, 18);
    for (int i = 0; i < 4; ++i)
        knobs[i]->setBounds (8 + i * 46, 218, 44, 44);
    genBtn.setBounds (196, 200, getWidth() - 204, 54);
    treeArea = { 6, 268, getWidth() - 12, getHeight() - 274 };
}

void OuroPanel::tick()
{
    AnimatedPanel::tick();
    const float target = juce::jlimit (0.0f, 1.0f, ctx.proc.telemetry.wetLevel.load() * 1.5f);
    energy += (target - energy) * 0.25f;
    angle += 0.006f + energy * 0.05f + (ctx.ctl.generating ? 0.08f : 0.0f);
    if (ctx.ctl.generationCounter != lastCounter)
    {
        lastCounter = ctx.ctl.generationCounter;
        crackFlash = 1.0f;
    }
    crackFlash *= 0.93f;
    repaint (ringArea);
}

void OuroPanel::drawRing (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto c = area.getCentre();
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.38f;
    const float thick = 11.0f + energy * 9.0f;
    const float gap = 0.32f;

    auto ringPath = [&] (juce::Point<float> off)
    {
        juce::Path p;
        p.addCentredArc (c.x + off.x, c.y + off.y, radius, radius, 0.0f, angle + gap, angle + juce::MathConstants<float>::twoPi, true);
        return p;
    };
    const juce::PathStrokeType stroke (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // Écailles en fond (texture)
    g.setColour (Palette::orange.withAlpha (0.06f));
    for (int i = 0; i < 3; ++i)
        g.drawEllipse (juce::Rectangle<float> ((radius + 18 + i * 6) * 2, (radius + 18 + i * 6) * 2).withCentre (c), 1.0f);

    // Aberration chromatique : plus forte à chaque mutation
    const float shift = 1.5f + energy * 5.0f + crackFlash * 10.0f;
    g.setColour (Palette::red.withAlpha (0.5f));
    g.strokePath (ringPath ({ -shift, 0.0f }), stroke);
    g.setColour (Palette::blue.withAlpha (0.5f));
    g.strokePath (ringPath ({ shift, crackFlash * 3.0f }), stroke);
    g.setColour (Palette::orange.interpolatedWith (Palette::text, energy * 0.35f + crackFlash * 0.3f));
    g.strokePath (ringPath ({}), stroke);

    // Fissures : une par génération (positions déterministes)
    const int cracks = juce::jmin (40, ctx.ctl.generationCounter);
    juce::Random r (1234);
    for (int k = 0; k < cracks; ++k)
    {
        const float a = r.nextFloat() * juce::MathConstants<float>::twoPi + angle;
        juce::Path crack;
        float rr = radius - thick * 0.5f;
        float aa = a;
        crack.startNewSubPath (c.x + rr * std::sin (aa), c.y - rr * std::cos (aa));
        for (int s = 0; s < 4; ++s)
        {
            rr += thick / 4.0f;
            aa += (r.nextFloat() - 0.5f) * 0.08f;
            crack.lineTo (c.x + rr * std::sin (aa), c.y - rr * std::cos (aa));
        }
        const bool fresh = k == cracks - 1;
        g.setColour ((fresh ? Palette::text : Palette::background).withAlpha (fresh ? 0.6f + crackFlash * 0.4f : 0.85f));
        g.strokePath (crack, juce::PathStrokeType (fresh ? 2.0f : 1.3f));
    }

    // Tête du serpent
    const float ha = angle + juce::MathConstants<float>::twoPi;
    const juce::Point<float> head (c.x + radius * std::sin (ha), c.y - radius * std::cos (ha));
    const float hs = thick * 1.3f;
    g.setColour (Palette::text);
    g.fillEllipse (juce::Rectangle<float> (hs, hs).withCentre (head));
    g.setColour (Palette::red);
    g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre (head));

    // Numéro de génération
    int number = 0;
    for (auto& n : ctx.ctl.tree) if (n.id == ctx.ctl.currentNode) number = n.number;
    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (ctx.ctl.generating ? "MUTATION..." : "GEN", juce::Rectangle<float> (120, 14).withCentre (c.translated (0, -20)), juce::Justification::centred);
    g.setFont (juce::FontOptions (34.0f, juce::Font::bold));
    const auto numTxt = juce::String (number).paddedLeft ('0', 2);
    const auto nr = juce::Rectangle<float> (120, 38).withCentre (c.translated (0, 6));
    g.setColour (Palette::red.withAlpha (0.5f));
    g.drawText (numTxt, nr.translated (-2 - crackFlash * 4, 0), juce::Justification::centred);
    g.setColour (Palette::blue.withAlpha (0.5f));
    g.drawText (numTxt, nr.translated (2 + crackFlash * 4, 0), juce::Justification::centred);
    g.setColour (Palette::text);
    g.drawText (numTxt, nr, juce::Justification::centred);
}

void OuroPanel::drawTree (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (9.5f));
    g.drawText ("ARBRE DES GENERATIONS (clic = repartir de la)", area.reduced (4, 2), juce::Justification::topLeft);

    const auto& tree = ctx.ctl.tree;
    nodeHits.clear();
    if (tree.empty()) return;

    // Couloirs : une branche = une ligne
    std::map<int, int> lane, childCount;
    int maxLane = 0, maxNumber = 0;
    for (auto& n : tree)
    {
        if (n.parent < 0) { lane[n.id] = 0; continue; }
        const int pl = lane.count (n.parent) ? lane[n.parent] : 0;
        if (childCount[n.parent]++ == 0) lane[n.id] = pl;
        else lane[n.id] = ++maxLane;
        maxNumber = juce::jmax (maxNumber, n.number);
    }

    const float colW = 24.0f, rowH = 14.0f;
    auto inner = area.reduced (8, 0).withTrimmedTop (14).toFloat();
    int curNumber = 0;
    for (auto& n : tree) if (n.id == ctx.ctl.currentNode) curNumber = n.number;
    const float scroll = juce::jmax (0.0f, curNumber * colW - inner.getWidth() + 30.0f);
    const int maxRows = juce::jmax (1, (int) (inner.getHeight() / rowH));

    auto posOf = [&] (const GenNode& n)
    {
        return juce::Point<float> (inner.getX() + 8 + n.number * colW - scroll,
                                   inner.getY() + 7 + (lane[n.id] % maxRows) * rowH);
    };

    g.saveState();
    g.reduceClipRegion (area);
    for (auto& n : tree)
    {
        if (n.parent < 0) continue;
        for (auto& p : tree)
            if (p.id == n.parent)
            {
                const auto a = posOf (p), b = posOf (n);
                g.setColour (Palette::ouroboros.withAlpha (n.faded ? 0.25f : 0.7f));
                juce::Path path;
                path.startNewSubPath (a);
                path.lineTo (a.x + colW * 0.5f, b.y);
                path.lineTo (b);
                g.strokePath (path, juce::PathStrokeType (1.2f));
            }
    }
    for (auto& n : tree)
    {
        const auto p = posOf (n);
        const auto r = juce::Rectangle<float> (9, 9).withCentre (p);
        const bool cur = n.id == ctx.ctl.currentNode;
        g.setColour (n.faded ? Palette::panelEdge : (cur ? Palette::text : Palette::ouroboros));
        if (n.faded) g.drawEllipse (r, 1.0f);
        else g.fillEllipse (r);
        if (cur)
        {
            g.setColour (Palette::orange);
            g.drawEllipse (r.expanded (3.0f), 1.2f);
        }
        nodeHits.push_back ({ n.id, r.expanded (3.0f) });
    }
    g.restoreState();
}

void OuroPanel::paint (juce::Graphics& g)
{
    AnimatedPanel::paint (g);
    drawRing (g, ringArea.toFloat());
    drawTree (g, treeArea);
}

void OuroPanel::mouseDown (const juce::MouseEvent& e)
{
    for (auto& [id, r] : nodeHits)
        if (r.contains (e.position))
        {
            ctx.ctl.selectNode (id);
            repaint();
            return;
        }
}

//==============================================================================
// SÉQUENCEUR
//==============================================================================
StepGrid::StepGrid (UiContext& c) : ctx (c) {}

void StepGrid::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (r, 3.0f);

    const int len = juce::jlimit (1, kMaxSteps, (int) ctx.proc.apvts.getRawParameterValue ("seqLength")->load());
    const float w = r.getWidth() / len;
    const auto& pat = ctx.ctl.pattern;

    for (int i = 0; i < len; ++i)
    {
        const auto& s = pat.steps[(size_t) i];
        auto cell = juce::Rectangle<float> (r.getX() + i * w, r.getY(), w, r.getHeight()).reduced (w > 8 ? 1.0f : 0.3f, 2.0f);
        g.setColour ((i % 4 == 0 ? Palette::panelEdge.brighter (0.15f) : Palette::panelEdge).withAlpha (0.6f));
        g.fillRect (cell);

        float v = 0;
        switch (lane)
        {
            case 0: v = s.on ? 1.0f : 0.0f; break;
            case 1: v = s.prob; break;
            case 2: v = (s.ratchet - 1) / 3.0f; break;
            case 3: v = s.reverse; break;
            case 4: v = (s.pitch + 12.0f) / 24.0f; break;
            default: v = s.length; break;
        }
        const auto col = s.on ? Palette::sequencer : Palette::textDim.withAlpha (0.3f);
        if (lane == 4)
        {
            const float mid = cell.getCentreY();
            const float y = cell.getBottom() - v * cell.getHeight();
            g.setColour (col);
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (cell.getX(), juce::jmin (mid, y), cell.getRight(), juce::jmax (mid, y) + 1));
        }
        else
        {
            g.setColour (col);
            g.fillRect (cell.withTop (cell.getBottom() - v * cell.getHeight()));
        }
        if (i == playing)
        {
            g.setColour (Palette::text.withAlpha (0.5f));
            g.drawRect (cell, 1.5f);
        }
        if (s.ratchet > 1 && lane != 2 && w > 10)
        {
            g.setColour (Palette::text);
            g.setFont (juce::FontOptions (8.0f));
            g.drawText ("x" + juce::String (s.ratchet), cell.toNearestInt(), juce::Justification::centredTop);
        }
    }
}

void StepGrid::edit (const juce::MouseEvent& e, bool first)
{
    const int len = juce::jlimit (1, kMaxSteps, (int) ctx.proc.apvts.getRawParameterValue ("seqLength")->load());
    const int i = juce::jlimit (0, len - 1, (int) (e.position.x / getWidth() * len));
    const float v = juce::jlimit (0.0f, 1.0f, 1.0f - e.position.y / getHeight());
    auto& s = ctx.ctl.pattern.steps[(size_t) i];

    switch (lane)
    {
        case 0:
            if (first) paintOn = ! s.on;
            s.on = paintOn;
            break;
        case 1: s.prob = v; break;
        case 2: s.ratchet = 1 + juce::jlimit (0, 3, (int) (v * 4.0f)); break;
        case 3: s.reverse = v; break;
        case 4: s.pitch = std::round (v * 24.0f - 12.0f); break;
        default: s.length = juce::jmax (0.05f, v); break;
    }
    if (e.mods.isRightButtonDown())
        s = StepData();
    ctx.ctl.publishPattern();
    repaint();
}

void StepGrid::mouseDown (const juce::MouseEvent& e) { edit (e, true); }
void StepGrid::mouseDrag (const juce::MouseEvent& e) { edit (e, false); }

SequencerPanel::SequencerPanel (UiContext& c)
    : AnimatedPanel (c, "SEQUENCEUR", Palette::sequencer, (int) P::Module::Seq), grid (c)
{
    addToggle (P::seqRun, "LECTURE").setTooltip ("Lecture libre au tempo de l'hote, meme quand Ableton est a l'arret");
    addCombo (P::seqDivision);
    addCombo (P::midiMode).setTooltip ("Clavier = carte : la note choisit la zone (X), la velocite la hauteur (Y). Chromatique : la note transpose");
    addCombo (P::crossMode).setTooltip ("Type de croisement des fragments A et B");

    const std::pair<int, const char*> seqKnobs[] = {
        { P::seqLength, "Pas" }, { P::swing, "Swing" }, { P::ruleRandom, "Aleat." }, { P::ruleSimilar, "Ressemb." },
        { P::ruleContrast, "Contraste" }, { P::seqPitch, "Pitch" }, { P::seqLengthScale, "Long.frag" },
        { P::seqReverse, "Reverse" }, { P::engineLevel, "Niveau" }, { P::velocitySens, "Veloc." } };
    for (auto& [p, l] : seqKnobs)
        addKnob (p, l);
    knobs[2]->setTooltip ("Regle aleatoire : pioche ponderee par la zone peinte");
    knobs[3]->setTooltip ("Regle ressemblance : choisit un voisin proche du fragment precedent");
    knobs[4]->setTooltip ("Regle contraste : choisit un fragment eloigne du precedent");

    addAndMakeVisible (grid);
    const char* lanes[] = { "ON", "PROBA", "RATCHET", "REVERSE", "PITCH", "LONGUEUR" };
    for (int i = 0; i < 6; ++i)
    {
        auto* b = laneBtns.add (new juce::TextButton (lanes[i]));
        styleButton (*b, colour);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (88);
        b->onClick = [this, i] { grid.lane = i; grid.repaint(); };
        b->setTooltip ("Ligne editee dans la grille (clic/glisse = valeur, clic droit = remise a zero du pas)");
        addAndMakeVisible (b);
    }
    laneBtns[1]->setToggleState (true, juce::dontSendNotification);

    styleButton (crossBtn, colour);
    crossBtn.setTooltip ("Croise les fragments A et B choisis sur la carte (au hasard si rien n'est choisi). Le resultat devient un nouveau fragment.");
    crossBtn.onClick = [this] { ctx.ctl.crossSelected(); };
    addAndMakeVisible (crossBtn);

    styleButton (clearSteps, colour);
    clearSteps.setTooltip ("Remet tous les pas a zero");
    clearSteps.onClick = [this] { ctx.ctl.pattern = Pattern(); ctx.ctl.publishPattern(); grid.repaint(); };
    addAndMakeVisible (clearSteps);
}

void SequencerPanel::resized()
{
    AnimatedPanel::resized();
    toggles[0]->setBounds (8, 20, 88, 16);
    combos[0]->setBounds (8, 39, 88, 16);
    combos[1]->setBounds (8, 58, 88, 16);
    combos[2]->setBounds (8, 77, 88, 16);
    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (102 + i * 41, 18, 40, 42);
    crossBtn.setBounds (102, 66, 120, 26);
    const int gx = 520;
    for (int i = 0; i < 6; ++i)
        laneBtns[i]->setBounds (gx + i * 62, 3, 60, 14);
    clearSteps.setBounds (gx + 6 * 62, 3, 34, 14);
    grid.setBounds (gx, 20, getWidth() - gx - 6, getHeight() - 26);
}

void SequencerPanel::tick()
{
    AnimatedPanel::tick();
    const int step = ctx.proc.engine.currentStep.load();
    if (step != grid.playing)
    {
        grid.playing = step;
        grid.repaint();
    }
}
