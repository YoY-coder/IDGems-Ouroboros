#include <map>
#include <algorithm>
#include "Controller.h"
#include "PluginProcessor.h"

Controller::Controller (OuroborosProcessor& p)
    : proc (p), undoManager (p.undoManager)
{
    formats.registerBasicFormats();
    pool = std::make_unique<juce::ThreadPool> (juce::ThreadPoolOptions{}.withThreadName ("Ouroboros worker")
                                                                        .withNumberOfThreads (1));
    alive = std::make_shared<std::atomic<bool>> (true);

    juce::PropertiesFile::Options opt;
    opt.applicationName = "IDGems Ouroboros";
    opt.filenameSuffix = "settings";
    opt.folderName = "IDGems";
    opt.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (opt);

    for (int i = 0; i < kNumSlots; ++i)
        slots[(size_t) i].colour = slotColour (i);

    for (auto& r : ranges)
        r = { 0.0f, 1.0f };

    // Formes de LFO par défaut : sinus, triangle, rampe, carré
    for (int l = 0; l < kNumLfos; ++l)
        for (int i = 0; i < kLfoPoints; ++i)
        {
            const float ph = (float) i / kLfoPoints;
            float v = 0;
            switch (l)
            {
                case 0: v = 0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi * ph); break;
                case 1: v = ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f; break;
                case 2: v = ph; break;
                default: v = ph < 0.5f ? 1.0f : 0.0f; break;
            }
            mods.shapes[(size_t) l][(size_t) i] = v;
        }

    // Arbre : racine = matière d'origine
    GenNode root;
    root.id = 0;
    root.number = 0;
    root.params = proc.apvts.copyState();
    tree.push_back (root);

    publishPattern();
    publishMods();
    publishLibrary();
    startTimerHz (20);
}

Controller::~Controller()
{
    stopTimer();
    alive->store (false);
    pool->removeAllJobs (true, 4000);
    pool.reset();
}

void Controller::onMessageThread (std::function<void()> f)
{
    auto flag = alive;
    juce::MessageManager::callAsync ([flag, fn = std::move (f)]() mutable
    {
        if (flag->load())
            fn();
    });
}

void Controller::runJob (std::function<void()> job)
{
    ++busyJobs;
    auto flag = alive;
    pool->addJob ([this, flag, job = std::move (job)]
    {
        if (flag->load())
            job();
        --busyJobs;
    });
}

void Controller::setStatus (const juce::String& s)
{
    status = s;
    statusTime = juce::Time::getMillisecondCounter();
    sendChangeMessage();
}

juce::Colour Controller::slotColour (int slot)
{
    static const juce::uint32 cols[] = { 0xffff6a1a, 0xff2f8cff, 0xffff3b6b, 0xff19e3d6,
                                         0xffffc21a, 0xffa45bff, 0xff7dff4a, 0xffff9ad5 };
    return juce::Colour (cols[juce::jlimit (0, 7, slot)]);
}

juce::String Controller::bpmTag() const
{
    return juce::String (juce::roundToInt (proc.telemetry.bpm.load())) + "bpm";
}

int64_t Controller::memoryUsed() const  { return proc.budget.used.load(); }
int64_t Controller::memoryLimit() const { return proc.budget.limit.load(); }

float Controller::captureFilledSeconds() const
{
    auto r = proc.getRing();
    return r != nullptr ? r->filledSeconds() : 0.0f;
}

P::Values Controller::currentValues() const
{
    P::Values v {};
    for (int i = 0; i < P::count; ++i)
        v[(size_t) i] = proc.param (i)->convertFrom0to1 (proc.param (i)->getValue());
    return v;
}

void Controller::applyValues (const P::Values& v, bool respectLocks)
{
    for (int i = 0; i < P::count; ++i)
    {
        if (respectLocks && locks[(size_t) i])
            continue;
        auto* p = proc.param (i);
        const float norm = p->convertTo0to1 (v[(size_t) i]);
        if (std::abs (norm - p->getValue()) > 1.0e-6f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
    }
}

//==============================================================================
void Controller::timerCallback()
{
    proc.trash.empty();

    // Budget mémoire réglé par l'utilisateur
    const int budgetChoice = (int) proc.apvts.getRawParameterValue ("memoryBudget")->load();
    if (budgetChoice != lastBudgetChoice)
    {
        lastBudgetChoice = budgetChoice;
        proc.budget.limit.store (MemoryBudget::limitForChoice (budgetChoice));
        if (memoryUsed() > memoryLimit())
            fadeOldGenerations (0);
    }

    updateCaptureRingIfNeeded();

    if (libraryDirty)
    {
        libraryDirty = false;
        publishLibrary();
    }

    // Générations automatiques toutes les N mesures
    const bool autoMode = proc.apvts.getRawParameterValue ("genMode")->load() > 0.5f;
    if (autoMode && proc.telemetry.playing.load())
    {
        static const int barsChoices[] = { 1, 2, 4, 8, 16 };
        const int bars = barsChoices[juce::jlimit (0, 4, (int) proc.apvts.getRawParameterValue ("genBars")->load())];
        const double beatsPerBar = juce::jmax (1, proc.telemetry.beatsPerBar.load());
        const auto bar = (int64_t) std::floor (proc.telemetry.ppq.load() / beatsPerBar);
        if (lastAutoBar < 0 || bar < lastAutoBar)
            lastAutoBar = bar;
        else if (bar - lastAutoBar >= bars && ! generating)
        {
            lastAutoBar = bar;
            newGeneration();
        }
    }
    else
        lastAutoBar = -1;

    // Copie de l'état pour la sauvegarde depuis un autre thread
    static juce::uint32 lastCache = 0;
    if (stateDirty && juce::Time::getMillisecondCounter() - lastCache > 2000)
    {
        lastCache = juce::Time::getMillisecondCounter();
        stateDirty = false;
        auto s = saveExtraState();
        const juce::ScopedLock sl (cacheLock);
        cachedExtra = s;
    }

    // Transactions d'annulation : une par geste
    static juce::uint32 lastTx = 0;
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastTx > 600)
    {
        lastTx = now;
        undoManager.beginNewTransaction();
    }
}

void Controller::updateCaptureRingIfNeeded()
{
    const int secs = juce::roundToInt (proc.apvts.getRawParameterValue ("captureSeconds")->load());
    if (lastCaptureSeconds < 0)
    {
        lastCaptureSeconds = secs;
        return;
    }
    if (secs == lastCaptureSeconds)
        return;

    auto old = proc.getRing();
    const double sr = old != nullptr ? old->getSampleRate() : proc.telemetry.sampleRate.load();
    const int frames = (int) (sr * secs);
    const int64_t delta = (int64_t) frames * 4 - (old != nullptr ? old->getBytes() : 0);
    if (delta > 0 && ! proc.budget.canAllocate (delta))
    {
        setStatus ("Memoire pleine : capture limitee a " + juce::String (lastCaptureSeconds) + " s");
        return;
    }
    lastCaptureSeconds = secs;
    proc.setRing (std::make_shared<CaptureRing> (proc.budget, frames, sr));
}

//==============================================================================
// Estomac
//==============================================================================
int Controller::firstFreeSlot() const
{
    for (int i = 0; i < kNumSlots; ++i)
        if (! slots[(size_t) i].used && ! slots[(size_t) i].busy)
            return i;
    return -1;
}

void Controller::swallow (int slot)
{
    if (slot < 0)
        slot = firstFreeSlot();
    if (slot < 0)
    {
        setStatus ("Les 8 sources sont pleines : vide une source d'abord");
        return;
    }

    auto ring = proc.getRing();
    if (ring == nullptr)
        return;

    // On libère l'ancienne source de ce slot avant d'en créer une nouvelle
    auto& s = slots[(size_t) slot];
    s.audio.reset();
    s.bands = {};
    s.frags.clear();
    s.busy = true;
    publishLibrary();

    const auto name = "Capture " + juce::Time::getCurrentTime().formatted ("%H:%M:%S");
    runJob ([this, ring, slot, name]
    {
        auto data = ring->snapshot (proc.budget, name);
        onMessageThread ([this, slot, name, data]() mutable
        {
            auto& s = slots[(size_t) slot];
            s.busy = false;
            if (data == nullptr)
            {
                setStatus ("Rien a avaler (capture vide) ou memoire pleine");
                return;
            }
            s.used = true;
            s.active = true;
            s.name = name;
            s.file = juce::File();
            s.audio = std::move (data);
            selectedSlot = slot;
            setStatus ("Avale : " + name);
            reslice (slot);
        });
    });
}

void Controller::loadFiles (const juce::StringArray& paths, int firstSlot)
{
    int slot = firstSlot >= 0 ? firstSlot : firstFreeSlot();
    for (const auto& path : paths)
    {
        const juce::File file (path);
        if (! file.existsAsFile())
            continue;
        if (slot < 0 || slot >= kNumSlots)
        {
            setStatus ("Plus de place : 8 sources maximum");
            break;
        }

        auto& s = slots[(size_t) slot];
        s.audio.reset();
        s.bands = {};
        s.frags.clear();
        s.busy = true;
        const int target = slot;

        runJob ([this, file, target]
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            std::shared_ptr<SampleData> data;
            juce::String error;
            if (reader == nullptr)
                error = "Format non reconnu : " + file.getFileName();
            else
            {
                // 60 s maximum par source : pas d'accumulation
                const int frames = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0));
                juce::AudioBuffer<float> buf (2, juce::jmax (1, frames));
                reader->read (&buf, 0, frames, 0, true, reader->numChannels > 1);
                if (reader->numChannels == 1)
                    buf.copyFrom (1, 0, buf, 0, 0, frames);
                data = SampleData::fromBuffer (proc.budget, buf, 0, frames, reader->sampleRate, file.getFileNameWithoutExtension());
                if (data == nullptr)
                    error = "Memoire pleine : impossible de charger " + file.getFileName();
                else if (reader->lengthInSamples > frames)
                    error = file.getFileName() + " tronque a 60 s";
            }

            onMessageThread ([this, target, file, data, error]() mutable
            {
                auto& s = slots[(size_t) target];
                s.busy = false;
                if (error.isNotEmpty())
                    setStatus (error);
                if (data == nullptr)
                    return;
                s.used = true;
                s.active = true;
                s.name = file.getFileNameWithoutExtension();
                s.file = file;
                s.audio = std::move (data);
                selectedSlot = target;
                reslice (target);
                stateDirty = true;
            });
        });

        // Slot suivant libre
        int next = -1;
        for (int i = slot + 1; i < kNumSlots; ++i)
            if (! slots[(size_t) i].used && ! slots[(size_t) i].busy) { next = i; break; }
        slot = next;
    }
}

void Controller::clearSlot (int slot)
{
    if (slot < 0 || slot >= kNumSlots)
        return;
    auto& s = slots[(size_t) slot];
    s = SourceSlot();
    s.colour = slotColour (slot);
    publishLibrary();
    stateDirty = true;
    sendChangeMessage();
}

void Controller::setSlotActive (int slot, bool a)  { slots[(size_t) slot].active = a; libraryDirty = true; stateDirty = true; }
void Controller::setSlotWeight (int slot, float w) { slots[(size_t) slot].weight = w; libraryDirty = true; stateDirty = true; }

void Controller::setSlotGain (int slot, float g)
{
    auto& s = slots[(size_t) slot];
    s.gain = g;
    for (auto& r : s.frags)
        r.f.gain = g;
    libraryDirty = true;
    stateDirty = true;
}

void Controller::setSlotSlice (int slot, const SliceSettings& ss)
{
    slots[(size_t) slot].slice = ss;
    stateDirty = true;
    reslice (slot);
}

void Controller::reslice (int slot)
{
    auto& s = slots[(size_t) slot];
    if (s.audio == nullptr)
        return;

    // Les bandes de l'ancien découpage sont libérées avant d'en créer de nouvelles
    s.bands = {};
    s.frags.clear();
    s.busy = true;
    publishLibrary();
    sendChangeMessage();

    const auto audio = s.audio;
    const auto settings_ = s.slice;
    const double bpm = proc.telemetry.bpm.load();
    const auto colour = s.colour;
    const float gain = s.gain;

    runJob ([this, slot, audio, settings_, bpm, colour, gain]
    {
        juce::Random rng ((juce::int64) audio->getNumFrames() * 31 + slot);
        std::vector<FragRec> frags;
        std::array<SampleDataPtr, 4> bands;
        juce::String error;

        if (settings_.mode == SliceSettings::Bands)
        {
            juce::AudioBuffer<float> buf (2, audio->getNumFrames());
            audio->copyTo (buf, 0, audio->getNumFrames());
            auto split = Slicer::splitBands (buf, audio->getSampleRate());
            for (int b = 0; b < 4; ++b)
            {
                auto band = SampleData::fromBuffer (proc.budget, split[(size_t) b], 0, buf.getNumSamples(),
                                                    audio->getSampleRate(), audio->getName() + " B" + juce::String (b + 1));
                if (band == nullptr)
                {
                    error = "Memoire pleine : bandes incompletes";
                    break;
                }
                auto mono = band->toMono (0, band->getNumFrames());
                auto regions = Slicer::slice (mono, band->getSampleRate(), bpm, settings_, 64, rng);
                auto recs = analyseRegions (band, regions, slot, 0, colour.withRotatedHue (0.04f * b), gain);
                frags.insert (frags.end(), recs.begin(), recs.end());
                bands[(size_t) b] = band;
            }
        }
        else
        {
            auto mono = audio->toMono (0, audio->getNumFrames());
            auto regions = Slicer::slice (mono, audio->getSampleRate(), bpm, settings_, 256, rng);
            frags = analyseRegions (audio, regions, slot, 0, colour, gain);
        }

        onMessageThread ([this, slot, frags = std::move (frags), bands, error, audio]() mutable
        {
            if (slots[(size_t) slot].audio != audio)
                return; // la source a changé entre-temps
            if (error.isNotEmpty())
                setStatus (error);
            finishSlice (slot, std::move (frags), bands);
        });
    });
}

std::vector<FragRec> Controller::analyseRegions (const SampleDataPtr& buf, const std::vector<SliceRegion>& regions,
                                                 int slot, int gen, juce::Colour colour, float gain)
{
    // Appelé depuis un thread de fond : n'utilise pas l'état du contrôleur sauf le compteur d'ids (atomique de fait : un seul worker)
    std::vector<FragRec> out;
    out.reserve (regions.size());
    for (const auto& r : regions)
    {
        if (r.length < 32)
            continue;
        FragRec rec;
        rec.buf = buf;
        rec.f.start = r.start;
        rec.f.length = r.length;
        rec.f.desc = Analyzer::analyse (buf->toMono (r.start, r.length), buf->getSampleRate());
        rec.f.norm = normalise (rec.f.desc);
        rec.f.slot = slot;
        rec.f.generation = gen;
        rec.f.colour = colour;
        rec.f.gain = gain;
        rec.f.id = newFragId();
        out.push_back (std::move (rec));
    }
    return out;
}

void Controller::finishSlice (int slot, std::vector<FragRec> frags, std::array<SampleDataPtr, 4> bands)
{
    auto& s = slots[(size_t) slot];
    s.busy = false;
    s.frags = std::move (frags);
    s.bands = bands;
    for (auto& r : s.frags)
        r.f.liked = likedIds.count (r.f.id) > 0;
    setStatus (s.name + " : " + juce::String ((int) s.frags.size()) + " fragments");
    publishLibrary();
}

//==============================================================================
// Bibliothèque
//==============================================================================
void Controller::publishLibrary()
{
    // 1. Rassembler les fragments : sources actives + génération courante
    std::vector<const FragRec*> slotRecs, genRecs;
    std::vector<float> slotW;
    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto& s = slots[(size_t) i];
        if (! s.used || ! s.active || s.busy)
            continue;
        for (const auto& r : s.frags)
        {
            slotRecs.push_back (&r);
            slotW.push_back (s.weight);
        }
    }
    const GenNode* node = nullptr;
    for (const auto& n : tree)
        if (n.id == currentNode) node = &n;
    if (node != nullptr)
        for (const auto& r : node->frags)
            genRecs.push_back (&r);

    // 2. Plafond de 512 fragments : générations d'abord (max 384), puis sources réparties
    if ((int) genRecs.size() > 384)
        genRecs.resize (384);
    const int room = kMaxFragments - (int) genRecs.size();
    std::vector<int> slotPick;
    if ((int) slotRecs.size() <= room)
        for (int i = 0; i < (int) slotRecs.size(); ++i) slotPick.push_back (i);
    else
    {
        const double step = (double) slotRecs.size() / juce::jmax (1, room);
        for (int i = 0; i < room; ++i) slotPick.push_back ((int) (i * step));
    }

    auto lib = std::make_shared<LibrarySnapshot>();
    lib->version = ++libraryVersion;
    lib->axisX = axisX;
    lib->axisY = axisY;

    std::map<const SampleData*, int> bufIndex;
    auto addRec = [&] (const FragRec& r, float w)
    {
        auto it = bufIndex.find (r.buf.get());
        int bi;
        if (it == bufIndex.end())
        {
            bi = (int) lib->buffers.size();
            lib->buffers.push_back (r.buf);
            bufIndex[r.buf.get()] = bi;
        }
        else bi = it->second;

        Fragment f = r.f;
        f.buffer = bi;
        f.liked = likedIds.count (f.id) > 0;
        lib->frags.push_back (f);
        lib->weights.push_back (w);
    };

    for (int i : slotPick)
        addRec (*slotRecs[(size_t) i], slotW[(size_t) i]);
    const int numSlotFrags = (int) lib->frags.size();
    for (auto* r : genRecs)
        addRec (*r, 1.0f);

    const int n = lib->size();

    // 3. Poids : zone peinte, likes, ADN
    double sumSlot = 0, sumGen = 0;
    for (int i = 0; i < n; ++i)
    {
        auto& f = lib->frags[(size_t) i];
        float w = lib->weights[(size_t) i];
        if (! maskEmpty)
        {
            const int mx = juce::jlimit (0, kMaskSize - 1, (int) (f.norm[(size_t) axisX] * kMaskSize));
            const int my = juce::jlimit (0, kMaskSize - 1, (int) ((1.0f - f.norm[(size_t) axisY]) * kMaskSize));
            w *= mask[(size_t) (my * kMaskSize + mx)];
        }
        if (f.liked)
            w *= 2.5f;
        lib->weights[(size_t) i] = w;
        (i < numSlotFrags ? sumSlot : sumGen) += w;
    }
    if (sumSlot > 0 && sumGen > 0)
    {
        const float dnaAmount = proc.apvts.getRawParameterValue ("dna")->load();
        const double ks = juce::jmax (0.02, (double) dnaAmount) / sumSlot;
        const double kg = juce::jmax (0.02, 1.0 - dnaAmount) / sumGen;
        for (int i = 0; i < n; ++i)
            lib->weights[(size_t) i] *= (float) (i < numSlotFrags ? ks : kg);
    }

    lib->cumulative.resize ((size_t) n);
    float acc = 0;
    for (int i = 0; i < n; ++i)
    {
        acc += lib->weights[(size_t) i];
        lib->cumulative[(size_t) i] = acc;
    }

    // 4. Voisins proches / éloignés (5 descripteurs)
    lib->near.assign ((size_t) n, {});
    lib->far.assign ((size_t) n, {});
    std::vector<std::pair<float, int>> d ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        for (int j = 0; j < n; ++j)
        {
            float dist = 0;
            for (int a = 0; a < kNumAxes; ++a)
            {
                const float diff = lib->frags[(size_t) i].norm[(size_t) a] - lib->frags[(size_t) j].norm[(size_t) a];
                dist += diff * diff;
            }
            d[(size_t) j] = { j == i ? 1.0e9f : dist, j };
        }
        const int k = juce::jmin (kNeighbours, n - 1);
        std::partial_sort (d.begin(), d.begin() + k, d.end());
        auto& nr = lib->near[(size_t) i];
        nr.fill (-1);
        for (int m = 0; m < k; ++m) nr[(size_t) m] = (int16_t) d[(size_t) m].second;

        for (auto& e : d) if (e.second == i) e.first = -1.0f;
        std::partial_sort (d.begin(), d.begin() + k, d.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        auto& fr = lib->far[(size_t) i];
        fr.fill (-1);
        for (int m = 0; m < k; ++m) fr[(size_t) m] = (int16_t) d[(size_t) m].second;
    }

    library = lib;
    proc.libraryBox.post (lib);
    stateDirty = true;
    sendChangeMessage();
}

//==============================================================================
// Carte
//==============================================================================
void Controller::paintMask (float x, float y, float radius, float amount)
{
    if (maskEmpty && amount > 0)
    {
        mask.fill (0.0f);
        maskEmpty = false;
    }
    if (maskEmpty)
        return;

    const float r = radius * kMaskSize;
    const float cx = x * kMaskSize, cy = y * kMaskSize;
    for (int j = juce::jmax (0, (int) (cy - r)); j <= juce::jmin (kMaskSize - 1, (int) (cy + r)); ++j)
        for (int i = juce::jmax (0, (int) (cx - r)); i <= juce::jmin (kMaskSize - 1, (int) (cx + r)); ++i)
        {
            const float dd = std::hypot (i + 0.5f - cx, j + 0.5f - cy) / juce::jmax (0.5f, r);
            if (dd <= 1.0f)
            {
                auto& m = mask[(size_t) (j * kMaskSize + i)];
                m = juce::jlimit (0.0f, 1.0f, m + amount * (1.0f - dd));
            }
        }

    bool any = false;
    for (float m : mask) if (m > 0.001f) { any = true; break; }
    if (! any)
        maskEmpty = true;
    libraryDirty = true;
    stateDirty = true;
}

void Controller::clearMask()
{
    mask.fill (0.0f);
    maskEmpty = true;
    publishLibrary();
}

void Controller::setAxes (int x, int y)
{
    axisX = juce::jlimit (0, kNumAxes - 1, x);
    axisY = juce::jlimit (0, kNumAxes - 1, y);
    clearMask(); // la zone peinte dépend des axes
}

void Controller::toggleLike (uint32_t id)
{
    if (likedIds.count (id) > 0) likedIds.erase (id);
    else likedIds.insert (id);
    publishLibrary();
}

void Controller::audition (int fragIndex)
{
    proc.commands.push ({ AudioCommand::Audition, fragIndex });
}

//==============================================================================
// Pattern, modulations
//==============================================================================
void Controller::publishPattern()
{
    proc.patternBox.post (std::make_shared<Pattern> (pattern));
    stateDirty = true;
}

void Controller::publishMods()
{
    proc.modBox.post (std::make_shared<ModSnapshot> (mods));
    stateDirty = true;
}

void Controller::randomizePattern (float amount, juce::Random& rng)
{
    for (auto& s : pattern.steps)
    {
        if (rng.nextFloat() > amount) continue;
        s.on = rng.nextFloat() > 0.2f;
        s.prob = juce::jlimit (0.1f, 1.0f, 0.4f + rng.nextFloat() * 0.8f);
        s.ratchet = rng.nextFloat() < 0.15f ? 2 + rng.nextInt (3) : 1;
        s.reverse = rng.nextFloat() < 0.2f ? rng.nextFloat() : 0.0f;
        s.pitch = rng.nextFloat() < 0.3f ? (float) (rng.nextInt (25) - 12) : 0.0f;
        s.length = rng.nextFloat() < 0.4f ? 0.1f + rng.nextFloat() * 0.9f : 1.0f;
    }
    publishPattern();
}

void Controller::addRoute (int source, int param, float depth)
{
    for (auto& r : mods.routes)
        if (r.source == source && r.param == param) { r.depth = depth; publishMods(); return; }
    for (auto& r : mods.routes)
        if (r.source < 0) { r = { source, param, depth }; publishMods(); return; }
    setStatus ("64 modulations maximum");
}

void Controller::removeRoutes (int param)
{
    for (auto& r : mods.routes)
        if (r.param == param) r = {};
    publishMods();
}

std::vector<ModRoute> Controller::routesFor (int param) const
{
    std::vector<ModRoute> out;
    for (const auto& r : mods.routes)
        if (r.param == param && r.source >= 0) out.push_back (r);
    return out;
}

void Controller::moveFx (int from, int to)
{
    auto& o = mods.fxOrder;
    if (from < 0 || from >= 7 || to < 0 || to >= 7 || from == to) return;
    const int v = o[(size_t) from];
    if (from < to) std::rotate (o.begin() + from, o.begin() + from + 1, o.begin() + to + 1);
    else           std::rotate (o.begin() + to, o.begin() + from, o.begin() + from + 1);
    o[(size_t) to] = v;
    publishMods();
}

//==============================================================================
// MIDI learn
//==============================================================================
void Controller::startMidiLearn (int param)
{
    proc.midiLearnTarget.store (param);
    setStatus ("MIDI Learn : bouge un controleur MIDI pour " + juce::String (P::info (param).name));
}

void Controller::clearMidiLearn (int param)
{
    for (auto& c : proc.ccMap)
        if (c.load() == param) c.store (-1);
}

int Controller::midiLearnCcFor (int param) const
{
    for (int cc = 0; cc < 128; ++cc)
        if (proc.ccMap[(size_t) cc].load() == param) return cc;
    return -1;
}

//==============================================================================
// Undo / A-B
//==============================================================================
void Controller::undo() { undoManager.undo(); }
void Controller::redo() { undoManager.redo(); }
void Controller::beginGesture() { undoManager.beginNewTransaction(); }

void Controller::toggleAB()
{
    auto current = proc.apvts.copyState();
    if (abOther.isValid())
        proc.apvts.replaceState (abOther.createCopy());
    abOther = current;
    abIsB = ! abIsB;
    sendChangeMessage();
}

void Controller::copyAtoB()
{
    abOther = proc.apvts.copyState();
    setStatus (abIsB ? "B copie dans A" : "A copie dans B");
}

//==============================================================================
juce::ValueTree Controller::getExtraStateForSaving()
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        auto s = saveExtraState();
        const juce::ScopedLock sl (cacheLock);
        cachedExtra = s;
        return s.createCopy();
    }
    const juce::ScopedLock sl (cacheLock);
    return cachedExtra.createCopy();
}

void Controller::restoreExtraStateAsync (const juce::ValueTree& t)
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        restoreExtraState (t);
    else
        onMessageThread ([this, t] { restoreExtraState (t); });
}
