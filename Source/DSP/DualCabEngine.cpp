#include "DualCabEngine.h"

#include <array>
#include <cmath>

namespace dualir
{
namespace
{
float finiteClamp (float value, float fallback, float lower, float upper) noexcept
{
    return juce::jlimit (lower, upper, std::isfinite (value) ? value : fallback);
}

float cabGain (const CabSettings& settings) noexcept
{
    if (! settings.enabled)
        return 0.0f;

    const auto gain = juce::Decibels::decibelsToGain (
        finiteClamp (settings.gainDb, 0.0f, -24.0f, 24.0f));
    return settings.inverted ? -gain : gain;
}
}

void DualCabEngine::prepare (double sampleRate, int maximumBlockSize, int channels,
                            const EngineSettings& initialSettings)
{
    const auto rate = std::isfinite (sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
    blockSize = juce::jlimit (1, 1048576, maximumBlockSize);
    preparedChannels = juce::jlimit (1, 2, channels);
    scratchA.setSize (preparedChannels, blockSize);
    scratchB.setSize (preparedChannels, blockSize);
    cabA.prepare (rate, blockSize, preparedChannels);
    cabB.prepare (rate, blockSize, preparedChannels);

    for (auto* value : { &gainA, &gainB, &dryGain, &outputGain, &bypassMix })
        value->reset (rate, 0.02);

    const auto hasA = cabA.hasImpulse();
    const auto hasB = cabB.hasImpulse();
    const auto blend = finiteClamp (initialSettings.blend, 0.5f, 0.0f, 1.0f);
    gainA.setCurrentAndTargetValue (
        hasA ? cabGain (initialSettings.a) * (hasB ? 1.0f - blend : 1.0f) : 0.0f);
    gainB.setCurrentAndTargetValue (
        hasB ? cabGain (initialSettings.b) * (hasA ? blend : 1.0f) : 0.0f);
    dryGain.setCurrentAndTargetValue (! hasA && ! hasB ? 1.0f : 0.0f);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        finiteClamp (initialSettings.outputDb, 0.0f, -24.0f, 12.0f)));
    bypassMix.setCurrentAndTargetValue (initialSettings.bypass ? 1.0f : 0.0f);
}

void DualCabEngine::reset() noexcept
{
    cabA.reset();
    cabB.reset();
    for (auto* value : { &gainA, &gainB, &dryGain, &outputGain, &bypassMix })
        value->setCurrentAndTargetValue (value->getTargetValue());
}

void DualCabEngine::queueImpulse (int slot, juce::AudioBuffer<float> buffer,
                                 double sourceRate, bool isLoaded)
{
    if (slot == 0)
        cabA.queueImpulse (std::move (buffer), sourceRate, isLoaded);
    else if (slot == 1)
        cabB.queueImpulse (std::move (buffer), sourceRate, isLoaded);
}

void DualCabEngine::process (juce::AudioBuffer<float>& buffer,
                            const EngineSettings& settings) noexcept
{
    if (blockSize <= 0 || preparedChannels <= 0)
        return;

    // Some hosts temporarily exceed the announced maximum block size. Chunking
    // keeps both scratch memory and the convolution ProcessSpec valid.
    for (int offset = 0; offset < buffer.getNumSamples();)
    {
        const auto count = juce::jmin (blockSize, buffer.getNumSamples() - offset);
        processChunk (buffer, offset, count, settings);
        offset += count;
    }

    for (int channel = preparedChannels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());
}

void DualCabEngine::processChunk (juce::AudioBuffer<float>& buffer, int offset, int count,
                                 const EngineSettings& settings) noexcept
{
    const auto channels = juce::jmin (preparedChannels, buffer.getNumChannels());
    if (channels <= 0)
        return;

    std::array<float*, 2> aPointers {}, bPointers {};
    for (int channel = 0; channel < channels; ++channel)
    {
        aPointers[static_cast<size_t> (channel)] = scratchA.getWritePointer (channel);
        bPointers[static_cast<size_t> (channel)] = scratchB.getWritePointer (channel);
        juce::FloatVectorOperations::copy (aPointers[static_cast<size_t> (channel)],
                                           buffer.getReadPointer (channel, offset), count);
        juce::FloatVectorOperations::copy (bPointers[static_cast<size_t> (channel)],
                                           buffer.getReadPointer (channel, offset), count);
    }

    // Views use AudioBuffer's inline pointer storage; samples remain preallocated.
    juce::AudioBuffer<float> aView (aPointers.data(), channels, count);
    juce::AudioBuffer<float> bView (bPointers.data(), channels, count);
    cabA.process (aView);
    cabB.process (bView);

    const auto hasA = cabA.hasImpulse();
    const auto hasB = cabB.hasImpulse();
    const auto blend = finiteClamp (settings.blend, 0.5f, 0.0f, 1.0f);
    gainA.setTargetValue (hasA ? cabGain (settings.a) * (hasB ? 1.0f - blend : 1.0f) : 0.0f);
    gainB.setTargetValue (hasB ? cabGain (settings.b) * (hasA ? blend : 1.0f) : 0.0f);
    dryGain.setTargetValue (! hasA && ! hasB ? 1.0f : 0.0f);
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (
        finiteClamp (settings.outputDb, 0.0f, -24.0f, 12.0f)));
    bypassMix.setTargetValue (settings.bypass ? 1.0f : 0.0f);

    // Fixed final trim applies to the complete mix, including bypass.
    const float outputTrim = juce::Decibels::decibelsToGain (-12.0f);
    for (int sample = 0; sample < count; ++sample)
    {
        const auto a = gainA.getNextValue();
        const auto b = gainB.getNextValue();
        const auto dry = dryGain.getNextValue();
        const auto out = outputGain.getNextValue();
        const auto bypass = bypassMix.getNextValue();

        for (int channel = 0; channel < channels; ++channel)
        {
            auto* output = buffer.getWritePointer (channel, offset);
            const auto original = output[sample];
            const auto wet = (aView.getSample (channel, sample) * a
                            + bView.getSample (channel, sample) * b + original * dry) * out;
            output[sample] = (wet * (1.0f - bypass) + original * bypass) * outputTrim;
        }
    }
}
}
