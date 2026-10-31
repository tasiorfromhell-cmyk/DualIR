#include "CabProcessor.h"

#include <cmath>

namespace dualir
{
CabProcessor::CabProcessor (juce::dsp::ConvolutionMessageQueue& queue)
    : convolution (queue)
{
}

void CabProcessor::queueImpulse (juce::AudioBuffer<float> buffer, double sourceRate, bool isLoaded)
{
    if (! std::isfinite (sourceRate) || sourceRate <= 0.0)
        return;

    if (! isLoaded)
    {
        // Allocate the identity on the caller's worker thread, never in process().
        buffer.setSize (1, 1);
        buffer.setSample (0, 0, 1.0f);
    }

    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() < 1 || buffer.getNumChannels() > 2)
        return;

    const juce::SpinLock::ScopedLockType lock (pendingLock);
    // An obsolete pending buffer is released here on the producer thread.
    pendingBuffer = std::move (buffer);
    pendingRate = sourceRate;
    pendingLoaded = isLoaded;
    pending = true;
}

void CabProcessor::consumePending() noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (pendingLock);
    if (! lock.isLocked() || ! pending)
        return;

    // All calls into convolution are serialised with its process() and prepare().
    // This move overload is documented as realtime-safe by JUCE.
    convolution.loadImpulseResponse (std::move (pendingBuffer), pendingRate,
                                    juce::dsp::Convolution::Stereo::yes,
                                    juce::dsp::Convolution::Trim::no,
                                    juce::dsp::Convolution::Normalise::no);
    loaded.store (pendingLoaded, std::memory_order_release);
    pending = false;
}

void CabProcessor::prepare (double sampleRate, int maximumBlockSize, int channels)
{
    consumePending();
    const auto rate = std::isfinite (sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
    convolution.prepare ({ rate,
                           static_cast<juce::uint32> (juce::jmax (1, maximumBlockSize)),
                           static_cast<juce::uint32> (juce::jlimit (1, 2, channels)) });
    prepared = true;
}

void CabProcessor::reset() noexcept
{
    if (prepared)
        convolution.reset();
}

void CabProcessor::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (! prepared)
        return;

    consumePending();
    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
        return;

    auto block = juce::dsp::AudioBlock<float> (buffer);
    convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
}

bool CabProcessor::hasImpulse() const noexcept
{
    return loaded.load (std::memory_order_acquire);
}
}
