#include "PluginProcessor.h"
#include "PluginEditor.h"

OuroborosProcessor::OuroborosProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Entree",    juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Sortie",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "OUROBOROS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout OuroborosProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::outGain, 1 }, "Gain de sortie",
        juce::NormalisableRange<float> (-60.0f, 12.0f, 0.01f, 2.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

bool OuroborosProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::stereo() && mainOut != juce::AudioChannelSet::mono())
        return false;
    if (mainIn != mainOut)
        return false;

    // Sidechain : désactivé, mono ou stéréo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void OuroborosProcessor::prepareToPlay (double sampleRate, int)
{
    outGainSmoothed.reset (sampleRate, 0.03);
    outGainSmoothed.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue (ParamIDs::outGain)->load(), -60.0f));
}

static float peakOf (const juce::AudioBuffer<float>& b, int firstChan, int numChans, int numSamples)
{
    float p = 0.0f;
    for (int c = 0; c < numChans; ++c)
        p = juce::jmax (p, b.getMagnitude (firstChan + c, 0, numSamples));
    return p;
}

/** Retombée douce des mètres : on garde le max entre la nouvelle crête et l'ancienne qui décroît. */
static void publishPeak (std::atomic<float>& target, float peak)
{
    const float prev = target.load (std::memory_order_relaxed);
    target.store (juce::jmax (peak, prev * 0.85f), std::memory_order_relaxed);
}

void OuroborosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // --- Tempo de l'hôte -------------------------------------------------------------------
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                telemetry.bpm.store (*bpm, std::memory_order_relaxed);

    // --- Bus ------------------------------------------------------------------------------
    auto mainIn  = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);

    const bool hasSidechain = getBusCount (true) > 1 && getBus (true, 1)->isEnabled()
                              && getBus (true, 1)->getNumberOfChannels() > 0;
    telemetry.sidechainActive.store (hasSidechain, std::memory_order_relaxed);

    publishPeak (telemetry.inLevel, peakOf (mainIn, 0, mainIn.getNumChannels(), numSamples));

    if (hasSidechain)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        publishPeak (telemetry.scLevel, peakOf (sc, 0, sc.getNumChannels(), numSamples));
    }
    else
    {
        publishPeak (telemetry.scLevel, 0.0f);
    }

    // --- MIDI -----------------------------------------------------------------------------
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            telemetry.lastNote.store (msg.getNoteNumber(), std::memory_order_relaxed);
            telemetry.lastVelocity.store (msg.getFloatVelocity(), std::memory_order_relaxed);
            telemetry.noteCounter.fetch_add (1, std::memory_order_relaxed);
        }
    }
    midi.clear(); // on ne renvoie pas de MIDI

    // --- Phase 1 : passage direct de l'audio + gain de sortie ------------------------------
    // (les canaux de sortie au-delà du bus principal sont mis à zéro)
    for (int c = mainOut.getNumChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    outGainSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue (ParamIDs::outGain)->load(), -60.0f));

    if (outGainSmoothed.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = outGainSmoothed.getNextValue();
            for (int c = 0; c < mainOut.getNumChannels(); ++c)
                mainOut.getWritePointer (c)[i] *= g;
        }
    }
    else
    {
        mainOut.applyGain (outGainSmoothed.getTargetValue());
    }

    publishPeak (telemetry.outLevelL, mainOut.getNumChannels() > 0 ? mainOut.getMagnitude (0, 0, numSamples) : 0.0f);
    publishPeak (telemetry.outLevelR, mainOut.getNumChannels() > 1 ? mainOut.getMagnitude (1, 0, numSamples)
                                                                   : telemetry.outLevelL.load());
}

juce::AudioProcessorEditor* OuroborosProcessor::createEditor()
{
    return new OuroborosEditor (*this);
}

void OuroborosProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("editorWidth", editorWidth.load(), nullptr);
    state.setProperty ("version", JucePlugin_VersionString, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OuroborosProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            editorWidth.store ((int) tree.getProperty ("editorWidth", 0));
            apvts.replaceState (tree);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OuroborosProcessor();
}
