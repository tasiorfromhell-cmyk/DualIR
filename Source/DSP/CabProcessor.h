#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>

namespace dualir
{
/** One cabinet convolution engine. File decoding belongs on a worker thread.

    queueImpulse() may be called by a non-realtime producer while audio is
    running. The audio thread only tries the pending lock; it never waits for
    the producer. JUCE takes ownership of the decoded samples and builds the
    convolution partitions on its own background queue.
*/
class CabProcessor final
{
public:
    CabProcessor() = default;
    explicit CabProcessor (juce::dsp::ConvolutionMessageQueue& queue);

    void prepare (double sampleRate, int maximumBlockSize, int channels);
    void reset() noexcept;

    // Move buffers into this function to avoid an unnecessary producer-side copy.
    // loaded=false represents an empty slot, backed by an identity IR.
    void queueImpulse (juce::AudioBuffer<float> buffer, double sourceRate, bool loaded = true);
    void process (juce::AudioBuffer<float>& buffer) noexcept;
    bool hasImpulse() const noexcept;

private:
    void consumePending() noexcept;

    juce::dsp::Convolution convolution;
    juce::SpinLock pendingLock;
    juce::AudioBuffer<float> pendingBuffer;
    double pendingRate = 44100.0;
    bool pendingLoaded = false;
    bool pending = false;
    bool prepared = false;
    std::atomic<bool> loaded { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabProcessor)
};
}
