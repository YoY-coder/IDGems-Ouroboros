#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "control/Controller.h"

OuroborosProcessor::OuroborosProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Entree",    juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Sortie",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undoManager, "OUROBOROS", P::createLayout())
{
    for (int i = 0; i < P::count; ++i)
    {
        params[(size_t) i] = apvts.getParameter (P::info (i).id);
        raw[(size_t) i] = apvts.getRawParameterValue (P::info (i).id);
        modulatedDisplay[(size_t) i].store (P::info (i).def);
    }
    for (auto& c : ccMap)
        c.store (-1);

    base = P::defaults();
    engine.setTrash (&trash);
    controller = std::make_unique<Controller> (*this);
}

OuroborosProcessor::~OuroborosProcessor()
{
    controller.reset();
}

std::shared_ptr<CaptureRing> OuroborosProcessor::getRing() const
{
    const juce::SpinLock::ScopedLockType sl (ringLock);
    return ringShared;
}

void OuroborosProcessor::setRing (std::shared_ptr<CaptureRing> r)
{
    {
        const juce::SpinLock::ScopedLockType sl (ringLock);
        ringShared = r;
    }
    ringQueue.push (std::move (r));
}

bool OuroborosProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::stereo() && mainOut != juce::AudioChannelSet::mono())
        return false;
    if (mainIn != mainOut)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void OuroborosProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    telemetry.sampleRate.store (sampleRate);
    const int block = juce::jmax (512, samplesPerBlock);
    engine.prepare (sampleRate, block);
    output.prepare (sampleRate, block);
    wet.setSize (2, block);
    dry.setSize (2, block);

    // Le buffer de capture dépend du taux d'échantillonnage : on le (re)crée ici,
    // l'audio ne tourne pas pendant prepareToPlay.
    const int frames = (int) (sampleRate * juce::jlimit (5.0f, 120.0f, raw[P::captureSeconds]->load()));
    if (ringAudio == nullptr || ringAudio->getSampleRate() != sampleRate || ringAudio->getCapacity() != frames)
    {
        auto r = std::make_shared<CaptureRing> (budget, frames, sampleRate);
        {
            const juce::SpinLock::ScopedLockType sl (ringLock);
            ringShared = r;
        }
        ringAudio = r;
    }
}

static float peakOf (const juce::AudioBuffer<float>& b, int numSamples)
{
    float p = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        p = juce::jmax (p, b.getMagnitude (c, 0, numSamples));
    return p;
}

static void publishPeak (std::atomic<float>& target, float peak)
{
    const float prev = target.load (std::memory_order_relaxed);
    target.store (juce::jmax (peak, prev * 0.85f), std::memory_order_relaxed);
}

void OuroborosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples > wet.getNumSamples())
    {
        // L'hôte envoie un bloc plus grand qu'annoncé (rare) : on agrandit.
        wet.setSize (2, numSamples, false, false, true);
        dry.setSize (2, numSamples, false, false, true);
    }

    // --- Nouvelles données publiées par l'UI --------------------------------------------
    libraryBox.fetch (engine.library, trash);
    patternBox.fetch (engine.pattern, trash);
    modBox.fetch (engine.mods, trash);
    {
        std::shared_ptr<CaptureRing> r;
        while (ringQueue.pop (r))
        {
            trash.dispose (std::move (ringAudio));
            ringAudio = std::move (r);
        }
    }

    // --- Paramètres ---------------------------------------------------------------------
    for (int i = 0; i < P::count; ++i)
        base[(size_t) i] = raw[(size_t) i]->load (std::memory_order_relaxed);

    // --- Commandes UI ---------------------------------------------------------------------
    AudioCommand cmd;
    while (commands.pop (cmd))
    {
        if (cmd.type == AudioCommand::Audition)       engine.audition (cmd.value);
        else if (cmd.type == AudioCommand::Panic)     { engine.panic(); output.reset(); }
        else if (cmd.type == AudioCommand::ResetSequencer) engine.reset();
    }

    // --- Transport ----------------------------------------------------------------------
    Transport transport;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                transport.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition())
                transport.ppq = *ppq;
            transport.playing = pos->getIsPlaying();
            if (auto sig = pos->getTimeSignature())
                telemetry.beatsPerBar.store (juce::jmax (1, sig->numerator * 4 / juce::jmax (1, sig->denominator)));
        }
    }
    telemetry.bpm.store (transport.bpm, std::memory_order_relaxed);

    // --- Bus ----------------------------------------------------------------------------------
    auto mainIn = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);
    const bool hasSidechain = getBusCount (true) > 1 && getBus (true, 1)->isEnabled()
                              && getBus (true, 1)->getNumberOfChannels() > 0;
    telemetry.sidechainActive.store (hasSidechain, std::memory_order_relaxed);

    const int inCh = mainIn.getNumChannels();
    for (int c = 0; c < 2; ++c)
    {
        if (inCh > 0)
            dry.copyFrom (c, 0, mainIn, juce::jmin (c, inCh - 1), 0, numSamples);
        else
            dry.clear (c, 0, numSamples);
    }
    publishPeak (telemetry.inLevel, peakOf (mainIn, numSamples));

    // --- Capture (entrée ou sidechain) dans le buffer circulaire ---------------------------
    const float* capL = dry.getReadPointer (0);
    const float* capR = dry.getReadPointer (1);
    if (hasSidechain)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        publishPeak (telemetry.scLevel, peakOf (sc, numSamples));
        if (base[P::captureSource] > 0.5f && sc.getNumChannels() > 0)
        {
            capL = sc.getReadPointer (0);
            capR = sc.getReadPointer (juce::jmin (1, sc.getNumChannels() - 1));
        }
    }
    else
        publishPeak (telemetry.scLevel, 0.0f);

    if (ringAudio != nullptr)
        ringAudio->write (capL, capR, numSamples);

    // --- MIDI : learn + CC + notes ---------------------------------------------------------------
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            telemetry.lastNote.store (msg.getNoteNumber(), std::memory_order_relaxed);
            telemetry.lastVelocity.store (msg.getFloatVelocity(), std::memory_order_relaxed);
            telemetry.noteCounter.fetch_add (1, std::memory_order_relaxed);
        }
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            const int learn = midiLearnTarget.load();
            if (learn >= 0)
            {
                for (auto& c : ccMap)
                    if (c.load() == learn) c.store (-1);
                ccMap[(size_t) cc].store (learn);
                midiLearnTarget.store (-1);
            }
            const int target = ccMap[(size_t) cc].load();
            if (target >= 0 && target < P::count)
                params[(size_t) target]->setValueNotifyingHost (msg.getControllerValue() / 127.0f);
        }
    }

    // --- Moteur (wet) -----------------------------------------------------------------------
    engine.process (wet.getWritePointer (0), wet.getWritePointer (1), numSamples, base, transport, &midi);
    midi.clear();

    const auto& v = engine.getModulatedValues();
    publishPeak (telemetry.wetLevel, juce::jmax (wet.getMagnitude (0, 0, numSamples), wet.getMagnitude (1, 0, numSamples)));
    telemetry.playing.store (transport.playing || base[P::seqRun] > 0.5f, std::memory_order_relaxed);
    telemetry.ppq.store (transport.ppq, std::memory_order_relaxed);

    // --- Mélange dry/wet puis chaîne de sortie --------------------------------------------------
    const float mix = juce::jlimit (0.0f, 1.0f, v[P::dryWet]);
    for (int c = 0; c < 2; ++c)
    {
        auto* w = wet.getWritePointer (c);
        const auto* d = dry.getReadPointer (c);
        for (int i = 0; i < numSamples; ++i)
            w[i] = d[i] * (1.0f - mix) + w[i] * mix;
    }
    output.process (wet.getWritePointer (0), wet.getWritePointer (1), numSamples, v);

    for (int c = mainOut.getNumChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, numSamples);
    if (mainOut.getNumChannels() == 1)
    {
        mainOut.copyFrom (0, 0, wet, 0, 0, numSamples);
        mainOut.addFrom (0, 0, wet, 1, 0, numSamples);
        mainOut.applyGain (0.5f);
    }
    else
    {
        for (int c = 0; c < juce::jmin (2, mainOut.getNumChannels()); ++c)
            mainOut.copyFrom (c, 0, wet, c, 0, numSamples);
    }

    publishPeak (telemetry.outLevelL, wet.getMagnitude (0, 0, numSamples));
    publishPeak (telemetry.outLevelR, wet.getMagnitude (1, 0, numSamples));

    for (int i = 0; i < P::count; ++i)
        modulatedDisplay[(size_t) i].store (v[(size_t) i], std::memory_order_relaxed);
}

juce::AudioProcessorEditor* OuroborosProcessor::createEditor()
{
    return new OuroborosEditor (*this);
}

void OuroborosProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("OUROBOROS_STATE");
    root.setProperty ("version", JucePlugin_VersionString, nullptr);
    root.setProperty ("editorWidth", editorWidth.load(), nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    juce::ValueTree midiMap ("MIDIMAP");
    for (int cc = 0; cc < 128; ++cc)
        if (ccMap[(size_t) cc].load() >= 0)
            midiMap.setProperty ("cc" + juce::String (cc), P::info (ccMap[(size_t) cc].load()).id, nullptr);
    root.appendChild (midiMap, nullptr);

    // Données de l'UI (sources, pattern, modulations, fragments likés…)
    auto extra = controller->getExtraStateForSaving();
    if (extra.isValid())
        root.appendChild (extra, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void OuroborosProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml (*xml);
    if (root.hasType (apvts.state.getType()))
    {
        // Ancien format (phase 1)
        editorWidth.store ((int) root.getProperty ("editorWidth", 0));
        apvts.replaceState (root);
        return;
    }
    if (! root.hasType ("OUROBOROS_STATE"))
        return;

    editorWidth.store ((int) root.getProperty ("editorWidth", 0));
    auto state = root.getChildWithName (apvts.state.getType());
    if (state.isValid())
        apvts.replaceState (state);

    auto midiMap = root.getChildWithName ("MIDIMAP");
    for (auto& c : ccMap)
        c.store (-1);
    for (int i = 0; i < midiMap.getNumProperties(); ++i)
    {
        const auto name = midiMap.getPropertyName (i).toString();
        const int cc = name.substring (2).getIntValue();
        const int p = P::indexOf (midiMap.getProperty (name).toString());
        if (cc >= 0 && cc < 128 && p >= 0)
            ccMap[(size_t) cc].store (p);
    }

    auto extra = root.getChildWithName ("EXTRA").createCopy();
    if (extra.isValid())
        controller->restoreExtraStateAsync (extra);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OuroborosProcessor();
}
