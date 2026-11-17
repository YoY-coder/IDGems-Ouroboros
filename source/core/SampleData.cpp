#include "SampleData.h"

SampleData::SampleData (MemoryBudget& b, int frames, double sr, const juce::String& n)
    : budget (b.shared_from_this()), data ((size_t) frames * 2, 0), numFrames (frames), sampleRate (sr), name (n)
{
    budget->used += getBytes();
}

SampleData::~SampleData()
{
    budget->used -= getBytes();
}

std::shared_ptr<SampleData> SampleData::create (MemoryBudget& budget, int numFrames, double sr, const juce::String& name)
{
    if (numFrames <= 0)
        return nullptr;
    if (! budget.canAllocate ((int64_t) numFrames * 4))
        return nullptr;
    return std::shared_ptr<SampleData> (new SampleData (budget, numFrames, sr, name));
}

std::shared_ptr<SampleData> SampleData::fromBuffer (MemoryBudget& budget, const juce::AudioBuffer<float>& buf,
                                                    int start, int numFrames, double sr, const juce::String& name)
{
    numFrames = juce::jmin (numFrames, buf.getNumSamples() - start);
    auto sd = create (budget, numFrames, sr, name);
    if (sd == nullptr)
        return nullptr;

    const float* l = buf.getReadPointer (0, start);
    const float* r = buf.getReadPointer (juce::jmin (1, buf.getNumChannels() - 1), start);
    for (int i = 0; i < numFrames; ++i)
        sd->set (i, l[i], r[i]);
    return sd;
}

void SampleData::copyTo (juce::AudioBuffer<float>& dest, int srcStart, int num) const
{
    num = juce::jmin (num, numFrames - srcStart, dest.getNumSamples());
    auto* l = dest.getWritePointer (0);
    auto* r = dest.getWritePointer (juce::jmin (1, dest.getNumChannels() - 1));
    for (int i = 0; i < num; ++i)
    {
        l[i] = getL (srcStart + i);
        r[i] = getR (srcStart + i);
    }
}

std::vector<float> SampleData::toMono (int start, int num) const
{
    start = juce::jlimit (0, numFrames, start);
    num = juce::jlimit (0, numFrames - start, num);
    std::vector<float> m ((size_t) num);
    for (int i = 0; i < num; ++i)
        m[(size_t) i] = 0.5f * (getL (start + i) + getR (start + i));
    return m;
}

//==============================================================================
CaptureRing::CaptureRing (MemoryBudget& b, int frames, double sr)
    : budget (b.shared_from_this()), data ((size_t) frames * 2, 0), capacity (frames), sampleRate (sr)
{
    budget->used += getBytes();
}

CaptureRing::~CaptureRing()
{
    budget->used -= getBytes();
}

void CaptureRing::write (const float* l, const float* r, int num) noexcept
{
    int pos = writePos.load (std::memory_order_relaxed);
    for (int i = 0; i < num; ++i)
    {
        data[(size_t) pos * 2]     = SampleData::toInt (l[i]);
        data[(size_t) pos * 2 + 1] = SampleData::toInt (r[i]);
        if (++pos >= capacity)
            pos = 0;
    }
    writePos.store (pos, std::memory_order_release);
    written.fetch_add (num, std::memory_order_relaxed);
}

std::shared_ptr<SampleData> CaptureRing::snapshot (MemoryBudget& b, const juce::String& name) const
{
    const int filled = (int) std::min<int64_t> (written.load(), capacity);
    if (filled < 64)
        return nullptr;

    auto sd = SampleData::create (b, filled, sampleRate, name);
    if (sd == nullptr)
        return nullptr;

    const int end = writePos.load (std::memory_order_acquire);
    int pos = (end - filled + capacity) % capacity;
    for (int i = 0; i < filled; ++i)
    {
        sd->set (i, data[(size_t) pos * 2] / 32768.0f, data[(size_t) pos * 2 + 1] / 32768.0f);
        if (++pos >= capacity)
            pos = 0;
    }
    return sd;
}
