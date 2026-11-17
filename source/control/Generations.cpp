#include "Controller.h"
#include "PluginProcessor.h"
#include "engine/CrossRenderer.h"

/**
    Rendu hors ligne du moteur (même code qu'en temps réel) : sert aux générations,
    aux loops exportées et au batch. Appelé depuis un thread de fond.
*/
juce::AudioBuffer<float> Controller::renderOffline (const P::Values& values, const Pattern& pat, int bars, uint32_t seed,
                                                  std::shared_ptr<const LibrarySnapshot> lib, const ModSnapshot& modsCopy)
{
    const double sr = proc.telemetry.sampleRate.load();
    const double bpm = juce::jlimit (20.0, 400.0, proc.telemetry.bpm.load());
    const int beatsPerBar = juce::jmax (1, proc.telemetry.beatsPerBar.load());
    const int total = (int) (bars * beatsPerBar * 60.0 / bpm * sr);
    constexpr int block = 512;

    EngineCore engine;
    engine.prepare (sr, block);
    engine.library = lib;
    engine.pattern = std::make_shared<Pattern> (pat);
    engine.mods = std::make_shared<ModSnapshot> (modsCopy);
    engine.setSeed (seed);

    OutputChain out;
    out.prepare (sr, block);

    P::Values v = values;
    v[P::seqRun] = 0.0f;
    v[P::dryWet] = 1.0f;
    v[P::outGain] = 0.0f;

    // Queue de 1 s pour laisser résonner delay / reverb, puis on replie la queue sur le début
    const int tail = (int) sr;
    juce::AudioBuffer<float> buf (2, total + tail);
    buf.clear();

    Transport t;
    t.playing = true;
    t.bpm = bpm;
    for (int pos = 0; pos < total + tail; pos += block)
    {
        const int n = juce::jmin (block, total + tail - pos);
        t.ppq = pos * bpm / 60.0 / sr;
        t.playing = pos < total;
        engine.process (buf.getWritePointer (0, pos), buf.getWritePointer (1, pos), n, v, t, nullptr);
        out.process (buf.getWritePointer (0, pos), buf.getWritePointer (1, pos), n, v);
    }

    // Boucle parfaite : la queue est ajoutée au début
    for (int ch = 0; ch < 2; ++ch)
        buf.addFrom (ch, 0, buf, ch, total, juce::jmin (tail, total));
    buf.setSize (2, total, true);

    // Le repli de la queue peut dépasser le plafond : on ramène sous le plafond
    const float ceiling = juce::Decibels::decibelsToGain (v[P::limCeiling]);
    const float peak = buf.getMagnitude (0, buf.getNumSamples());
    if (peak > ceiling)
        buf.applyGain (ceiling / peak);
    return buf;
}

void Controller::fadeOldGenerations (int64_t bytesNeeded)
{
    const int maxKeep = juce::roundToInt (proc.apvts.getRawParameterValue ("maxGens")->load());

    auto countAlive = [this]
    {
        int c = 0;
        for (auto& n : tree) if (! n.faded && n.id != 0) ++c;
        return c;
    };

    // Les plus anciennes générations perdent leur audio (la recette reste dans l'arbre)
    for (auto& n : tree)
    {
        const bool overCount = countAlive() > maxKeep;
        const bool overMem = ! proc.budget.canAllocate (bytesNeeded);
        if (! overCount && ! overMem)
            break;
        if (n.id == 0 || n.id == currentNode || n.faded)
            continue;
        n.faded = true;
        n.frags.clear();
        n.audio.reset();
    }
    proc.trash.empty();
}

void Controller::newGeneration()
{
    if (generating)
        return;
    if (library == nullptr || library->size() == 0)
    {
        setStatus ("Rien a muter : avale ou glisse un son d'abord");
        return;
    }

    static const int barsChoices[] = { 1, 2, 4, 8, 16 };
    auto values = currentValues();
    const int bars = barsChoices[juce::jlimit (0, 4, (int) values[P::genBars])];
    const float mutation = values[P::mutation];
    const float deathRate = values[P::death];
    const uint32_t seed = nextSeed();
    juce::Random rng ((juce::int64) seed);

    // --- Mutation : séquenceur + FX + modulation, dosée par le taux de mutation, cadenas respectés
    for (int i = 0; i < P::count; ++i)
    {
        const auto& inf = P::info (i);
        if (! inf.randomizable || locks[(size_t) i] || moduleLocks[(size_t) inf.module])
            continue;
        if (inf.module != P::Module::Seq && inf.module != P::Module::Fx && inf.module != P::Module::Mod)
            continue;
        if (rng.nextFloat() > mutation)
            continue;
        const float norm = inf.toNorm (values[(size_t) i]);
        const auto [lo, hi] = ranges[(size_t) i];
        float target = norm + (rng.nextFloat() * 2.0f - 1.0f) * mutation * 0.4f;
        if (inf.kind == P::Kind::B && rng.nextFloat() < mutation * 0.3f)
            target = 1.0f - norm;
        values[(size_t) i] = inf.fromNorm (juce::jlimit (lo, hi, target));
    }
    applyValues (values, true);
    if (! moduleLocks[(size_t) P::Module::Seq])
        randomizePattern (mutation * 0.5f, rng);

    // Place mémoire : on estime la taille du rendu
    const double sr = proc.telemetry.sampleRate.load();
    const double bpm = juce::jmax (20.0, proc.telemetry.bpm.load());
    const int64_t bytes = (int64_t) (bars * juce::jmax (1, proc.telemetry.beatsPerBar.load()) * 60.0 / bpm * sr) * 4;
    fadeOldGenerations (bytes);

    // Copie de la génération courante (survivants potentiels)
    std::vector<FragRec> previous;
    for (const auto& n : tree)
        if (n.id == currentNode) previous = n.frags;
    const auto liked = likedIds;
    const int parent = currentNode;
    const int seqDiv = (int) values[P::seqDivision];
    const auto pat = pattern;
    const auto lib = library;
    const auto modsCopy = mods;

    generating = true;
    setStatus ("Ouroboros avale sa queue...");

    runJob ([this, values, pat, bars, seed, deathRate, previous, liked, parent, seqDiv, sr, lib, modsCopy]() mutable
    {
        juce::Random rng ((juce::int64) seed + 1);
        auto rendered = renderOffline (values, pat, bars, seed, lib, modsCopy);
        const float peak = rendered.getMagnitude (0, rendered.getNumSamples());

        std::shared_ptr<SampleData> audio;
        std::vector<FragRec> frags;
        juce::String error;

        if (peak < 0.001f)
            error = "Generation vide : rien n'a joue (verifie les sources et le sequenceur)";
        else
        {
            CrossRenderer::normalise (rendered, -1.0f);
            audio = SampleData::fromBuffer (proc.budget, rendered, 0, rendered.getNumSamples(), sr, "Generation");
            if (audio == nullptr)
                error = "Memoire pleine : generation annulee (baisse Gen. max ou augmente la memoire)";
        }

        if (audio != nullptr)
        {
            // Découpe de la génération sur la grille du séquenceur
            SliceSettings ss;
            ss.mode = SliceSettings::Grid;
            ss.gridDivision = juce::jlimit (0, 4, seqDiv);
            auto mono = audio->toMono (0, audio->getNumFrames());
            auto regions = Slicer::slice (mono, sr, proc.telemetry.bpm.load(), ss, 128, rng);

            // On écarte les tranches silencieuses
            std::vector<SliceRegion> kept;
            for (auto& r : regions)
            {
                float pk = 0;
                for (int i = r.start; i < r.start + r.length; i += 4) pk = juce::jmax (pk, std::abs (mono[(size_t) i]));
                if (pk > 0.01f) kept.push_back (r);
            }
            frags = analyseRegions (audio, kept, -1, 0, juce::Colour (0xffff3b30), 1.0f);

            // Sélection : les likés survivent toujours, les autres selon le taux de mort.
            // Les survivants sont copiés dans un petit buffer à eux (compactage) pour que
            // l'audio des anciennes générations puisse être recyclé.
            for (auto& r : previous)
            {
                const bool isLiked = liked.count (r.f.id) > 0;
                if (! isLiked && rng.nextFloat() < deathRate)
                    continue;
                juce::AudioBuffer<float> tmp (2, r.f.length);
                r.buf->copyTo (tmp, r.f.start, r.f.length);
                auto own = SampleData::fromBuffer (proc.budget, tmp, 0, r.f.length, r.buf->getSampleRate(), "Survivant");
                if (own == nullptr)
                    continue;
                FragRec s = r;
                s.buf = own;
                s.f.start = 0;
                frags.push_back (std::move (s));
            }
        }

        onMessageThread ([this, audio, frags = std::move (frags), error, seed, parent, pat]() mutable
        {
            generating = false;
            if (audio == nullptr)
            {
                setStatus (error);
                return;
            }

            int parentNumber = 0;
            for (auto& n : tree) if (n.id == parent) parentNumber = n.number;

            GenNode node;
            node.id = tree.empty() ? 0 : tree.back().id + 1;
            node.parent = parent;
            node.number = parentNumber + 1;
            node.seed = seed;
            node.audio = audio;
            node.params = proc.apvts.copyState();
            node.pattern = pattern;

            // numéro de génération sur les nouveaux fragments
            for (auto& r : frags)
                if (r.f.generation == 0 && r.f.slot < 0) r.f.generation = node.number;
            if (frags.size() > 384)
                frags.resize (384);
            node.frags = std::move (frags);

            tree.push_back (std::move (node));
            currentNode = tree.back().id;
            ++generationCounter;
            fadeOldGenerations (0);
            publishLibrary();
            setStatus ("Generation " + juce::String (tree.back().number) + " : "
                       + juce::String ((int) tree.back().frags.size()) + " fragments");
        });
    });
}

void Controller::selectNode (int id)
{
    for (auto& n : tree)
    {
        if (n.id != id)
            continue;
        currentNode = id;
        if (n.params.isValid())
            proc.apvts.replaceState (n.params.createCopy());
        if (id != 0)
        {
            pattern = n.pattern;
            publishPattern();
        }
        publishLibrary();
        setStatus (n.faded ? "Branche fanee : reglages restaures, son recycle (repars d'ici avec Nouvelle generation)"
                           : "Retour a la generation " + juce::String (n.number));
        return;
    }
}

void Controller::crossSelected()
{
    if (library == nullptr || library->size() < 1)
    {
        setStatus ("Pas de fragments a croiser");
        return;
    }

    auto findRec = [this] (uint32_t id) -> const FragRec*
    {
        for (auto& s : slots)
            for (auto& r : s.frags)
                if (r.f.id == id) return &r;
        for (auto& n : tree)
            for (auto& r : n.frags)
                if (r.f.id == id) return &r;
        return nullptr;
    };

    const FragRec* a = findRec (selA);
    const FragRec* b = findRec (selB);
    auto randomRec = [this]() -> const FragRec*
    {
        const auto& f = library->frags[(size_t) random.nextInt (library->size())];
        for (auto& s : slots) for (auto& r : s.frags) if (r.f.id == f.id) return &r;
        for (auto& n : tree) for (auto& r : n.frags) if (r.f.id == f.id) return &r;
        return nullptr;
    };
    if (a == nullptr) a = randomRec();
    if (b == nullptr || b == a) b = randomRec();
    if (a == nullptr || b == nullptr)
        return;

    const FragRec ra = *a, rb = *b;
    const int mode = (int) proc.apvts.getRawParameterValue ("crossMode")->load();
    const double sr = proc.telemetry.sampleRate.load();
    const uint32_t seed = nextSeed();
    setStatus ("Croisement en cours...");

    runJob ([this, ra, rb, mode, sr, seed]
    {
        juce::Random rng ((juce::int64) seed);
        auto out = CrossRenderer::render (*ra.buf, ra.f, *rb.buf, rb.f, mode, sr, rng);
        std::shared_ptr<SampleData> data;
        if (out.getNumSamples() > 32)
            data = SampleData::fromBuffer (proc.budget, out, 0, out.getNumSamples(), sr, "Croisement");

        std::vector<FragRec> recs;
        if (data != nullptr)
            recs = analyseRegions (data, { { 0, data->getNumFrames() } }, -1, 0, juce::Colour (0xffffffff), 1.0f);

        onMessageThread ([this, recs]
        {
            if (recs.empty())
            {
                setStatus ("Croisement impossible (memoire pleine ?)");
                return;
            }
            for (auto& n : tree)
                if (n.id == currentNode)
                {
                    n.frags.insert (n.frags.begin(), recs.begin(), recs.end());
                    if (n.frags.size() > 384) n.frags.resize (384);
                }
            selA = recs.front().f.id;
            publishLibrary();
            setStatus ("Croisement ajoute a la carte (point blanc)");
        });
    });
}
