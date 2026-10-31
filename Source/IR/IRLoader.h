#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace dualir
{
/** Decodes and validates IRs on a worker thread, never on the audio thread. */
class IRLoader final
{
public:
    struct Result
    {
        juce::AudioBuffer<float> samples;
        double sampleRate = 0.0;
        juce::String error;

        bool succeeded() const noexcept
        {
            return error.isEmpty() && samples.getNumSamples() > 0;
        }
    };

    static constexpr double maxDurationSeconds = 2.0;
    static constexpr size_t maxEncodedBytes = 16u * 1024u * 1024u;

    static Result loadFile (const juce::File& file);
    static Result loadEncoded (const void* data, size_t size);

    /** Lossless IEEE float32 WAV for embedding an IR in plug-in state.
        Returns an empty block if validation or writing fails.
    */
    static juce::MemoryBlock encodeWav (const juce::AudioBuffer<float>& samples, double sampleRate);

private:
    static Result decode (std::unique_ptr<juce::InputStream> stream);
};
}
