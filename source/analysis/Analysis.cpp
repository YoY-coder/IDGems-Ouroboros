#include "Analysis.h"

namespace Slicer
{
    static std::vector<SliceRegion> fromBoundaries (std::vector<int> b, int total, int minLen)
    {
        std::sort (b.begin(), b.end());
        b.erase (std::unique (b.begin(), b.end()), b.end());
        if (b.empty() || b.front() != 0)
            b.insert (b.begin(), 0);

        std::vector<SliceRegion> out;
        for (size_t i = 0; i < b.size(); ++i)
        {
            const int s = b[i];
            const int e = (i + 1 < b.size()) ? b[i + 1] : total;
            if (e - s >= minLen)
                out.push_back ({ s, e - s });
            else if (! out.empty())
                out.back().length += e - s; // trop court : on l'ajoute au précédent
        }
        return out;
    }

    static std::vector<SliceRegion> transients (const std::vector<float>& x, double sr, const SliceSettings& s)
    {
        // Enveloppe d'énergie par trames de 256, détection de montées (flux positif).
        const int hop = 256;
        const int frames = (int) x.size() / hop;
        if (frames < 4)
            return { { 0, (int) x.size() } };

        std::vector<float> env ((size_t) frames);
        for (int f = 0; f < frames; ++f)
        {
            float e = 0;
            for (int i = 0; i < hop; ++i)
                e += x[(size_t) (f * hop + i)] * x[(size_t) (f * hop + i)];
            env[(size_t) f] = std::sqrt (e / hop);
        }

        std::vector<float> flux ((size_t) frames, 0.0f);
        float maxFlux = 1.0e-9f;
        for (int f = 1; f < frames; ++f)
        {
            const float a = juce::Decibels::gainToDecibels (env[(size_t) f], -80.0f);
            const float b = juce::Decibels::gainToDecibels (env[(size_t) f - 1], -80.0f);
            flux[(size_t) f] = juce::jmax (0.0f, a - b) * juce::jlimit (0.0f, 1.0f, env[(size_t) f] * 20.0f);
            maxFlux = juce::jmax (maxFlux, flux[(size_t) f]);
        }

        // Seuil adaptatif : moyenne locale × (1 + seuil), et seuil absolu relatif au max.
        const float absThresh = maxFlux * juce::jmap (s.threshold, 0.02f, 0.6f);
        const int win = juce::jmax (2, (int) juce::jmap (s.sensitivity, 12.0f, 2.0f));
        const int minGapFrames = juce::jmax (1, (int) (juce::jmap (s.sensitivity, 0.12f, 0.03f) * sr / hop));

        std::vector<int> bounds;
        int last = -minGapFrames;
        for (int f = 1; f < frames - 1; ++f)
        {
            float mean = 0;
            int n = 0;
            for (int k = juce::jmax (0, f - win); k <= juce::jmin (frames - 1, f + win); ++k, ++n)
                mean += flux[(size_t) k];
            mean /= (float) juce::jmax (1, n);

            const bool peak = flux[(size_t) f] >= flux[(size_t) f - 1] && flux[(size_t) f] > flux[(size_t) f + 1];
            if (peak && flux[(size_t) f] > absThresh && flux[(size_t) f] > mean * 1.3f && f - last >= minGapFrames)
            {
                bounds.push_back (f * hop);
                last = f;
            }
        }
        return fromBoundaries (bounds, (int) x.size(), (int) (0.02 * sr));
    }

    static std::vector<SliceRegion> grid (int total, double sr, double bpm, int division)
    {
        const double beats = 4.0 / std::pow (2.0, juce::jlimit (0, 4, division) + 2) * 1.0; // 1/4 -> 1 temps
        const double len = juce::jmax (64.0, beats * 60.0 / juce::jmax (20.0, bpm) * sr);
        std::vector<int> b;
        for (double p = 0; p < total; p += len)
            b.push_back ((int) p);
        return fromBoundaries (b, total, 32);
    }

    static std::vector<SliceRegion> random (int total, double sr, const SliceSettings& s, juce::Random& rng)
    {
        const double mn = juce::jmax (5.0f, juce::jmin (s.randomMinMs, s.randomMaxMs)) * 0.001 * sr;
        const double mx = juce::jmax (s.randomMinMs, s.randomMaxMs) * 0.001 * sr;
        std::vector<int> b;
        double p = 0;
        while (p < total)
        {
            b.push_back ((int) p);
            p += mn + rng.nextDouble() * (mx - mn);
        }
        return fromBoundaries (b, total, 32);
    }

    std::vector<SliceRegion> slice (const std::vector<float>& mono, double sr, double bpm,
                                    const SliceSettings& s, int maxSlices, juce::Random& rng)
    {
        std::vector<SliceRegion> r;
        switch (s.mode)
        {
            case SliceSettings::Transients: r = transients (mono, sr, s); break;
            case SliceSettings::Grid:       r = grid ((int) mono.size(), sr, bpm, s.gridDivision); break;
            case SliceSettings::Random:     r = random ((int) mono.size(), sr, s, rng); break;
            case SliceSettings::Bands:      r = transients (mono, sr, s); break; // chaque bande est découpée aux transitoires
            default: break;
        }

        // Les fragments très longs (> 4 s) sont recoupés.
        std::vector<SliceRegion> out;
        const int maxLen = (int) (4.0 * sr);
        for (auto reg : r)
        {
            while (reg.length > maxLen)
            {
                out.push_back ({ reg.start, maxLen });
                reg.start += maxLen;
                reg.length -= maxLen;
            }
            out.push_back (reg);
        }

        // Plafond : on garde un sous-ensemble régulier.
        if ((int) out.size() > maxSlices && maxSlices > 0)
        {
            std::vector<SliceRegion> kept;
            const double step = (double) out.size() / maxSlices;
            for (int i = 0; i < maxSlices; ++i)
                kept.push_back (out[(size_t) (i * step)]);
            out = std::move (kept);
        }

        snapToZeroCrossings (mono, out, sr);
        return out;
    }

    void snapToZeroCrossings (const std::vector<float>& x, std::vector<SliceRegion>& regions, double sr)
    {
        const int range = (int) (0.002 * sr);
        const int n = (int) x.size();
        auto snap = [&] (int pos)
        {
            for (int d = 0; d <= range; ++d)
            {
                for (int sgn : { -1, 1 })
                {
                    const int p = pos + d * sgn;
                    if (p > 0 && p < n && (x[(size_t) p - 1] <= 0.0f) != (x[(size_t) p] <= 0.0f))
                        return p;
                }
            }
            return pos;
        };

        for (auto& r : regions)
        {
            const int s = snap (r.start);
            const int e = snap (juce::jmin (n, r.start + r.length));
            if (e - s > 32)
            {
                r.start = s;
                r.length = e - s;
            }
        }
    }

    std::array<juce::AudioBuffer<float>, 4> splitBands (const juce::AudioBuffer<float>& in, double sr)
    {
        const float freqs[3] = { 200.0f, 1200.0f, 5000.0f };
        std::array<juce::AudioBuffer<float>, 4> bands;
        for (auto& b : bands)
            b.setSize (2, in.getNumSamples());

        juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, in.getNumSamples()), 2 };
        std::array<juce::dsp::LinkwitzRileyFilter<float>, 3> xo;
        for (int i = 0; i < 3; ++i)
        {
            xo[(size_t) i].setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
            xo[(size_t) i].setCutoffFrequency (freqs[i]);
            xo[(size_t) i].prepare (spec);
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = in.getReadPointer (juce::jmin (ch, in.getNumChannels() - 1));
            for (int i = 0; i < in.getNumSamples(); ++i)
            {
                float rest = src[i];
                for (int b = 0; b < 3; ++b)
                {
                    float lo, hi;
                    xo[(size_t) b].processSample (ch, rest, lo, hi);
                    bands[(size_t) b].setSample (ch, i, lo);
                    rest = hi;
                }
                bands[3].setSample (ch, i, rest);
            }
        }
        return bands;
    }
}

//==============================================================================
namespace Analyzer
{
    static float yinPitch (const float* x, int n, double sr)
    {
        // YIN simplifié sur une fenêtre ; retourne -1 si pas de hauteur claire.
        const int maxLag = juce::jmin (n / 2, (int) (sr / 60.0));
        const int minLag = juce::jmax (2, (int) (sr / 1500.0));
        if (maxLag <= minLag + 2)
            return -1.0f;

        const int w = n - maxLag;
        std::vector<float> d ((size_t) maxLag + 1, 0.0f);
        for (int lag = 1; lag <= maxLag; ++lag)
        {
            float s = 0;
            for (int i = 0; i < w; ++i)
            {
                const float diff = x[i] - x[i + lag];
                s += diff * diff;
            }
            d[(size_t) lag] = s;
        }

        // Différence moyenne cumulée normalisée
        float running = 0;
        d[0] = 1.0f;
        for (int lag = 1; lag <= maxLag; ++lag)
        {
            running += d[(size_t) lag];
            d[(size_t) lag] = running > 0 ? d[(size_t) lag] * lag / running : 1.0f;
        }

        for (int lag = minLag; lag < maxLag; ++lag)
        {
            if (d[(size_t) lag] < 0.15f)
            {
                while (lag + 1 < maxLag && d[(size_t) lag + 1] < d[(size_t) lag])
                    ++lag;
                // interpolation parabolique
                const float a = d[(size_t) lag - 1], b = d[(size_t) lag], c = d[(size_t) lag + 1];
                const float denom = a - 2 * b + c;
                const float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;
                const double f0 = sr / (lag + shift);
                return (float) (69.0 + 12.0 * std::log2 (f0 / 440.0));
            }
        }
        return -1.0f;
    }

    Descriptors analyse (const std::vector<float>& x, double sr)
    {
        Descriptors d;
        const int n = (int) x.size();
        d.lengthSec = (float) (n / sr);
        if (n < 16)
            return d;

        float peak = 0, sum = 0;
        for (float v : x)
        {
            peak = juce::jmax (peak, std::abs (v));
            sum += v * v;
        }
        d.peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
        d.rmsDb  = juce::Decibels::gainToDecibels (std::sqrt (sum / n), -100.0f);

        // Spectre moyen sur jusqu'à 8 fenêtres de 2048
        constexpr int order = 11, size = 1 << order;
        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> window (size, juce::dsp::WindowingFunction<float>::hann);
        std::vector<float> mag (size / 2, 0.0f);
        std::vector<float> buf (size * 2, 0.0f);

        const int numWin = juce::jlimit (1, 8, n / size);
        const int stride = numWin > 1 ? (n - size) / (numWin - 1) : 0;
        for (int w = 0; w < numWin; ++w)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            const int start = w * stride;
            const int len = juce::jmin (size, n - start);
            std::copy (x.begin() + start, x.begin() + start + len, buf.begin());
            window.multiplyWithWindowingTable (buf.data(), size);
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (int k = 0; k < size / 2; ++k)
                mag[(size_t) k] += buf[(size_t) k];
        }

        double num = 0, den = 0, logSum = 0, linSum = 0;
        int bins = 0;
        for (int k = 1; k < size / 2; ++k)
        {
            const double m = mag[(size_t) k] / numWin + 1.0e-12;
            const double f = k * sr / size;
            num += f * m;
            den += m;
            logSum += std::log (m);
            linSum += m;
            ++bins;
        }
        d.centroidHz = den > 0 ? (float) (num / den) : 0.0f;
        d.flatness = bins > 0 && linSum > 0
                         ? (float) juce::jlimit (0.0, 1.0, std::exp (logSum / bins) / (linSum / bins))
                         : 0.0f;

        // Hauteur : sur la zone la plus forte, 4096 échantillons max, seulement si peu bruité
        if (d.flatness < 0.35f && d.rmsDb > -50.0f)
        {
            const int win = juce::jmin (n, 2048);
            int best = 0;
            float bestE = -1;
            for (int s = 0; s + win <= n; s += win / 2)
            {
                float e = 0;
                for (int i = 0; i < win; i += 8)
                    e += x[(size_t) (s + i)] * x[(size_t) (s + i)];
                if (e > bestE) { bestE = e; best = s; }
            }
            d.pitchMidi = yinPitch (x.data() + best, win, sr);
        }
        return d;
    }
}
