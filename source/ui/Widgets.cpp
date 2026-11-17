#include "Widgets.h"

IDGemsLookAndFeel::IDGemsLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::background);
    setColour (juce::PopupMenu::backgroundColourId, Palette::panel);
    setColour (juce::PopupMenu::textColourId, Palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::orange.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, Palette::text);
    setColour (juce::PopupMenu::headerTextColourId, Palette::orange);
    setColour (juce::ComboBox::backgroundColourId, Palette::background);
    setColour (juce::ComboBox::textColourId, Palette::text);
    setColour (juce::ComboBox::outlineColourId, Palette::panelEdge);
    setColour (juce::ComboBox::arrowColourId, Palette::textDim);
    setColour (juce::TextButton::buttonColourId, Palette::background);
    setColour (juce::TextButton::textColourOffId, Palette::text);
    setColour (juce::TextButton::textColourOnId, Palette::background);
    setColour (juce::Slider::textBoxTextColourId, Palette::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Palette::text);
    setColour (juce::TextEditor::backgroundColourId, Palette::background);
    setColour (juce::TextEditor::textColourId, Palette::text);
    setColour (juce::TextEditor::outlineColourId, Palette::panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, Palette::orange);
    setColour (juce::ListBox::backgroundColourId, Palette::background);
    setColour (juce::TooltipWindow::backgroundColourId, Palette::panel.brighter (0.1f));
    setColour (juce::TooltipWindow::textColourId, Palette::text);
    setColour (juce::TooltipWindow::outlineColourId, Palette::orange);
    setColour (juce::ScrollBar::thumbColourId, Palette::panelEdge.brighter (0.3f));
    setColour (juce::AlertWindow::backgroundColourId, Palette::panel);
    setColour (juce::AlertWindow::textColourId, Palette::text);
    setColour (juce::AlertWindow::outlineColourId, Palette::orange);
}

void IDGemsLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (size, size);
    const auto c = r.getCentre();
    const float rad = size * 0.5f - 2.0f;
    const float thick = juce::jmax (2.0f, size * 0.09f);

    juce::Path track;
    track.addCentredArc (c.x, c.y, rad, rad, 0.0f, start, end, true);
    g.setColour (Palette::panelEdge);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float angle = start + pos * (end - start);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;
    juce::Path val;
    val.addCentredArc (c.x, c.y, rad, rad, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    // léger décalage chromatique
    g.setColour (Palette::blue.withAlpha (0.35f));
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::translation (0.8f, 0.0f));
    g.setColour (accent);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // index
    const juce::Point<float> tip (c.x + (rad - thick * 1.6f) * std::sin (angle), c.y - (rad - thick * 1.6f) * std::cos (angle));
    g.setColour (Palette::text);
    g.drawLine ({ c, tip }, juce::jmax (1.2f, thick * 0.55f));
}

void IDGemsLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
    g.setColour (on ? accent : bg.brighter (over ? 0.12f : 0.0f).brighter (down ? 0.1f : 0.0f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (on ? accent.brighter (0.3f) : (over ? accent : Palette::panelEdge));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void IDGemsLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? Palette::background : b.findColour (juce::TextButton::textColourOffId));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (2, 0), juce::Justification::centred, 1, 0.7f);
}

juce::Font IDGemsLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::FontOptions (juce::jmin (13.0f, h * 0.55f), juce::Font::bold);
}

void IDGemsLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.isMouseOver() ? Palette::orange : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 9.0f, ay = h * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 2.5f);
    g.setColour (Palette::textDim);
    g.fillPath (arrow);
}

juce::Font IDGemsLookAndFeel::getComboBoxFont (juce::ComboBox& b)
{
    return juce::FontOptions (juce::jmin (12.0f, b.getHeight() * 0.62f));
}

void IDGemsLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (2, 0, box.getWidth() - 14, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

juce::Font IDGemsLookAndFeel::getPopupMenuFont()
{
    return juce::FontOptions (14.0f);
}

juce::Label* IDGemsLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::FontOptions (10.0f));
    l->setJustificationType (juce::Justification::centred);
    return l;
}

//==============================================================================
void styleButton (juce::TextButton& b, juce::Colour accent, bool toggle)
{
    b.setColour (juce::TextButton::buttonOnColourId, accent);
    b.setColour (juce::TextButton::buttonColourId, Palette::background);
    b.setClickingTogglesState (toggle);
}

//==============================================================================
Knob::Knob (UiContext& c, int p, juce::Colour a, const juce::String& l)
    : param (p), ctx (c), accent (a), label (l.isNotEmpty() ? l : juce::String (P::info (p).name))
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setDoubleClickReturnValue (true, P::info (p).def);
    slider.setMouseDragSensitivity (180);
    slider.onDragStart = [this] { ctx.ctl.beginGesture(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ctx.proc.apvts, P::info (p).id, slider);
    slider.addMouseListener (this, false);
    setTooltip (juce::String (P::info (p).name) + "\nClic droit : cadenas, plage random, modulation, MIDI learn");
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (11);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (9.5f));
    g.drawFittedText (label, getLocalBounds().removeFromBottom (11), juce::Justification::centred, 1, 0.6f);
}

void Knob::paintOverChildren (juce::Graphics& g)
{
    auto r = slider.getBounds().toFloat();
    const float size = juce::jmin (r.getWidth(), r.getHeight()) - 4.0f;
    auto sq = r.withSizeKeepingCentre (size, size);

    if (hasMod && shownMod >= 0.0f)
    {
        // Point de la valeur modulée en temps réel
        const auto& rp = slider.getRotaryParameters();
        const float a = rp.startAngleRadians + shownMod * (rp.endAngleRadians - rp.startAngleRadians);
        const auto c = sq.getCentre();
        const float rad = size * 0.5f + 1.5f;
        g.setColour (Palette::modulation);
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ c.x + rad * std::sin (a), c.y - rad * std::cos (a) }));
    }

    if (ctx.ctl.locks[(size_t) param])
    {
        g.setColour (Palette::orange);
        auto lr = juce::Rectangle<float> (sq.getRight() - 6.0f, sq.getY() - 1.0f, 7.0f, 6.0f);
        g.fillRoundedRectangle (lr.withTrimmedTop (2.0f), 1.0f);
        g.drawEllipse (lr.withHeight (5.0f).translated (0.0f, -2.5f).reduced (1.2f, 0.0f), 1.0f);
    }
    if (ctx.ctl.midiLearnCcFor (param) >= 0)
    {
        g.setColour (Palette::blue);
        g.setFont (juce::FontOptions (8.0f));
        g.drawText ("CC" + juce::String (ctx.ctl.midiLearnCcFor (param)), sq.withHeight (9.0f).translated (0, -3).toNearestInt(),
                    juce::Justification::topLeft);
    }
}

void Knob::refresh()
{
    hasMod = ! ctx.ctl.routesFor (param).empty() || (ctx.proc.apvts.getRawParameterValue ("chaos")->load() > 0.0f);
    if (! hasMod)
    {
        if (shownMod >= 0.0f) { shownMod = -1.0f; repaint(); }
        return;
    }
    const float v = P::info (param).toNorm (ctx.proc.modulatedDisplay[(size_t) param].load());
    if (std::abs (v - shownMod) > 0.004f)
    {
        shownMod = v;
        repaint();
    }
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showMenu();
}

void Knob::showMenu()
{
    auto& ctl = ctx.ctl;
    juce::PopupMenu m;
    m.addSectionHeader (P::info (param).name);
    m.addItem (1, "Cadenas (exclu du random)", true, ctl.locks[(size_t) param]);

    juce::PopupMenu range;
    range.addItem (10, "Plage complete");
    range.addItem (11, "+/- 10 % autour de la valeur");
    range.addItem (12, "+/- 25 % autour de la valeur");
    range.addItem (13, "+/- 50 % autour de la valeur");
    range.addItem (14, "Moitie basse");
    range.addItem (15, "Moitie haute");
    m.addSubMenu ("Plage de random", range);

    if (P::info (param).isContinuous())
    {
        juce::PopupMenu modMenu;
        for (int s = 0; s < numModSources; ++s)
        {
            juce::PopupMenu depth;
            for (int d : { 100, 50, 25, -25, -50, -100 })
                depth.addItem (1000 + s * 20 + (d + 100) / 25, (d > 0 ? "+" : "") + juce::String (d) + " %");
            modMenu.addSubMenu (modSourceName (s), depth);
        }
        m.addSubMenu ("Moduler par", modMenu);
        const auto routes = ctl.routesFor (param);
        if (! routes.empty())
        {
            juce::String txt = "Retirer les modulations (";
            for (auto& r : routes) txt << modSourceName (r.source) << " ";
            m.addItem (20, txt.trimEnd() + ")");
        }
    }

    m.addSeparator();
    m.addItem (30, "MIDI Learn");
    if (ctl.midiLearnCcFor (param) >= 0)
        m.addItem (31, "Oublier le CC " + juce::String (ctl.midiLearnCcFor (param)));
    m.addItem (40, "Valeur par defaut");

    juce::Component::SafePointer<Knob> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& self = *safe;
        auto& c = self.ctx.ctl;
        const int p = self.param;
        const float cur = P::info (p).toNorm ((float) self.slider.getValue());
        if (r == 1) c.locks[(size_t) p] = ! c.locks[(size_t) p];
        else if (r == 10) c.setRange (p, 0, 1);
        else if (r >= 11 && r <= 13) { const float w = r == 11 ? 0.1f : (r == 12 ? 0.25f : 0.5f); c.setRange (p, cur - w, cur + w); }
        else if (r == 14) c.setRange (p, 0.0f, 0.5f);
        else if (r == 15) c.setRange (p, 0.5f, 1.0f);
        else if (r == 20) c.removeRoutes (p);
        else if (r == 30) c.startMidiLearn (p);
        else if (r == 31) c.clearMidiLearn (p);
        else if (r == 40) self.slider.setValue (P::info (p).def, juce::sendNotificationSync);
        else if (r >= 1000)
        {
            const int s = (r - 1000) / 20;
            const int d = ((r - 1000) % 20) * 25 - 100;
            c.addRoute (s, p, d / 100.0f);
        }
        self.repaint();
    });
}

//==============================================================================
ParamToggle::ParamToggle (UiContext& c, int p, const juce::String& text, juce::Colour accent)
    : juce::TextButton (text)
{
    styleButton (*this, accent, true);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (c.proc.apvts, P::info (p).id, *this);
    setTooltip (P::info (p).name);
}

ParamCombo::ParamCombo (UiContext& c, int p)
{
    addItemList (juce::StringArray::fromTokens (P::info (p).choices, "|", ""), 1);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (c.proc.apvts, P::info (p).id, *this);
    setTooltip (P::info (p).name);
}

//==============================================================================
ModulePanel::ModulePanel (UiContext& c, const juce::String& t, juce::Colour col, int m)
    : ctx (c), title (t), colour (col), module (m)
{
    if (module >= 0)
    {
        styleButton (dice, colour);
        dice.setTooltip ("Random de ce module (dose par le dosage global, cadenas respectes)");
        dice.onClick = [this] { ctx.ctl.randomizeModule ((P::Module) module, ctx.ctl.nextSeed()); };
        addAndMakeVisible (dice);

        styleButton (lock, colour, true);
        lock.setTooltip ("Cadenas du module : exclu du random et des mutations");
        lock.setToggleState (ctx.ctl.moduleLocks[(size_t) module], juce::dontSendNotification);
        lock.onClick = [this] { ctx.ctl.moduleLocks[(size_t) module] = lock.getToggleState(); };
        addAndMakeVisible (lock);
    }
}

void ModulePanel::resized()
{
    if (module >= 0)
    {
        lock.setBounds (getWidth() - 40, 3, 34, 14);
        dice.setBounds (getWidth() - 70, 3, 26, 14);
    }
}

void ModulePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Palette::panel.withAlpha (0.92f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

    g.setColour (colour);
    g.fillRect (r.removeFromTop (2.0f).reduced (4.0f, 0.0f));

    // Titre avec aberration chromatique
    auto tr = getLocalBounds().reduced (8, 3).removeFromTop (15);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (Palette::red.withAlpha (0.45f));
    g.drawText (title, tr.translated (-1, 0), juce::Justification::centredLeft);
    g.setColour (Palette::blue.withAlpha (0.45f));
    g.drawText (title, tr.translated (1, 0), juce::Justification::centredLeft);
    g.setColour (colour);
    g.drawText (title, tr, juce::Justification::centredLeft);
}
