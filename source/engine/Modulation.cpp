#include "Modulation.h"

void ModEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    reset();
}

void ModEngine::reset()
{
    phase.fill (0.0);
    rndValue.fill (0.0f);
    rndTarget.fill (0.0f);
    rndCounter.fill (0.0);
    for (auto& s : sourceValues) s.store (0.0f);
    for (auto& p : lfoPhases) p.store (0.0f);
}

float ModEngine::readShape (const std::array<float, kLfoPoints>& s, double ph) noexcept
{
    ph -= std::floor (ph);
    const double x = ph * kLfoPoints;
    const int i0 = (int) x % kLfoPoints;
    const int i1 = (i0 + 1) % kLfoPoints;
    const float f = (float) (x - std::floor (x));
    return s[(size_t) i0] + (s[(size_t) i1] - s[(size_t) i0]) * f;
}

void ModEngine::compute (const P::Values& base, const ModSnapshot& mods, const Transport& t, int numSamples,
                         P::Values& out, float& filterCutoffRight)
{
    static const double divBeats[] = { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25 };

    std::array<float, numModSources> src {};
    std::array<float, kNumLfos> lfoRight {};

    // --- LFO ---------------------------------------------------------------------------
    for (int l = 0; l < kNumLfos; ++l)
    {
        const int o = l * 4;
        const float rate   = base[(size_t) (P::lfo1Rate + o)];
        const bool sync    = base[(size_t) (P::lfo1Sync + o)] > 0.5f;
        const int div      = juce::jlimit (0, 6, (int) base[(size_t) (P::lfo1Div + o)]);
        const float stereo = base[(size_t) (P::lfo1Stereo + o)];

        if (sync)
            phase[(size_t) l] = t.ppq / divBeats[div];
        else
            phase[(size_t) l] += rate * numSamples / sr;
        phase[(size_t) l] -= std::floor (phase[(size_t) l]);

        const auto& shape = mods.shapes[(size_t) l];
        src[(size_t) l] = readShape (shape, phase[(size_t) l]) * 2.0f - 1.0f;
        lfoRight[(size_t) l] = readShape (shape, phase[(size_t) l] + stereo * 0.5) * 2.0f - 1.0f;
        lfoPhases[(size_t) l].store ((float) phase[(size_t) l], std::memory_order_relaxed);
    }

    // --- Random (sample & hold / lissé) -------------------------------------------------
    for (int r = 0; r < kNumRnd; ++r)
    {
        const float rate   = base[(size_t) (P::rnd1Rate + r * 2)];
        const float smooth = base[(size_t) (P::rnd1Smooth + r * 2)];
        rndCounter[(size_t) r] += rate * numSamples / sr;
        if (rndCounter[(size_t) r] >= 1.0)
        {
            rndCounter[(size_t) r] -= std::floor (rndCounter[(size_t) r]);
            rndTarget[(size_t) r] = rng.nextFloat() * 2.0f - 1.0f;
        }
        if (smooth <= 0.001f)
            rndValue[(size_t) r] = rndTarget[(size_t) r];
        else
        {
            const float coef = 1.0f - std::exp (-(float) numSamples / (float) (sr * 0.5 * smooth / juce::jmax (0.01f, rate)));
            rndValue[(size_t) r] += (rndTarget[(size_t) r] - rndValue[(size_t) r]) * coef;
        }
        src[(size_t) (srcRnd1 + r)] = rndValue[(size_t) r];
    }

    // --- Macros ------------------------------------------------------------------------
    for (int m = 0; m < kNumMacros; ++m)
        src[(size_t) (srcMacro1 + m)] = base[(size_t) (P::macro1 + m)];

    for (int s = 0; s < numModSources; ++s)
        sourceValues[(size_t) s].store (src[(size_t) s], std::memory_order_relaxed);

    // --- Somme des modulations (en normalisé) ---------------------------------------------
    std::array<float, P::count> modNorm {};
    float cutoffRightMod = 0.0f;

    for (const auto& route : mods.routes)
    {
        if (route.source < 0 || route.param < 0 || route.param >= P::count)
            continue;
        modNorm[(size_t) route.param] += route.depth * src[(size_t) route.source];
        if (route.param == P::filtCutoff)
            cutoffRightMod += route.depth * (route.source < kNumLfos ? lfoRight[(size_t) route.source]
                                                                    : src[(size_t) route.source]);
    }

    // --- Macro Chaos : pousse mutation, contraste, random et FX ---------------------------
    const float chaos = base[(size_t) P::chaos];
    if (chaos > 0.0f)
    {
        modNorm[(size_t) P::mutation]     += 0.7f * chaos;
        modNorm[(size_t) P::ruleContrast] += 0.8f * chaos;
        modNorm[(size_t) P::ruleRandom]   += 0.4f * chaos;
        modNorm[(size_t) P::randomAmount] += 0.5f * chaos;
        modNorm[(size_t) P::death]        += 0.4f * chaos;
        modNorm[(size_t) P::seqReverse]   += 0.4f * chaos;
        modNorm[(size_t) P::stutChance]   += 0.6f * chaos;
        for (int p : { (int) P::crushMix, (int) P::distMix, (int) P::shiftMix, (int) P::stutMix,
                       (int) P::dlyMix, (int) P::verbMix, (int) P::distDrive })
            modNorm[(size_t) p] += 0.5f * chaos;
    }

    for (int i = 0; i < P::count; ++i)
    {
        const auto& inf = P::info (i);
        if (inf.isContinuous() && modNorm[(size_t) i] != 0.0f)
            out[(size_t) i] = inf.fromNorm (inf.toNorm (base[(size_t) i]) + modNorm[(size_t) i]);
        else
            out[(size_t) i] = base[(size_t) i];
    }

    const auto& cInfo = P::info (P::filtCutoff);
    filterCutoffRight = cInfo.fromNorm (cInfo.toNorm (base[(size_t) P::filtCutoff]) + cutoffRightMod);
}
