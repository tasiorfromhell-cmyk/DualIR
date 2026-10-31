#pragma once

#include "CabProcessor.h"

namespace dualir
{
struct CabSettings
{
    float gainDb = 0.0f;
    bool enabled = true;
    bool inverted = false;
};

struct EngineSettings
{
    CabSettings a, b;
    float blend = 0.5f;
    float outputDb = 0.0f;
    bool bypass = false;
};

/** Parallel zero-latency cabinet pair with bounded, allocation-free audio work.

    A single populated slot receives the full wet signal; two populated slots
    use linear A/B weights. Empty slots cannot leak an unfiltered dry signal into
    a loaded cabinet. With no IRs, audio passes through unchanged. Bypass routes
    the unmodified input around both cabinet and output gain stages.
*/
class DualCabEngine final
{
public:
    DualCabEngine() = default;

    // Initialise smoothing from the host's current/restored parameters so the
    // first sample already respects mute, gain, polarity, blend and bypass.
    void prepare (double sampleRate, int maximumBlockSize, int channels,
                  const EngineSettings& initialSettings = {});
    void reset() noexcept;
    void queueImpulse (int slot, juce::AudioBuffer<float> buffer,
                       double sourceRate, bool loaded = true);
    void process (juce::AudioBuffer<float>& buffer, const EngineSettings& settings) noexcept;

private:
    void processChunk (juce::AudioBuffer<float>& buffer, int offset, int count,
                       const EngineSettings& settings) noexcept;

    // Keep the queue alive until both convolvers have been destroyed.
    juce::dsp::ConvolutionMessageQueue messageQueue { 64 };
    CabProcessor cabA { messageQueue }, cabB { messageQueue };
    juce::AudioBuffer<float> scratchA, scratchB;
    juce::SmoothedValue<float> gainA, gainB, dryGain, outputGain, bypassMix;
    int blockSize = 0;
    int preparedChannels = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DualCabEngine)
};
}
