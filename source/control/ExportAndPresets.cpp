#include "Controller.h"
#include "PluginProcessor.h"
#include "engine/CrossRenderer.h"

//==============================================================================
// Dossiers
//==============================================================================
static juce::File defaultRoot()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("IDGems Ouroboros");
}

juce::File Controller::getExportFolder() const
{
    const auto p = settings->getValue ("exportFolder");
    return p.isNotEmpty() ? juce::File (p) : defaultRoot().getChildFile ("Exports");
}

void Controller::setExportFolder (const juce::File& f)
{
    settings->setValue ("exportFolder", f.getFullPathName());
    settings->saveIfNeeded();
}

juce::File Controller::getIdgemsFolder() const
{
    const auto p = settings->getValue ("idgemsFolder");
    return p.isNotEmpty() ? juce::File (p)
                          : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("IDGems");
}

void Controller::setIdgemsFolder (const juce::File& f)
{
    settings->setValue ("idgemsFolder", f.getFullPathName());
    settings->saveIfNeeded();
}

//==============================================================================
// Écriture
//==============================================================================
void Controller::writeWav (const juce::File& file, const juce::AudioBuffer<float>& buf, double sr)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}
                                               .withSampleRate (sr)
                                               .withNumChannels (2)
                                               .withBitsPerSample (24));
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

void Controller::writeJson (const juce::File& file, const juce::var& v)
{
    file.replaceWithText (juce::JSON::toString (v));
}

/** Métadonnées "fragment" : format prévu pour le futur pont avec IDGems Synth. */
static juce::var fragmentMeta (const Fragment& f, double sr, double bpm, uint32_t seed, const juce::String& kind)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "idgems-fragment-v1");
    o->setProperty ("plugin", "IDGems Ouroboros");
    o->setProperty ("version", JucePlugin_VersionString);
    o->setProperty ("kind", kind);
    o->setProperty ("sampleRate", sr);
    o->setProperty ("lengthSamples", f.length);
    o->setProperty ("bpm", bpm);
    o->setProperty ("generation", f.generation);
    o->setProperty ("sourceSlot", f.slot);
    o->setProperty ("seed", (int) seed);
    o->setProperty ("liked", f.liked);
    auto* d = new juce::DynamicObject();
    d->setProperty ("brightnessHz", f.desc.centroidHz);
    d->setProperty ("noise", f.desc.flatness);
    d->setProperty ("pitchMidi", f.desc.pitchMidi);
    d->setProperty ("rmsDb", f.desc.rmsDb);
    d->setProperty ("peakDb", f.desc.peakDb);
    d->setProperty ("lengthSec", f.desc.lengthSec);
    o->setProperty ("descriptors", juce::var (d));
    return juce::var (o);
}

static Fragment wholeBufferFragment (int numFrames, int generation)
{
    Fragment f;
    f.length = numFrames;
    f.generation = generation;
    return f;
}

//==============================================================================
// Exports (uniquement sur action de l'utilisateur)
//==============================================================================
void Controller::exportGeneration()
{
    const GenNode* node = nullptr;
    for (auto& n : tree) if (n.id == currentNode) node = &n;
    if (node == nullptr || node->audio == nullptr)
    {
        setStatus ("Pas de generation a exporter (lance une generation d'abord)");
        return;
    }
    const auto audio = node->audio;
    const int number = node->number;
    const uint32_t seed = node->seed;
    const auto folder = getExportFolder();
    const auto tag = bpmTag();
    const double bpm = proc.telemetry.bpm.load();

    runJob ([this, audio, number, seed, folder, tag, bpm]
    {
        juce::AudioBuffer<float> buf (2, audio->getNumFrames());
        audio->copyTo (buf, 0, audio->getNumFrames());
        const auto name = "Ouroboros_Gen" + juce::String (number).paddedLeft ('0', 2) + "_" + tag;
        auto file = folder.getNonexistentChildFile (name, ".wav", false);
        writeWav (file, buf, audio->getSampleRate());
        writeJson (file.withFileExtension ("json"),
                   fragmentMeta (wholeBufferFragment (buf.getNumSamples(), number), audio->getSampleRate(), bpm, seed, "loop"));
        onMessageThread ([this, file] { setStatus ("Exporte : " + file.getFileName()); });
    });
}

void Controller::exportLiked()
{
    if (library == nullptr)
        return;
    std::vector<FragRec> liked;
    for (const auto& f : library->frags)
        if (f.liked)
            liked.push_back ({ library->buffers[(size_t) f.buffer], f });
    if (liked.empty())
    {
        setStatus ("Aucun fragment like (outil coeur sur la carte)");
        return;
    }
    const auto folder = getExportFolder().getChildFile ("One-shots");
    const auto tag = bpmTag();
    const double bpm = proc.telemetry.bpm.load();

    runJob ([this, liked, folder, tag, bpm]
    {
        int count = 0;
        for (const auto& r : liked)
        {
            juce::AudioBuffer<float> buf (2, r.f.length);
            r.buf->copyTo (buf, r.f.start, r.f.length);
            const auto name = "Ouroboros_Frag" + juce::String ((int) r.f.id) + "_G" + juce::String (r.f.generation) + "_" + tag;
            auto file = folder.getNonexistentChildFile (name, ".wav", false);
            writeWav (file, buf, r.buf->getSampleRate());
            writeJson (file.withFileExtension ("json"), fragmentMeta (r.f, r.buf->getSampleRate(), bpm, 0, "one-shot"));
            ++count;
        }
        onMessageThread ([this, count, folder]
        {
            setStatus (juce::String (count) + " one-shots exportes dans " + folder.getFullPathName());
        });
    });
}

void Controller::exportLoop()
{
    static const int barsChoices[] = { 1, 2, 4, 8, 16 };
    const auto values = currentValues();
    const int bars = barsChoices[juce::jlimit (0, 4, (int) values[P::genBars])];
    const auto pat = pattern;
    const auto lib = library;
    const auto modsCopy = mods;
    const uint32_t seed = lastSeed != 0 ? lastSeed : nextSeed();
    const auto folder = getExportFolder().getChildFile ("Loops");
    const auto tag = bpmTag();
    const double bpm = proc.telemetry.bpm.load();
    const double sr = proc.telemetry.sampleRate.load();

    runJob ([this, values, pat, lib, modsCopy, bars, seed, folder, tag, bpm, sr]
    {
        auto buf = renderOffline (values, pat, bars, seed, lib, modsCopy);
        const auto name = "Ouroboros_Loop_" + tag + "_" + juce::String (bars) + "mes_seed" + juce::String ((int) seed);
        auto file = folder.getNonexistentChildFile (name, ".wav", false);
        writeWav (file, buf, sr);
        writeJson (file.withFileExtension ("json"), fragmentMeta (wholeBufferFragment (buf.getNumSamples(), 0), sr, bpm, seed, "loop"));
        onMessageThread ([this, file] { setStatus ("Loop exportee : " + file.getFileName()); });
    });
}

void Controller::exportBatch (int count)
{
    static const int barsChoices[] = { 1, 2, 4, 8, 16 };
    count = juce::jlimit (1, 64, count);
    const auto base = currentValues();
    const int bars = barsChoices[juce::jlimit (0, 4, (int) base[P::genBars])];
    const float amount = base[P::randomAmount];
    const auto lib = library;
    const auto modsCopy = mods;
    const auto folder = getExportFolder().getChildFile ("Batch " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"));
    const auto tag = bpmTag();
    const double bpm = proc.telemetry.bpm.load();
    const double sr = proc.telemetry.sampleRate.load();

    // Variations préparées ici (cadenas et plages respectés)
    struct Variation { P::Values values; Pattern pattern; uint32_t seed; };
    std::vector<Variation> vars;
    for (int k = 0; k < count; ++k)
    {
        Variation v { base, pattern, nextSeed() };
        juce::Random rng ((juce::int64) v.seed);
        for (int i = 0; i < P::count; ++i)
        {
            const auto& inf = P::info (i);
            if (! inf.randomizable || locks[(size_t) i] || moduleLocks[(size_t) inf.module] || inf.module == P::Module::Ouro)
                continue;
            const auto [lo, hi] = ranges[(size_t) i];
            const float norm = inf.toNorm (v.values[(size_t) i]);
            const float r = lo + rng.nextFloat() * (hi - lo);
            v.values[(size_t) i] = inf.fromNorm (norm + (r - norm) * amount);
            if (inf.kind != P::Kind::F) v.values[(size_t) i] = std::round (v.values[(size_t) i]);
        }
        for (auto& s : v.pattern.steps)
            if (rng.nextFloat() < amount)
            {
                s.prob = 0.4f + rng.nextFloat() * 0.6f;
                s.reverse = rng.nextFloat() < 0.2f ? rng.nextFloat() : 0.0f;
                s.pitch = rng.nextFloat() < 0.3f ? (float) (rng.nextInt (25) - 12) : 0.0f;
            }
        vars.push_back (v);
    }

    setStatus ("Batch : rendu de " + juce::String (count) + " variations...");
    runJob ([this, vars, lib, modsCopy, bars, folder, tag, bpm, sr]
    {
        int i = 0;
        for (const auto& v : vars)
        {
            auto buf = renderOffline (v.values, v.pattern, bars, v.seed, lib, modsCopy);
            const auto name = "Ouroboros_Batch" + juce::String (++i).paddedLeft ('0', 2) + "_" + tag + "_seed" + juce::String ((int) v.seed);
            auto file = folder.getChildFile (name + ".wav");
            writeWav (file, buf, sr);
            writeJson (file.withFileExtension ("json"), fragmentMeta (wholeBufferFragment (buf.getNumSamples(), 0), sr, bpm, v.seed, "loop"));
        }
        onMessageThread ([this, folder, i] { setStatus (juce::String (i) + " variations exportees dans " + folder.getFileName()); });
    });
}

juce::File Controller::writeFragmentForDrag (uint32_t fragId)
{
    if (library == nullptr)
        return {};
    for (const auto& f : library->frags)
    {
        if (f.id != fragId)
            continue;
        const auto& b = library->buffers[(size_t) f.buffer];
        juce::AudioBuffer<float> buf (2, f.length);
        b->copyTo (buf, f.start, f.length);
        // Le fichier glissé est un export voulu : il reste dans le dossier d'export (Ableton le référence)
        auto file = getExportFolder().getChildFile ("Glisses")
                        .getChildFile ("Ouroboros_Frag" + juce::String ((int) f.id) + "_" + bpmTag() + ".wav");
        if (! file.existsAsFile())
        {
            writeWav (file, buf, b->getSampleRate());
            writeJson (file.withFileExtension ("json"), fragmentMeta (f, b->getSampleRate(), proc.telemetry.bpm.load(), 0, "one-shot"));
        }
        return file;
    }
    return {};
}

juce::File Controller::writeLoopForDrag()
{
    const GenNode* node = nullptr;
    for (auto& n : tree) if (n.id == currentNode) node = &n;
    if (node == nullptr || node->audio == nullptr)
    {
        setStatus ("Pas de generation a glisser");
        return {};
    }
    juce::AudioBuffer<float> buf (2, node->audio->getNumFrames());
    node->audio->copyTo (buf, 0, buf.getNumSamples());
    auto file = getExportFolder().getChildFile ("Glisses")
                    .getChildFile ("Ouroboros_Gen" + juce::String (node->number) + "_seed" + juce::String ((int) node->seed) + "_" + bpmTag() + ".wav");
    if (! file.existsAsFile())
        writeWav (file, buf, node->audio->getSampleRate());
    return file;
}

//==============================================================================
// Presets
//==============================================================================
static juce::File presetFolder()
{
    return defaultRoot().getChildFile ("Presets");
}

namespace
{
    struct Factory { const char* name; const char* tags; std::vector<std::pair<int, float>> values; };

    const std::vector<Factory>& factoryPresets()
    {
        static const std::vector<Factory> f =
        {
            { "Init", "base", {} },
            { "Bouillie douce", "texture|ambient",
              { { P::seqDivision, 1 }, { P::ruleSimilar, 1.0f }, { P::ruleRandom, 0.3f }, { P::ruleContrast, 0.0f },
                { P::verbOn, 1 }, { P::verbMix, 0.5f }, { P::verbSize, 0.85f }, { P::dlyOn, 1 }, { P::dlyMix, 0.35f },
                { P::seqLengthScale, 1.0f }, { P::mutation, 0.15f }, { P::dna, 0.7f } } },
            { "Glitch serre", "glitch|rythme",
              { { P::seqDivision, 3 }, { P::ruleContrast, 0.8f }, { P::stutOn, 1 }, { P::stutChance, 0.45f },
                { P::crushOn, 1 }, { P::crushBits, 6 }, { P::crushMix, 0.6f }, { P::seqLengthScale, 0.35f }, { P::swing, 0.2f } } },
            { "Mutant", "generations|chaos",
              { { P::genMode, 1 }, { P::genBars, 1 }, { P::mutation, 0.6f }, { P::death, 0.5f }, { P::dna, 0.25f },
                { P::shiftOn, 1 }, { P::shiftMode, 1 }, { P::shiftAmount, 0.1f }, { P::shiftMix, 0.4f } } },
            { "Cathedrale cassee", "ambient|reverb",
              { { P::seqDivision, 0 }, { P::verbOn, 1 }, { P::verbMix, 0.7f }, { P::verbSize, 0.95f }, { P::seqPitch, -12 },
                { P::shiftOn, 1 }, { P::shiftAmount, 1.0f }, { P::shiftMix, 0.3f }, { P::filtOn, 1 }, { P::filtCutoff, 3500 } } },
            { "Chaos total", "chaos|glitch",
              { { P::chaos, 0.8f }, { P::seqDivision, 3 }, { P::stutOn, 1 }, { P::distOn, 1 }, { P::distMode, 1 },
                { P::distDrive, 0.6f }, { P::seqReverse, 0.4f }, { P::crushOn, 1 } } },
        };
        return f;
    }
}

std::vector<PresetInfo> Controller::listPresets()
{
    std::vector<PresetInfo> out;
    const auto& f = factoryPresets();
    for (int i = 0; i < (int) f.size(); ++i)
    {
        PresetInfo p;
        p.name = f[(size_t) i].name;
        p.tags = juce::StringArray::fromTokens (f[(size_t) i].tags, "|", "");
        p.factory = true;
        p.factoryIndex = i;
        p.favourite = settings->getBoolValue ("fav_factory_" + p.name, false);
        out.push_back (p);
    }

    for (const auto& file : presetFolder().findChildFiles (juce::File::findFiles, false, "*.ouropreset"))
    {
        if (auto xml = juce::XmlDocument::parse (file))
        {
            PresetInfo p;
            p.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
            p.tags = juce::StringArray::fromTokens (xml->getStringAttribute ("tags"), "|", "");
            p.favourite = xml->getBoolAttribute ("favourite");
            p.file = file;
            out.push_back (p);
        }
    }
    return out;
}

void Controller::applyFactoryPreset (int index)
{
    const auto& f = factoryPresets()[(size_t) juce::jlimit (0, (int) factoryPresets().size() - 1, index)];
    auto v = P::defaults();
    for (auto& [p, val] : f.values)
        v[(size_t) p] = val;
    // Les réglages globaux (mémoire, capture, gain) ne sont pas écrasés
    const auto cur = currentValues();
    for (int p : { (int) P::memoryBudget, (int) P::captureSeconds, (int) P::captureSource, (int) P::outGain, (int) P::dryWet })
        v[(size_t) p] = cur[(size_t) p];
    applyValues (v, false);
    pattern = Pattern();
    publishPattern();
}

void Controller::loadPreset (const PresetInfo& p)
{
    beginGesture();
    if (p.factory)
        applyFactoryPreset (p.factoryIndex);
    else if (auto xml = juce::XmlDocument::parse (p.file))
    {
        auto t = juce::ValueTree::fromXml (*xml);
        auto state = t.getChildWithName (proc.apvts.state.getType());
        if (state.isValid())
        {
            // On conserve les réglages globaux de l'instance
            const auto cur = currentValues();
            proc.apvts.replaceState (state);
            P::Values v = currentValues();
            for (int i : { (int) P::memoryBudget, (int) P::captureSeconds, (int) P::captureSource })
                v[(size_t) i] = cur[(size_t) i];
            applyValues (v, false);
        }
        applyPatternAndMods (t.getChildWithName ("EXTRA"));
    }
    currentPresetName = p.name;
    setStatus ("Preset : " + p.name);
}

void Controller::savePreset (const juce::String& name, const juce::StringArray& tags, bool favourite)
{
    juce::ValueTree t ("OUROPRESET");
    t.setProperty ("name", name, nullptr);
    t.setProperty ("tags", tags.joinIntoString ("|"), nullptr);
    t.setProperty ("favourite", favourite, nullptr);
    t.appendChild (proc.apvts.copyState(), nullptr);

    juce::ValueTree extra ("EXTRA");
    auto full = saveExtraState();
    for (auto c : full)
        if (c.hasType ("PATTERN") || c.hasType ("MODS"))
            extra.appendChild (c.createCopy(), nullptr);
    t.appendChild (extra, nullptr);

    auto file = presetFolder().getChildFile (juce::File::createLegalFileName (name) + ".ouropreset");
    file.getParentDirectory().createDirectory();
    if (auto xml = t.createXml())
    {
        // les attributs name/tags/favourite sont aussi lus directement par listPresets()
        xml->writeTo (file);
    }
    currentPresetName = name;
    setStatus ("Preset sauvegarde : " + name);
}

void Controller::setPresetFavourite (const PresetInfo& p, bool fav)
{
    if (p.factory)
    {
        settings->setValue ("fav_factory_" + p.name, fav);
        settings->saveIfNeeded();
        return;
    }
    if (auto xml = juce::XmlDocument::parse (p.file))
    {
        xml->setAttribute ("favourite", fav);
        xml->writeTo (p.file);
    }
}
