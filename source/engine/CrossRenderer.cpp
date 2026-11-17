#include "CrossRenderer.h"

namespace CrossRenderer
{
    using Cpx = juce::dsp::Complex<float>;

    juce::AudioBuffer<float> extract (const SampleData& d, int start, int length, double sampleRate)
    {
        start = juce::jlimit (0, d.getNumFrames() - 1, start);
        length = juce::jlimit (1, d.getNumFrames() - start, length);
        const double ratio = d.getSampleRate() / sampleRate;
        const int outLen = juce::jmax (1, (int) (length / ratio));

        juce::AudioBuffer<float> out (2, outLen);
        for (int i = 0; i < outLen; ++i)
        {
            const double p = i * ratio;
            const int i0 = juce::jmin (length - 1, (int) p);
            const int i1 = juce::jmin (length - 1, i0 + 1);
            const float f = (float) (p - i0);
            out.setSample (0, i, d.getL (start + i0) + (d.getL (start + i1) - d.getL (start + i0)) * f);
            out.setSample (1, i, d.getR (start + i0) + (d.getR (start + i1) - d.getR (start + i0)) * f);
        }
        return out;
    }

    void normalise (juce::AudioBuffer<float>& b, float peakDb)
    {
        const float peak = b.getMagnitude (0, b.getNumSamples());
        if (peak > 1.0e-5f)
            b.applyGain (juce::Decibels::decibelsToGain (peakDb) / peak);
    }

    //==========================================================================
    struct Stft
    {
        static constexpr int order = 10, size = 1 << order, hop = size / 4;
        juce::dsp::FFT fft { order };
        std::vector<float> window;

        Stft()
        {
            window.resize (size);
            for (int i = 0; i < size; ++i)
                window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / size);
        }

        /** Spectres complets de chaque trame. */
        std::vector<std::vector<Cpx>> analyse (const float* x, int n)
        {
            const int frames = juce::jmax (1, (n + hop - 1) / hop);
            std::vector<std::vector<Cpx>> out ((size_t) frames, std::vector<Cpx> (size));
            std::vector<Cpx> in (size);
            for (int f = 0; f < frames; ++f)
            {
                for (int i = 0; i < size; ++i)
                {
                    const int idx = f * hop + i - size / 2;
                    in[(size_t) i] = Cpx (idx >= 0 && idx < n ? x[idx] * window[(size_t) i] : 0.0f, 0.0f);
                }
                fft.perform (in.data(), out[(size_t) f].data(), false);
            }
            return out;
        }

        void synthesise (const std::vector<std::vector<Cpx>>& spec, float* y, int n)
        {
            std::fill (y, y + n, 0.0f);
            std::vector<float> norm ((size_t) n, 0.0f);
            std::vector<Cpx> buf (size);
            for (size_t f = 0; f < spec.size(); ++f)
            {
                fft.perform (spec[f].data(), buf.data(), true);
                for (int i = 0; i < size; ++i)
                {
                    const int idx = (int) f * hop + i - size / 2;
                    if (idx >= 0 && idx < n)
                    {
                        y[idx] += buf[(size_t) i].real() * window[(size_t) i];
                        norm[(size_t) idx] += window[(size_t) i] * window[(size_t) i];
                    }
                }
            }
            for (int i = 0; i < n; ++i)
                if (norm[(size_t) i] > 1.0e-3f)
                    y[i] /= norm[(size_t) i];
        }
    };

    static std::vector<float> smoothMag (const std::vector<Cpx>& s, int radius)
    {
        const int n = (int) s.size();
        std::vector<float> m ((size_t) n), out ((size_t) n);
        for (int i = 0; i < n; ++i) m[(size_t) i] = std::abs (s[(size_t) i]);
        for (int i = 0; i < n; ++i)
        {
            float sum = 0; int c = 0;
            for (int k = juce::jmax (0, i - radius); k <= juce::jmin (n - 1, i + radius); ++k, ++c)
                sum += m[(size_t) k];
            out[(size_t) i] = sum / (float) c;
        }
        return out;
    }

    static juce::AudioBuffer<float> spectral (const juce::AudioBuffer<float>& A, const juce::AudioBuffer<float>& B, bool morph)
    {
        Stft stft;
        const int n = morph ? juce::jmax (A.getNumSamples(), B.getNumSamples()) : B.getNumSamples();
        juce::AudioBuffer<float> out (2, n);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto sa = stft.analyse (A.getReadPointer (ch), A.getNumSamples());
            auto sb = stft.analyse (B.getReadPointer (ch), B.getNumSamples());
            const int frames = juce::jmax (1, (n + Stft::hop - 1) / Stft::hop);
            std::vector<std::vector<Cpx>> so ((size_t) frames, std::vector<Cpx> (Stft::size));

            for (int f = 0; f < frames; ++f)
            {
                const float t = frames > 1 ? (float) f / (frames - 1) : 0.0f;
                const auto& fa = sa[(size_t) juce::jmin ((int) sa.size() - 1, (int) (t * (sa.size() - 1)))];
                const auto& fb = sb[(size_t) juce::jmin ((int) sb.size() - 1, (int) (t * (sb.size() - 1)))];
                auto& fo = so[(size_t) f];

                if (! morph)
                {
                    // Vocoder : l'enveloppe spectrale de A sculpte B
                    const auto ea = smoothMag (fa, 6);
                    const auto eb = smoothMag (fb, 6);
                    for (int k = 0; k < Stft::size; ++k)
                        fo[(size_t) k] = fb[(size_t) k] * (ea[(size_t) k] / (eb[(size_t) k] + 1.0e-4f));
                }
                else
                {
                    // Morph : amplitudes interpolées géométriquement, phase mélangée
                    for (int k = 0; k < Stft::size; ++k)
                    {
                        const float ma = std::abs (fa[(size_t) k]), mb = std::abs (fb[(size_t) k]);
                        const float mag = std::pow (ma + 1.0e-6f, 1.0f - t) * std::pow (mb + 1.0e-6f, t);
                        Cpx c = fa[(size_t) k] * (1.0f - t) + fb[(size_t) k] * t;
                        const float cm = std::abs (c);
                        fo[(size_t) k] = cm > 1.0e-9f ? c * (mag / cm) : Cpx (mag, 0.0f);
                    }
                }
            }
            stft.synthesise (so, out.getWritePointer (ch), n);
        }
        return out;
    }

    static juce::AudioBuffer<float> granular (const juce::AudioBuffer<float>& A, const juce::AudioBuffer<float>& B,
                                              double sr, juce::Random& rng)
    {
        const int n = juce::jmax (A.getNumSamples(), B.getNumSamples());
        juce::AudioBuffer<float> out (2, n);
        out.clear();

        int pos = 0;
        while (pos < n)
        {
            const int grain = (int) ((0.02 + rng.nextDouble() * 0.06) * sr);
            const auto& src = rng.nextBool() ? A : B;
            const int maxStart = juce::jmax (0, src.getNumSamples() - grain);
            const int s = maxStart > 0 ? rng.nextInt (maxStart) : 0;
            const int len = juce::jmin (grain, src.getNumSamples() - s);
            for (int i = 0; i < len && pos + i < n; ++i)
            {
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / juce::jmax (1, len));
                for (int ch = 0; ch < 2; ++ch)
                    out.addSample (ch, pos + i, src.getSample (ch, s + i) * w);
            }
            pos += juce::jmax (8, grain / 2);
        }
        return out;
    }

    static juce::AudioBuffer<float> collage (const juce::AudioBuffer<float>& A, const juce::AudioBuffer<float>& B, double sr)
    {
        const int ha = juce::jmax (1, A.getNumSamples() / 2);
        const int hb = juce::jmax (1, B.getNumSamples() / 2);
        const int xf = juce::jmin ((int) (0.01 * sr), ha, hb);
        const int n = ha + hb;
        juce::AudioBuffer<float> out (2, n);
        out.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < ha; ++i)
            {
                const float g = i > ha - xf ? (float) (ha - i) / xf : 1.0f;
                out.setSample (ch, i, A.getSample (ch, i) * g);
            }
            for (int i = 0; i < hb; ++i)
            {
                const float g = i < xf ? (float) i / xf : 1.0f;
                const int o = ha - xf + i;
                if (o >= 0 && o < n)
                    out.addSample (ch, o, B.getSample (ch, B.getNumSamples() - hb + i) * g);
            }
        }
        out.setSize (2, n - xf, true);
        return out;
    }

    juce::AudioBuffer<float> render (const SampleData& a, const Fragment& fa, const SampleData& b, const Fragment& fb,
                                     int mode, double sr, juce::Random& rng)
    {
        const int maxLen = (int) (4.0 * sr);
        auto A = extract (a, fa.start, fa.length, sr);
        auto B = extract (b, fb.start, fb.length, sr);
        if (A.getNumSamples() > maxLen) A.setSize (2, maxLen, true);
        if (B.getNumSamples() > maxLen) B.setSize (2, maxLen, true);

        juce::AudioBuffer<float> out;
        switch (mode)
        {
            case EnvelopeAonB: out = spectral (A, B, false); break;
            case Granular:     out = granular (A, B, sr, rng); break;
            case Morph:        out = spectral (A, B, true); break;
            default:           out = collage (A, B, sr); break;
        }

        // Fondus de sécurité
        const int f = juce::jmin (64, out.getNumSamples() / 4);
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
        {
            out.applyGainRamp (ch, 0, f, 0.0f, 1.0f);
            out.applyGainRamp (ch, out.getNumSamples() - f, f, 1.0f, 0.0f);
        }
        normalise (out, -1.0f);
        return out;
    }
}
