#include "Controller.h"
#include "PluginProcessor.h"

//==============================================================================
// Random
//==============================================================================
uint32_t Controller::nextSeed()
{
    lastSeed = (uint32_t) (random.nextInt (0x7fffffff) & 0xffffff) + 1;
    return lastSeed;
}

static bool randomTargetsModule (P::Module m)
{
    return m == P::Module::Seq || m == P::Module::Ouro || m == P::Module::Fx
        || m == P::Module::Mod || m == P::Module::Out;
}

void Controller::randomizeModule (P::Module module, uint32_t seed)
{
    juce::Random rng ((juce::int64) seed);
    auto values = currentValues();
    const float amount = proc.apvts.getRawParameterValue ("randomAmount")->load();

    // Historique : on garde l'état AVANT le random pour pouvoir revenir
    HistoryEntry h;
    h.seed = seed;
    h.values = values;
    h.pattern = pattern;
    h.label = juce::String (P::moduleName (module));

    for (int i = 0; i < P::count; ++i)
    {
        const auto& inf = P::info (i);
        if (! inf.randomizable || locks[(size_t) i] || moduleLocks[(size_t) inf.module])
            continue;
        if (module != P::Module::Rand && inf.module != module)
            continue;
        if (module == P::Module::Rand && ! randomTargetsModule (inf.module))
            continue;

        const auto [lo, hi] = ranges[(size_t) i];
        const float r = lo + rng.nextFloat() * (hi - lo);
        const float norm = inf.toNorm (values[(size_t) i]);
        values[(size_t) i] = inf.fromNorm (norm + (r - norm) * amount);
        if (inf.kind == P::Kind::B)
            values[(size_t) i] = (rng.nextFloat() < 0.5f * amount + (norm > 0.5f ? 1.0f - amount : 0.0f) * 0.5f) ? 1.0f : 0.0f;
        if (inf.kind == P::Kind::C || inf.kind == P::Kind::I)
            values[(size_t) i] = std::round (values[(size_t) i]);
    }

    // FX : on évite d'activer plus de 4 effets d'un coup
    if (module == P::Module::Fx || module == P::Module::Rand)
    {
        static const int onIds[] = { P::crushOn, P::distOn, P::filtOn, P::shiftOn, P::stutOn, P::dlyOn, P::verbOn };
        int on = 0;
        for (int id : onIds)
            if (values[(size_t) id] > 0.5f && ++on > 4 && ! locks[(size_t) id])
                values[(size_t) id] = 0.0f;
    }

    beginGesture();
    applyValues (values, true);

    if ((module == P::Module::Seq || module == P::Module::Rand) && ! moduleLocks[(size_t) P::Module::Seq])
        randomizePattern (amount, rng);

    history.insert (history.begin(), h);
    // On garde les favoris + 48 entrées maximum
    while (history.size() > 48)
    {
        auto it = std::find_if (history.rbegin(), history.rend(), [] (auto& e) { return ! e.favourite; });
        if (it == history.rend()) break;
        history.erase (std::next (it).base());
    }

    setStatus ("Random " + juce::String (P::moduleName (module)) + " - seed " + juce::String (seed));
}

void Controller::randomizeAll (uint32_t seed)
{
    randomizeModule (P::Module::Rand, seed);
}

void Controller::recallHistory (int index)
{
    if (index < 0 || index >= (int) history.size())
        return;
    const auto e = history[(size_t) index];
    beginGesture();
    applyValues (e.values, false);
    pattern = e.pattern;
    publishPattern();
    setStatus ("Retour avant le random seed " + juce::String (e.seed));
}

void Controller::toggleFavourite (int index)
{
    if (index >= 0 && index < (int) history.size())
        history[(size_t) index].favourite = ! history[(size_t) index].favourite;
    sendChangeMessage();
}

void Controller::setRange (int param, float lo, float hi)
{
    ranges[(size_t) param] = { juce::jlimit (0.0f, 1.0f, juce::jmin (lo, hi)), juce::jlimit (0.0f, 1.0f, juce::jmax (lo, hi)) };
    stateDirty = true;
}

//==============================================================================
// État (sauvegarde dans le projet)
//==============================================================================
static juce::String floatsToBase64 (const float* data, size_t n)
{
    juce::MemoryBlock mb (data, n * sizeof (float));
    return mb.toBase64Encoding();
}

static void base64ToFloats (const juce::String& s, float* data, size_t n)
{
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (s) && mb.getSize() == n * sizeof (float))
        std::memcpy (data, mb.getData(), mb.getSize());
}

static juce::ValueTree patternToTree (const Pattern& p)
{
    juce::ValueTree t ("PATTERN");
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const auto& s = p.steps[(size_t) i];
        juce::ValueTree st ("STEP");
        st.setProperty ("on", s.on, nullptr);
        st.setProperty ("prob", s.prob, nullptr);
        st.setProperty ("ratchet", s.ratchet, nullptr);
        st.setProperty ("rev", s.reverse, nullptr);
        st.setProperty ("pitch", s.pitch, nullptr);
        st.setProperty ("len", s.length, nullptr);
        t.appendChild (st, nullptr);
    }
    return t;
}

static void treeToPattern (const juce::ValueTree& t, Pattern& p)
{
    for (int i = 0; i < juce::jmin (kMaxSteps, t.getNumChildren()); ++i)
    {
        auto st = t.getChild (i);
        auto& s = p.steps[(size_t) i];
        s.on = st.getProperty ("on", true);
        s.prob = st.getProperty ("prob", 1.0f);
        s.ratchet = st.getProperty ("ratchet", 1);
        s.reverse = st.getProperty ("rev", 0.0f);
        s.pitch = st.getProperty ("pitch", 0.0f);
        s.length = st.getProperty ("len", 1.0f);
    }
}

static juce::ValueTree modsToTree (const ModSnapshot& m)
{
    juce::ValueTree t ("MODS");
    for (int l = 0; l < kNumLfos; ++l)
        t.setProperty ("shape" + juce::String (l), floatsToBase64 (m.shapes[(size_t) l].data(), kLfoPoints), nullptr);
    juce::String order;
    for (int i : m.fxOrder) order << i << ",";
    t.setProperty ("fxOrder", order, nullptr);
    for (const auto& r : m.routes)
    {
        if (r.source < 0) continue;
        juce::ValueTree rt ("ROUTE");
        rt.setProperty ("src", r.source, nullptr);
        rt.setProperty ("param", P::info (r.param).id, nullptr);
        rt.setProperty ("depth", r.depth, nullptr);
        t.appendChild (rt, nullptr);
    }
    return t;
}

static void treeToMods (const juce::ValueTree& t, ModSnapshot& m)
{
    for (int l = 0; l < kNumLfos; ++l)
        base64ToFloats (t.getProperty ("shape" + juce::String (l)).toString(), m.shapes[(size_t) l].data(), kLfoPoints);
    auto order = juce::StringArray::fromTokens (t.getProperty ("fxOrder").toString(), ",", "");
    order.removeEmptyStrings();
    if (order.size() == 7)
        for (int i = 0; i < 7; ++i) m.fxOrder[(size_t) i] = juce::jlimit (0, 6, order[i].getIntValue());
    m.routes = {};
    int k = 0;
    for (auto rt : t)
    {
        if (! rt.hasType ("ROUTE") || k >= kMaxRoutes) continue;
        const int p = P::indexOf (rt.getProperty ("param").toString());
        if (p < 0) continue;
        m.routes[(size_t) k++] = { (int) rt.getProperty ("src"), p, (float) rt.getProperty ("depth") };
    }
}

juce::ValueTree Controller::saveExtraState() const
{
    juce::ValueTree t ("EXTRA");
    t.appendChild (patternToTree (pattern), nullptr);
    t.appendChild (modsToTree (mods), nullptr);
    t.setProperty ("axisX", axisX, nullptr);
    t.setProperty ("axisY", axisY, nullptr);
    t.setProperty ("maskEmpty", maskEmpty, nullptr);
    if (! maskEmpty)
        t.setProperty ("mask", floatsToBase64 (mask.data(), mask.size()), nullptr);
    t.setProperty ("preset", currentPresetName, nullptr);

    // Cadenas et plages
    juce::ValueTree lk ("LOCKS");
    for (int i = 0; i < P::count; ++i)
    {
        if (locks[(size_t) i]) lk.setProperty (P::info (i).id, true, nullptr);
        if (ranges[(size_t) i].first > 0.0f || ranges[(size_t) i].second < 1.0f)
            lk.setProperty (juce::String ("range_") + P::info (i).id,
                            juce::String (ranges[(size_t) i].first) + ";" + juce::String (ranges[(size_t) i].second), nullptr);
    }
    juce::String ml;
    for (bool b : moduleLocks) ml << (b ? "1" : "0");
    lk.setProperty ("modules", ml, nullptr);
    t.appendChild (lk, nullptr);

    // Sources : réglages + chemin du fichier (l'audio capturé n'est pas sauvegardé)
    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto& s = slots[(size_t) i];
        juce::ValueTree st ("SLOT");
        st.setProperty ("index", i, nullptr);
        st.setProperty ("used", s.used && s.file.existsAsFile(), nullptr);
        st.setProperty ("file", s.file.getFullPathName(), nullptr);
        st.setProperty ("active", s.active, nullptr);
        st.setProperty ("gain", s.gain, nullptr);
        st.setProperty ("weight", s.weight, nullptr);
        st.setProperty ("mode", s.slice.mode, nullptr);
        st.setProperty ("thr", s.slice.threshold, nullptr);
        st.setProperty ("sens", s.slice.sensitivity, nullptr);
        st.setProperty ("grid", s.slice.gridDivision, nullptr);
        st.setProperty ("rmin", s.slice.randomMinMs, nullptr);
        st.setProperty ("rmax", s.slice.randomMaxMs, nullptr);
        t.appendChild (st, nullptr);
    }

    // Fragments likés : seul audio intégré au projet, compressé en FLAC, 16 Mo maximum
    juce::ValueTree likedTree ("LIKED");
    int64_t totalBytes = 0;
    if (library != nullptr)
    {
        juce::FlacAudioFormat flac;
        for (const auto& f : library->frags)
        {
            if (! f.liked)
                continue;
            const auto& buf = library->buffers[(size_t) f.buffer];
            juce::AudioBuffer<float> tmp (2, f.length);
            buf->copyTo (tmp, f.start, f.length);

            juce::MemoryOutputStream* mos = new juce::MemoryOutputStream();
            std::unique_ptr<juce::OutputStream> os (mos);
            auto writer = flac.createWriterFor (os, juce::AudioFormatWriterOptions{}
                                                        .withSampleRate (buf->getSampleRate())
                                                        .withNumChannels (2)
                                                        .withBitsPerSample (16));
            if (writer == nullptr) continue;
            writer->writeFromAudioSampleBuffer (tmp, 0, tmp.getNumSamples());
            writer->flush();
            const auto block = mos->getMemoryBlock();
            writer.reset();
            totalBytes += (int64_t) block.getSize();
            if (totalBytes > 16ll * 1024 * 1024)
                break;

            juce::ValueTree lt ("FRAG");
            lt.setProperty ("gen", f.generation, nullptr);
            lt.setProperty ("sr", buf->getSampleRate(), nullptr);
            lt.setProperty ("flac", block.toBase64Encoding(), nullptr);
            likedTree.appendChild (lt, nullptr);
        }
    }
    t.appendChild (likedTree, nullptr);
    return t;
}

void Controller::restoreExtraState (const juce::ValueTree& t)
{
    if (! t.isValid())
        return;

    treeToPattern (t.getChildWithName ("PATTERN"), pattern);
    treeToMods (t.getChildWithName ("MODS"), mods);
    publishPattern();
    publishMods();

    axisX = t.getProperty ("axisX", 0);
    axisY = t.getProperty ("axisY", 3);
    maskEmpty = t.getProperty ("maskEmpty", true);
    if (! maskEmpty)
        base64ToFloats (t.getProperty ("mask").toString(), mask.data(), mask.size());
    currentPresetName = t.getProperty ("preset", "Init").toString();

    auto lk = t.getChildWithName ("LOCKS");
    for (int i = 0; i < P::count; ++i)
    {
        locks[(size_t) i] = lk.getProperty (P::info (i).id, false);
        auto r = juce::StringArray::fromTokens (lk.getProperty (juce::String ("range_") + P::info (i).id, "0;1").toString(), ";", "");
        ranges[(size_t) i] = { r[0].getFloatValue(), r.size() > 1 ? r[1].getFloatValue() : 1.0f };
    }
    const auto ml = lk.getProperty ("modules").toString();
    for (int i = 0; i < (int) moduleLocks.size() && i < ml.length(); ++i)
        moduleLocks[(size_t) i] = ml[i] == '1';

    // Sources fichiers : rechargées depuis le disque
    for (auto st : t)
    {
        if (! st.hasType ("SLOT")) continue;
        const int i = st.getProperty ("index");
        if (i < 0 || i >= kNumSlots) continue;
        auto& s = slots[(size_t) i];
        s.active = st.getProperty ("active", true);
        s.gain = st.getProperty ("gain", 1.0f);
        s.weight = st.getProperty ("weight", 1.0f);
        s.slice.mode = st.getProperty ("mode", 0);
        s.slice.threshold = st.getProperty ("thr", 0.5f);
        s.slice.sensitivity = st.getProperty ("sens", 0.5f);
        s.slice.gridDivision = st.getProperty ("grid", 2);
        s.slice.randomMinMs = st.getProperty ("rmin", 60.0f);
        s.slice.randomMaxMs = st.getProperty ("rmax", 400.0f);
        const juce::File f (st.getProperty ("file").toString());
        if ((bool) st.getProperty ("used") && f.existsAsFile())
            loadFiles ({ f.getFullPathName() }, i);
    }

    // Fragments likés intégrés
    auto likedTree = t.getChildWithName ("LIKED");
    if (likedTree.getNumChildren() > 0)
    {
        juce::FlacAudioFormat flac;
        std::vector<FragRec> recs;
        for (auto lt : likedTree)
        {
            juce::MemoryBlock mb;
            if (! mb.fromBase64Encoding (lt.getProperty ("flac").toString())) continue;
            std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (mb, false), true));
            if (reader == nullptr) continue;
            const int n = (int) reader->lengthInSamples;
            juce::AudioBuffer<float> buf (2, n);
            reader->read (&buf, 0, n, 0, true, true);
            auto data = SampleData::fromBuffer (proc.budget, buf, 0, n, reader->sampleRate, "Like");
            if (data == nullptr) break;
            auto r = analyseRegions (data, { { 0, n } }, -1, (int) lt.getProperty ("gen", 0), juce::Colour (0xffff3b30), 1.0f);
            for (auto& rec : r)
            {
                likedIds.insert (rec.f.id);
                recs.push_back (rec);
            }
        }
        for (auto& n : tree)
            if (n.id == currentNode)
                n.frags.insert (n.frags.end(), recs.begin(), recs.end());
    }

    publishLibrary();
    sendChangeMessage();
}

void Controller::applyPatternAndMods (const juce::ValueTree& extra)
{
    if (! extra.isValid())
        return;
    auto pat = extra.getChildWithName ("PATTERN");
    if (pat.isValid())
    {
        pattern = Pattern();
        treeToPattern (pat, pattern);
        publishPattern();
    }
    auto m = extra.getChildWithName ("MODS");
    if (m.isValid())
    {
        treeToMods (m, mods);
        publishMods();
    }
}
