#include "FxRack.h"

const char* FxRack::fxName (int i)
{
    static const char* names[] = { "CRUSH", "DIST", "FILTRE", "SHIFT", "STUTTER", "DELAY", "REVERB" };
    return names[juce::jlimit (0, kNumFx - 1, i)];
}

void FxRack::prepare (double sampleRate, int block)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, block);
    tmpL.assign ((size_t) maxBlock, 0.0f);
    tmpR.assign ((size_t) maxBlock, 0.0f);

    juce::dsp::ProcessSpec mono { sr, (juce::uint32) maxBlock, 1 };
    filtL.prepare (mono);
    filtR.prepare (mono);

    psBufL.assign (4096, 0.0f);
    psBufR.assign (4096, 0.0f);

    static const float coefA[] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
    static const float coefB[] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 4; ++i)
        {
            hilA[(size_t) c][(size_t) i].a2 = coefA[i] * coefA[i];
            hilB[(size_t) c][(size_t) i].a2 = coefB[i] * coefB[i];
        }

    stBufL.assign ((size_t) (sr * 2.0), 0.0f);
    stBufR.assign ((size_t) (sr * 2.0), 0.0f);

    dlBufL.assign ((size_t) (sr * 4.0), 0.0f);
    dlBufR.assign ((size_t) (sr * 4.0), 0.0f);
    dlTime.reset (sr, 0.08);

    verb.setSampleRate (sr);
    reset();
}

void FxRack::reset()
{
    crushHoldL = crushHoldR = crushCounter = 0;
    filtL.reset();
    filtR.reset();
    std::fill (psBufL.begin(), psBufL.end(), 0.0f);
    std::fill (psBufR.begin(), psBufR.end(), 0.0f);
    for (auto& ch : hilA) for (auto& a : ch) a = { a.a2 };
    for (auto& ch : hilB) for (auto& a : ch) a = { a.a2 };
    hilDelay.fill (0.0f);
    std::fill (stBufL.begin(), stBufL.end(), 0.0f);
    std::fill (stBufR.begin(), stBufR.end(), 0.0f);
    stRemaining = 0;
    stPos = 0;
    std::fill (dlBufL.begin(), dlBufL.end(), 0.0f);
    std::fill (dlBufR.begin(), dlBufR.end(), 0.0f);
    dlLpL = dlLpR = 0;
    verb.reset();
}

void FxRack::process (float* L, float* R, int n, const P::Values& v, const std::array<int, kNumFx>& order,
                      double bpm, float cutoffRight)
{
    static const int onIds[]  = { P::crushOn, P::distOn, P::filtOn, P::shiftOn, P::stutOn, P::dlyOn, P::verbOn };
    static const int mixIds[] = { P::crushMix, P::distMix, P::filtMix, P::shiftMix, P::stutMix, P::dlyMix, P::verbMix };

    n = juce::jmin (n, maxBlock);
    for (int k = 0; k < kNumFx; ++k)
    {
        const int fx = juce::jlimit (0, kNumFx - 1, order[(size_t) k]);
        if (v[(size_t) onIds[fx]] < 0.5f)
            continue;

        const float mix = juce::jlimit (0.0f, 1.0f, v[(size_t) mixIds[fx]]);
        std::copy (L, L + n, tmpL.begin());
        std::copy (R, R + n, tmpR.begin());
        float* wl = tmpL.data();
        float* wr = tmpR.data();

        switch (fx)
        {
            case 0: crush (wl, wr, n, v); break;
            case 1: dist (wl, wr, n, v); break;
            case 2: filter (wl, wr, n, v, cutoffRight); break;
            case 3: shift (wl, wr, n, v); break;
            case 4: stutter (wl, wr, n, v, bpm); break;
            case 5: delay (wl, wr, n, v, bpm); break;
            case 6: reverb (wl, wr, n, v); break;
            default: break;
        }

        for (int i = 0; i < n; ++i)
        {
            L[i] += (wl[i] - L[i]) * mix;
            R[i] += (wr[i] - R[i]) * mix;
        }
    }
}

void FxRack::crush (float* L, float* R, int n, const P::Values& v)
{
    const float levels = std::pow (2.0f, v[P::crushBits] - 1.0f);
    const float down = juce::jmax (1.0f, v[P::crushDown]);
    for (int i = 0; i < n; ++i)
    {
        crushCounter += 1.0f;
        if (crushCounter >= down)
        {
            crushCounter -= down;
            crushHoldL = std::round (L[i] * levels) / levels;
            crushHoldR = std::round (R[i] * levels) / levels;
        }
        L[i] = crushHoldL;
        R[i] = crushHoldR;
    }
}

void FxRack::dist (float* L, float* R, int n, const P::Values& v)
{
    const float drive = v[P::distDrive];
    const float g = 1.0f + drive * 30.0f;
    const int mode = (int) v[P::distMode];
    const float comp = 1.0f / (1.0f + drive * 1.5f);

    auto shape = [&] (float x)
    {
        x *= g;
        switch (mode)
        {
            case 0:  return std::tanh (x) * (0.6f + 0.4f * comp);
            case 1:
            {
                // repli (wavefolder)
                for (int k = 0; k < 8 && std::abs (x) > 1.0f; ++k)
                    x = x > 1.0f ? 2.0f - x : -2.0f - x;
                return x * 0.8f;
            }
            default: return juce::jlimit (-1.0f, 1.0f, x) * 0.8f;
        }
    };

    for (int i = 0; i < n; ++i)
    {
        L[i] = shape (L[i]);
        R[i] = shape (R[i]);
    }
}

void FxRack::filter (float* L, float* R, int n, const P::Values& v, float cutoffRight)
{
    using T = juce::dsp::StateVariableTPTFilterType;
    const int type = (int) v[P::filtType];
    const T t = type == 0 ? T::lowpass : (type == 1 ? T::bandpass : T::highpass);
    const float nyq = (float) sr * 0.45f;

    filtL.setType (t);
    filtR.setType (t);
    filtL.setCutoffFrequency (juce::jlimit (20.0f, nyq, v[P::filtCutoff]));
    filtR.setCutoffFrequency (juce::jlimit (20.0f, nyq, cutoffRight));
    filtL.setResonance (v[P::filtReso]);
    filtR.setResonance (v[P::filtReso]);

    for (int i = 0; i < n; ++i)
    {
        L[i] = filtL.processSample (0, L[i]);
        R[i] = filtR.processSample (0, R[i]);
    }
}

void FxRack::shift (float* L, float* R, int n, const P::Values& v)
{
    const float amount = v[P::shiftAmount];
    if ((int) v[P::shiftMode] == 0)
    {
        // Pitch shift granulaire : deux têtes de lecture décalées, fenêtre ~40 ms
        const double ratio = std::pow (2.0, (double) amount); // ±1 octave
        const int size = (int) psBufL.size();
        const double win = juce::jmin (size - 4.0, sr * 0.04);
        const double inc = (1.0 - ratio) / win;

        auto read = [&] (const std::vector<float>& b, double delaySamples)
        {
            double pos = psWrite - delaySamples;
            while (pos < 0) pos += size;
            const int i0 = (int) pos % size;
            const int i1 = (i0 + 1) % size;
            const float f = (float) (pos - std::floor (pos));
            return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * f;
        };

        for (int i = 0; i < n; ++i)
        {
            psBufL[(size_t) psWrite] = L[i];
            psBufR[(size_t) psWrite] = R[i];

            psPhase += inc;
            psPhase -= std::floor (psPhase);
            const double p2 = std::fmod (psPhase + 0.5, 1.0);
            const float g1 = (float) std::sin (juce::MathConstants<double>::pi * psPhase);
            const float g2 = (float) std::sin (juce::MathConstants<double>::pi * p2);

            L[i] = read (psBufL, psPhase * win + 1) * g1 + read (psBufL, p2 * win + 1) * g2;
            R[i] = read (psBufR, psPhase * win + 1) * g1 + read (psBufR, p2 * win + 1) * g2;

            if (++psWrite >= size)
                psWrite = 0;
        }
    }
    else
    {
        // Frequency shift : transformée de Hilbert (allpass) puis modulation en anneau complexe
        const double hz = amount * 500.0;
        const double w = juce::MathConstants<double>::twoPi * hz / sr;
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            const float c = (float) std::cos (fsPhase), s = (float) std::sin (fsPhase);
            for (int k = 0; k < 2; ++k)
            {
                float a = ch[k][i], b = ch[k][i];
                for (auto& ap : hilA[(size_t) k]) a = ap.process (a);
                for (auto& ap : hilB[(size_t) k]) b = ap.process (b);
                const float re = hilDelay[(size_t) k];
                hilDelay[(size_t) k] = a;
                ch[k][i] = re * c - b * s;
            }
            fsPhase += w;
            if (fsPhase > juce::MathConstants<double>::twoPi) fsPhase -= juce::MathConstants<double>::twoPi;
            if (fsPhase < 0) fsPhase += juce::MathConstants<double>::twoPi;
        }
    }
}

void FxRack::stutter (float* L, float* R, int n, const P::Values& v, double bpm)
{
    static const double beats[] = { 0.5, 0.25, 0.125, 0.0625 };
    const int size = (int) stBufL.size();
    const int sliceLen = juce::jlimit (64, size / 2,
                                       (int) (beats[juce::jlimit (0, 3, (int) v[P::stutDiv])] * 60.0 / juce::jmax (20.0, bpm) * sr));
    const int fade = juce::jmin (64, sliceLen / 4);

    for (int i = 0; i < n; ++i)
    {
        stBufL[(size_t) stWrite] = L[i];
        stBufR[(size_t) stWrite] = R[i];

        if (stPos >= stSliceLen)
        {
            stPos = 0;
            stSliceLen = sliceLen;
            if (stRemaining > 0)
                --stRemaining;
            else if (stRng.nextFloat() < v[P::stutChance])
            {
                stRemaining = juce::jmax (1, (int) v[P::stutRepeats]);
                stLoopStart = (stWrite - sliceLen + size) % size;
            }
        }

        if (stRemaining > 0)
        {
            const int idx = (stLoopStart + stPos) % size;
            float g = 1.0f;
            if (stPos < fade) g = (float) stPos / fade;
            else if (stPos > stSliceLen - fade) g = (float) (stSliceLen - stPos) / fade;
            L[i] = stBufL[(size_t) idx] * g;
            R[i] = stBufR[(size_t) idx] * g;
        }

        ++stPos;
        if (++stWrite >= size)
            stWrite = 0;
    }
}

void FxRack::delay (float* L, float* R, int n, const P::Values& v, double bpm)
{
    static const double beats[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
    const int size = (int) dlBufL.size();
    const float target = (float) juce::jlimit (1.0, size - 2.0,
                                               beats[juce::jlimit (0, 5, (int) v[P::dlyTime])] * 60.0 / juce::jmax (20.0, bpm) * sr);
    dlTime.setTargetValue (target);
    const float fb = v[P::dlyFeedback];
    const bool ping = v[P::dlyPingPong] > 0.5f;
    const float lp = 0.35f;

    for (int i = 0; i < n; ++i)
    {
        const float d = dlTime.getNextValue();
        double rp = dlWrite - d;
        while (rp < 0) rp += size;
        const int i0 = (int) rp % size, i1 = (i0 + 1) % size;
        const float f = (float) (rp - std::floor (rp));
        const float dl = dlBufL[(size_t) i0] + (dlBufL[(size_t) i1] - dlBufL[(size_t) i0]) * f;
        const float dr = dlBufR[(size_t) i0] + (dlBufR[(size_t) i1] - dlBufR[(size_t) i0]) * f;

        dlLpL += (dl - dlLpL) * lp;
        dlLpR += (dr - dlLpR) * lp;

        if (ping)
        {
            dlBufL[(size_t) dlWrite] = 0.5f * (L[i] + R[i]) + dlLpR * fb;
            dlBufR[(size_t) dlWrite] = dlLpL * fb;
        }
        else
        {
            dlBufL[(size_t) dlWrite] = L[i] + dlLpL * fb;
            dlBufR[(size_t) dlWrite] = R[i] + dlLpR * fb;
        }

        L[i] = dl;
        R[i] = dr;
        if (++dlWrite >= size)
            dlWrite = 0;
    }
}

void FxRack::reverb (float* L, float* R, int n, const P::Values& v)
{
    juce::Reverb::Parameters p;
    p.roomSize = v[P::verbSize];
    p.damping = v[P::verbDamp];
    p.wetLevel = 1.0f;
    p.dryLevel = 0.0f;
    p.width = 1.0f;
    verb.setParameters (p);
    verb.processStereo (L, R, n);
}
