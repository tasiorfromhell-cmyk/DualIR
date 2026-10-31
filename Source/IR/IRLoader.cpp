#include "IRLoader.h"

#include <cmath>

namespace dualir
{
namespace
{
bool validSampleRate (double sampleRate) noexcept
{
    return std::isfinite (sampleRate) && sampleRate >= 8000.0 && sampleRate <= 384000.0;
}

juce::String validateSamples (const juce::AudioBuffer<float>& samples, double sampleRate)
{
    if (! validSampleRate (sampleRate))
        return "IR sample rate must be between 8 kHz and 384 kHz.";

    if (samples.getNumChannels() < 1 || samples.getNumChannels() > 2)
        return "Choose a mono or stereo impulse response.";

    if (samples.getNumSamples() <= 0)
        return "The impulse response contains no audio samples.";

    if (static_cast<double> (samples.getNumSamples()) > sampleRate * IRLoader::maxDurationSeconds)
        return "The impulse response must be no longer than 2 seconds.";

    bool containsSignal = false;
    for (int channel = 0; channel < samples.getNumChannels(); ++channel)
        for (int sample = 0; sample < samples.getNumSamples(); ++sample)
        {
            const auto value = samples.getSample (channel, sample);
            if (! std::isfinite (value))
                return "The impulse response contains invalid audio samples.";

            containsSignal = containsSignal || std::abs (value) > 0.0f;
        }

    if (! containsSignal)
        return "The impulse response contains only silence.";

    return {};
}

IRLoader::Result failure (const juce::String& message)
{
    IRLoader::Result result;
    result.error = message;
    return result;
}
}

IRLoader::Result IRLoader::loadFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return failure ("The impulse response file does not exist.");

    const auto size = file.getSize();
    if (size <= 0 || size > static_cast<juce::int64> (maxEncodedBytes))
        return failure ("Choose an audio file smaller than 16 MiB.");

    auto stream = file.createInputStream();
    if (stream == nullptr || stream->failedToOpen())
        return failure ("The impulse response file could not be opened.");

    return decode (std::move (stream));
}

IRLoader::Result IRLoader::loadEncoded (const void* data, size_t size)
{
    if (data == nullptr || size == 0 || size > maxEncodedBytes)
        return failure ("The stored impulse response is empty or too large.");

    return decode (std::make_unique<juce::MemoryInputStream> (data, size, false));
}

IRLoader::Result IRLoader::decode (std::unique_ptr<juce::InputStream> stream)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (std::move (stream)));
    if (reader == nullptr)
        return failure ("The file is not a supported or valid WAV/AIFF impulse response.");

    if (! validSampleRate (reader->sampleRate))
        return failure ("IR sample rate must be between 8 kHz and 384 kHz.");

    if (reader->numChannels < 1 || reader->numChannels > 2)
        return failure ("Choose a mono or stereo impulse response.");

    if (reader->lengthInSamples <= 0)
        return failure ("The impulse response contains no audio samples.");

    if (static_cast<double> (reader->lengthInSamples) > reader->sampleRate * maxDurationSeconds)
        return failure ("The impulse response must be no longer than 2 seconds.");

    Result result;
    result.sampleRate = reader->sampleRate;
    result.samples.setSize (static_cast<int> (reader->numChannels),
                            static_cast<int> (reader->lengthInSamples));
    if (! reader->read (&result.samples, 0, result.samples.getNumSamples(), 0, true, true))
        return failure ("The impulse response could not be decoded completely.");

    result.error = validateSamples (result.samples, result.sampleRate);
    if (result.error.isNotEmpty())
        result.samples.setSize (0, 0);
    return result;
}

juce::MemoryBlock IRLoader::encodeWav (const juce::AudioBuffer<float>& samples, double sampleRate)
{
    if (validateSamples (samples, sampleRate).isNotEmpty())
        return {};

    juce::MemoryBlock encoded;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (encoded, false);
    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriterOptions()
                             .withSampleRate (sampleRate)
                             .withNumChannels (samples.getNumChannels())
                             .withBitsPerSample (32)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor (stream, options);
    if (writer == nullptr)
        return {};

    if (! writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples()))
        return {};

    // Destruction finalises the WAV header before the block is returned.
    writer.reset();
    return encoded;
}
}
