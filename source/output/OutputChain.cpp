#include "OutputChain.h"

void OutputChain::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, maxBlock), 2 };

    hp[0].prepare (spec);
    hp[0].setType (juce::dsp::StateVariableTPTFilterType::highpass);
    hp[0].setResonance (0.707f);

    monoSplit.prepare (spec);
    monoSplit.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);

    xoLow.prepare (spec);
    xoLow.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    xoLow.setCutoffFrequency (150.0f);
    xoHigh.prepare (spec);
    xoHigh.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    xoHigh.setCutoffFrequency (2500.0f);


    scratch.setSize (2, juce::jmax (1, maxBlock));
    outGain.reset (sr, 0.03);
    reset();
}

void OutputChain::reset()
{
    hp[0].reset();
    monoSplit.reset();
    xoLow.reset();
    xoHigh.reset();
    limGain = 1.0f;
    envDb.fill (-100.0f);
    gainDb.fill (0.0f);
}

void OutputChain::process (float* L, float* R, int n, const P::Values& v)
{
    float* ch[2] = { L, R };

    // --- Passe-haut ----------------------------------------------------------------------
    hp[0].setCutoffFrequency (juce::jlimit (10.0f, 300.0f, v[P::hpFreq]));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            ch[c][i] = hp[0].processSample (c, ch[c][i]);

    // --- Grave en mono --------------------------------------------------------------------
    const float monoF = v[P::monoFreq];
    if (monoF > 20.0f)
    {
        monoSplit.setCutoffFrequency (monoF);
        for (int i = 0; i < n; ++i)
        {
            float lowL, highL, lowR, highR;
            monoSplit.processSample (0, L[i], lowL, highL);
            monoSplit.processSample (1, R[i], lowR, highR);
            const float m = 0.5f * (lowL + lowR);
            L[i] = m + highL;
            R[i] = m + highR;
        }
    }

    // --- OTT : compression haut/bas sur 3 bandes ------------------------------------------
    const float amount = v[P::ottAmount];
    if (amount > 0.001f)
    {
        const float att = 1.0f - std::exp (-1.0f / (float) (0.002 * sr));
        const float rel = 1.0f - std::exp (-1.0f / (float) (0.08 * sr));
        for (int i = 0; i < n; ++i)
        {
            float b[2][3];
            for (int c = 0; c < 2; ++c)
            {
                float lo, rest, mid, hi;
                xoLow.processSample (c, ch[c][i], lo, rest);
                xoHigh.processSample (c, rest, mid, hi);
                b[c][0] = lo; b[c][1] = mid; b[c][2] = hi;
            }
            float outL = 0, outR = 0;
            for (int k = 0; k < 3; ++k)
            {
                const float lvl = juce::Decibels::gainToDecibels (juce::jmax (std::abs (b[0][k]), std::abs (b[1][k])), -100.0f);
                envDb[(size_t) k] += (lvl - envDb[(size_t) k]) * (lvl > envDb[(size_t) k] ? att : rel);
                const float e = envDb[(size_t) k];
                float g = 0.0f;
                if (e > -20.0f)                g = (-20.0f - e) * 0.75f;                       // compression vers le bas
                else if (e < -42.0f && e > -80.0f) g = juce::jmin (18.0f, (-42.0f - e) * 0.6f); // compression vers le haut
                gainDb[(size_t) k] += (g - gainDb[(size_t) k]) * 0.002f;
                const float lin = juce::Decibels::decibelsToGain (gainDb[(size_t) k] * amount + 3.0f * amount);
                outL += b[0][k] * lin;
                outR += b[1][k] * lin;
            }
            L[i] = outL;
            R[i] = outR;
        }
    }

    // --- Soft clip -------------------------------------------------------------------------
    const float drive = 1.0f + v[P::clipDrive] * 7.0f;
    const float norm = 1.0f / std::tanh (drive);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            ch[c][i] = std::tanh (ch[c][i] * drive) * norm;

    // --- Gain puis limiteur -------------------------------------------------------------------
    outGain.setTargetValue (juce::Decibels::decibelsToGain (v[P::outGain], -60.0f));
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        L[i] *= g;
        R[i] *= g;
    }

    // Limiteur crête : attaque instantanée, relâchement 80 ms, plafond garanti
    const float ceiling = juce::Decibels::decibelsToGain (v[P::limCeiling]);
    const float release = 1.0f - std::exp (-1.0f / (float) (0.08 * sr));
    for (int i = 0; i < n; ++i)
    {
        const float peak = juce::jmax (std::abs (L[i]), std::abs (R[i]));
        const float target = peak > ceiling ? ceiling / peak : 1.0f;
        limGain = target < limGain ? target : limGain + (target - limGain) * release;
        L[i] = juce::jlimit (-ceiling, ceiling, L[i] * limGain);
        R[i] = juce::jlimit (-ceiling, ceiling, R[i] * limGain);
    }
}
