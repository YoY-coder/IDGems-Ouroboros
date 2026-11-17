#include "EngineCore.h"

namespace
{
    /** Hash entier -> [0,1) : décisions déterministes (rejouables avec la même seed). */
    inline float hash01 (uint64_t x) noexcept
    {
        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return (float) ((x >> 40) & 0xFFFFFF) / 16777216.0f;
    }

    const double kStepBeats[] = { 1.0, 0.5, 0.25, 0.125 };
}

EngineCore::EngineCore()
{
    for (auto& v : voiceFrags)
        v.store (-1);
    pattern = std::make_shared<Pattern>();
    mods = std::make_shared<ModSnapshot>();
    library = std::make_shared<LibrarySnapshot>();
    values = P::defaults();
}

void EngineCore::prepare (double sampleRate, int block)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, block);
    mod.prepare (sr);
    fx.prepare (sr, maxBlock);
    reset();
}

void EngineCore::reset()
{
    for (auto& v : voices)
        releaseVoice (v);
    fx.reset();
    internalPpq = 0.0;
    wasRunning = false;
    lastStepAbs = -1;
}

void EngineCore::panic()
{
    for (auto& v : voices)
        releaseVoice (v);
    fx.reset();
}

void EngineCore::releaseVoice (Voice& v)
{
    v.active = false;
    v.fragIndex = -1;
    if (v.data != nullptr)
    {
        if (trash != nullptr)
            trash->dispose (std::move (v.data));
        else
            v.data.reset();
    }
}

void EngineCore::process (float* L, float* R, int n, const P::Values& base, const Transport& transport,
                          const juce::MidiBuffer* midi)
{
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);

    // Transport : hôte en lecture, ou lecture libre interne au tempo de l'hôte
    Transport t = transport;
    const bool freeRun = base[P::seqRun] > 0.5f;
    if (! t.playing && freeRun)
    {
        if (! wasRunning)
            internalPpq = 0.0;
        t.ppq = internalPpq;
        t.playing = true;
        internalPpq += n * t.bpm / 60.0 / sr;
    }
    if (! t.playing && wasRunning)
        currentStep.store (-1, std::memory_order_relaxed);
    wasRunning = t.playing;

    mod.compute (base, *mods, t, n, values, cutoffRight);

    if (t.playing)
        runSequencer (n, t);
    if (midi != nullptr)
        handleMidi (*midi, n);

    renderVoices (L, R, n);

    // Le rack FX traite par morceaux de maxBlock
    for (int done = 0; done < n;)
    {
        const int num = juce::jmin (maxBlock, n - done);
        fx.process (L + done, R + done, num, values, mods->fxOrder, t.bpm, cutoffRight);
        done += num;
    }

    for (int i = 0; i < kMaxVoices; ++i)
        voiceFrags[(size_t) i].store (voices[(size_t) i].active ? voices[(size_t) i].fragIndex : -1,
                                      std::memory_order_relaxed);
}

void EngineCore::runSequencer (int n, const Transport& t)
{
    const int div = juce::jlimit (0, 3, (int) values[P::seqDivision]);
    const double stepPpq = kStepBeats[div];
    const double ppqStart = t.ppq;
    const double ppqPerSample = t.bpm / 60.0 / sr;
    const double ppqEnd = ppqStart + n * ppqPerSample;
    const double swing = values[P::swing];
    const int length = juce::jlimit (1, kMaxSteps, (int) values[P::seqLength]);

    const int64_t kFirst = (int64_t) std::floor (ppqStart / stepPpq) - 1;
    const int64_t kLast  = (int64_t) std::floor (ppqEnd / stepPpq) + 1;

    for (int64_t k = kFirst; k <= kLast; ++k)
    {
        if (k < 0)
            continue;
        const double base = k * stepPpq + ((k & 1) ? swing * stepPpq * 0.5 : 0.0);
        const auto& step = pattern->steps[(size_t) (k % length)];
        const int ratchets = juce::jlimit (1, 4, step.ratchet);

        for (int j = 0; j < ratchets; ++j)
        {
            const double tt = base + j * stepPpq / ratchets;
            if (tt >= ppqStart && tt < ppqEnd)
            {
                const int offset = juce::jlimit (0, n - 1, (int) ((tt - ppqStart) / ppqPerSample));
                if (j == 0)
                    currentStep.store ((int) (k % length), std::memory_order_relaxed);
                triggerStep (k, j, offset);
            }
        }
    }
}

void EngineCore::triggerStep (int64_t stepAbs, int sub, int offset)
{
    const int length = juce::jlimit (1, kMaxSteps, (int) values[P::seqLength]);
    const auto& step = pattern->steps[(size_t) (stepAbs % length)];
    if (! step.on || library->size() == 0)
        return;

    const uint64_t h = (uint64_t) stepAbs * 2654435761ULL + runSeed * 97ULL;
    if (hash01 (h) >= step.prob)
        return;

    // Un ratchet rejoue le même fragment que le début du pas
    int frag;
    if (sub > 0 && pickedForStep == stepAbs)
        frag = pickedFragForStep;
    else
    {
        frag = pickFragment (lastPicked);
        pickedForStep = stepAbs;
        pickedFragForStep = frag;
    }
    if (frag < 0)
        return;
    lastPicked = frag;

    const bool rev = hash01 (h + 17) < step.reverse || rng.nextFloat() < values[P::seqReverse];
    const float pitch = step.pitch + values[P::seqPitch];
    const float len = juce::jlimit (0.02f, 1.0f, step.length * values[P::seqLengthScale]);
    startVoice (frag, offset, pitch, rev, len, sub > 0 ? 0.85f : 1.0f);
}

void EngineCore::handleMidi (const juce::MidiBuffer& midi, int n)
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isNoteOn() || library->size() == 0)
            continue;

        const float vel = m.getFloatVelocity();
        const float sens = values[P::velocitySens];
        const float gain = (1.0f - sens) + sens * vel;
        const int offset = juce::jlimit (0, n - 1, meta.samplePosition);

        int frag;
        float pitch = values[P::seqPitch];
        if ((int) values[P::midiMode] == 0)
        {
            // Clavier = carte : la note choisit la position X, la vélocité la position Y
            const float x = juce::jlimit (0.0f, 1.0f, (m.getNoteNumber() - 36) / 60.0f);
            frag = pickNearMap (x, vel);
        }
        else
        {
            frag = pickFragment (lastPicked);
            pitch += (float) (m.getNoteNumber() - 60);
        }

        if (frag >= 0)
        {
            lastPicked = frag;
            startVoice (frag, offset, pitch, false, values[P::seqLengthScale], gain);
        }
    }
}

int EngineCore::pickFragment (int prev)
{
    const auto& lib = *library;
    const int num = lib.size();
    if (num == 0 || lib.cumulative.empty())
        return -1;

    const float wr = values[P::ruleRandom], ws = values[P::ruleSimilar], wc = values[P::ruleContrast];
    const float total = wr + ws + wc;
    float r = rng.nextFloat() * (total > 0 ? total : 1.0f);
    int rule = 0;
    if (total > 0 && prev >= 0 && prev < num)
        rule = r < wr ? 0 : (r < wr + ws ? 1 : 2);

    if (rule != 0)
    {
        const auto& cands = rule == 1 ? lib.near[(size_t) prev] : lib.far[(size_t) prev];
        float sum = 0;
        for (auto c : cands)
            if (c >= 0) sum += lib.weights[(size_t) c] + 1.0e-4f;
        if (sum > 0)
        {
            float x = rng.nextFloat() * sum;
            for (auto c : cands)
            {
                if (c < 0) continue;
                x -= lib.weights[(size_t) c] + 1.0e-4f;
                if (x <= 0) return c;
            }
        }
    }

    // Tirage pondéré (zone peinte, poids des sources, ADN)
    const float tot = lib.cumulative.back();
    if (tot <= 0)
        return rng.nextInt (num);
    const float x = rng.nextFloat() * tot;
    auto it = std::upper_bound (lib.cumulative.begin(), lib.cumulative.end(), x);
    return juce::jlimit (0, num - 1, (int) std::distance (lib.cumulative.begin(), it));
}

int EngineCore::pickNearMap (float x, float y)
{
    const auto& lib = *library;
    const int ax = juce::jlimit (0, kNumAxes - 1, lib.axisX);
    const int ay = juce::jlimit (0, kNumAxes - 1, lib.axisY);

    // Les 3 plus proches, puis un au hasard parmi eux
    std::array<int, 3> best { -1, -1, -1 };
    std::array<float, 3> bestD { 1e9f, 1e9f, 1e9f };
    for (int i = 0; i < lib.size(); ++i)
    {
        if (lib.weights[(size_t) i] <= 0.0f)
            continue;
        const float dx = lib.frags[(size_t) i].norm[(size_t) ax] - x;
        const float dy = lib.frags[(size_t) i].norm[(size_t) ay] - y;
        const float d = dx * dx + dy * dy;
        for (int k = 0; k < 3; ++k)
        {
            if (d < bestD[(size_t) k])
            {
                for (int m = 2; m > k; --m) { bestD[(size_t) m] = bestD[(size_t) m - 1]; best[(size_t) m] = best[(size_t) m - 1]; }
                bestD[(size_t) k] = d;
                best[(size_t) k] = i;
                break;
            }
        }
    }
    int count = 0;
    for (auto b : best) if (b >= 0) ++count;
    return count > 0 ? best[(size_t) rng.nextInt (count)] : -1;
}

void EngineCore::audition (int fragIndex)
{
    if (fragIndex >= 0 && fragIndex < library->size())
        startVoice (fragIndex, 0, 0.0f, false, 1.0f, 1.0f);
}

void EngineCore::startVoice (int frag, int offset, float pitchSemis, bool reverse, float lengthFrac, float gain)
{
    const auto& lib = *library;
    if (frag < 0 || frag >= lib.size())
        return;
    const auto& f = lib.frags[(size_t) frag];
    const auto& buf = lib.buffers[(size_t) f.buffer];
    if (buf == nullptr || f.length < 16)
        return;

    // Voix libre, sinon on vole celle qui a le moins de temps restant
    Voice* v = nullptr;
    for (auto& cand : voices)
        if (! cand.active) { v = &cand; break; }
    if (v == nullptr)
    {
        v = &voices[0];
        for (auto& cand : voices)
            if (cand.outTotal - cand.outDone < v->outTotal - v->outDone)
                v = &cand;
        releaseVoice (*v);
    }

    v->data = buf; // copie du shared_ptr : incrément atomique, pas d'allocation
    v->fragIndex = frag;
    v->start = f.start;
    v->length = juce::jmin (f.length, buf->getNumFrames() - f.start);
    v->inc = std::pow (2.0, pitchSemis / 12.0) * buf->getSampleRate() / sr;
    v->pos = 0.0;
    v->outTotal = juce::jmax (16, (int) (v->length * juce::jlimit (0.02f, 1.0f, lengthFrac) / v->inc));
    v->outDone = 0;
    v->fade = juce::jmax (8, juce::jmin ((int) (0.003 * sr), v->outTotal / 4));
    v->delay = offset;
    v->reverse = reverse;
    const float g = f.gain * gain * juce::Decibels::decibelsToGain (values[P::engineLevel]);
    v->gainL = g;
    v->gainR = g;
    v->active = true;

    lastFragment.store (frag, std::memory_order_relaxed);
    triggerCounter.fetch_add (1, std::memory_order_relaxed);
}

void EngineCore::renderVoices (float* L, float* R, int n)
{
    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        const SampleData& d = *v.data;
        const int last = v.length - 2;

        for (int i = 0; i < n; ++i)
        {
            if (v.delay > 0) { --v.delay; continue; }
            if (v.outDone >= v.outTotal || v.pos >= last)
            {
                releaseVoice (v);
                break;
            }

            float env = 1.0f;
            if (v.outDone < v.fade) env = (float) v.outDone / v.fade;
            const int left = v.outTotal - v.outDone;
            if (left < v.fade) env = juce::jmin (env, (float) left / v.fade);

            const int ip = (int) v.pos;
            const float fr = (float) (v.pos - ip);
            int a, b;
            if (v.reverse) { a = v.start + v.length - 1 - ip; b = juce::jmax (v.start, a - 1); }
            else           { a = v.start + ip; b = a + 1; }

            const float sl = d.getL (a) + (d.getL (b) - d.getL (a)) * fr;
            const float sr_ = d.getR (a) + (d.getR (b) - d.getR (a)) * fr;
            L[i] += sl * env * v.gainL;
            R[i] += sr_ * env * v.gainR;

            v.pos += v.inc;
            ++v.outDone;
        }
    }
}
