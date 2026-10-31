#include "../Source/IR/IRLoader.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
struct Checks
{
    void expect (bool passed, const char* description)
    {
        ++total;
        if (! passed)
        {
            ++failed;
            std::cerr << "FAIL: " << description << '\n';
        }
    }

    int total = 0;
    int failed = 0;
};

juce::MemoryBlock makeFixture (juce::AudioFormat& format, const juce::AudioBuffer<float>& samples,
                              double sampleRate, bool floatWav)
{
    juce::MemoryBlock bytes;
    std::unique_ptr<juce::OutputStream> output = std::make_unique<juce::MemoryOutputStream> (bytes, false);
    const auto options = juce::AudioFormatWriterOptions()
                             .withSampleRate (sampleRate)
                             .withNumChannels (samples.getNumChannels())
                             .withBitsPerSample (floatWav ? 32 : 24)
                             .withSampleFormat (floatWav
                                 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                 : juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto writer = format.createWriterFor (output, options);
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples()))
        return {};
    writer.reset();
    return bytes;
}
}

int main()
{
    Checks checks;
    for (const auto channels : { 1, 2 })
    {
        juce::AudioBuffer<float> samples (channels, 64);
        samples.clear();
        samples.setSample (0, 0, 1.0f);
        samples.setSample (0, 17, -0.25f);
        if (channels == 2)
        {
            samples.setSample (1, 2, 0.5f);
            samples.setSample (1, 31, -0.125f);
        }

        const auto encoded = dualir::IRLoader::encodeWav (samples, 48000.0);
        checks.expect (encoded.getSize() > 0, "Encode mono/stereo float WAV");
        const auto decoded = dualir::IRLoader::loadEncoded (encoded.getData(), encoded.getSize());
        checks.expect (decoded.succeeded(), "Decode mono/stereo float WAV");
        checks.expect (decoded.samples.getNumChannels() == channels
                       && decoded.samples.getNumSamples() == 64,
                       "Preserve dimensions, leading silence and IR length");
        checks.expect (juce::exactlyEqual (decoded.sampleRate, 48000.0), "Preserve sample rate");
        bool identical = decoded.succeeded() && decoded.samples.getNumChannels() == channels
                         && decoded.samples.getNumSamples() == 64;
        if (identical)
            for (int channel = 0; channel < channels; ++channel)
                for (int sample = 0; sample < 64; ++sample)
                    identical = identical && juce::exactlyEqual (
                        samples.getSample (channel, sample), decoded.samples.getSample (channel, sample));
        checks.expect (identical, "Preserve float amplitudes without normalisation");

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("DualIR-loader-test", ".wav", false);
        const auto written = file.replaceWithData (encoded.getData(), encoded.getSize());
        checks.expect (written, "Write temporary IR fixture");
        const auto loaded = dualir::IRLoader::loadFile (file);
        checks.expect (loaded.succeeded() && loaded.samples.getNumChannels() == channels,
                       "Load IR from filesystem");
        checks.expect (file.deleteFile(), "Remove temporary IR fixture");
    }

    checks.expect (! dualir::IRLoader::loadEncoded (nullptr, 0).succeeded(), "Reject empty stored IR");
    const char garbage[] = "this is not audio";
    checks.expect (! dualir::IRLoader::loadEncoded (garbage, sizeof (garbage)).succeeded(),
                   "Reject corrupt audio");
    checks.expect (! dualir::IRLoader::loadEncoded (garbage, dualir::IRLoader::maxEncodedBytes + 1).succeeded(),
                   "Reject oversized encoded audio before reading data");
    const auto absent = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getNonexistentChildFile ("DualIR-absent", ".wav", false);
    checks.expect (! dualir::IRLoader::loadFile (absent).succeeded(), "Reject missing file");

    juce::AudioBuffer<float> buffer (1, 1);
    buffer.clear();
    checks.expect (dualir::IRLoader::encodeWav (buffer, 48000.0).getSize() == 0, "Reject silent IR");
    buffer.setSample (0, 0, 1.0f);
    checks.expect (dualir::IRLoader::encodeWav (buffer, 0.0).getSize() == 0, "Reject zero sample rate");
    checks.expect (dualir::IRLoader::encodeWav (buffer, 4000.0).getSize() == 0, "Reject low sample rate");
    checks.expect (dualir::IRLoader::encodeWav (buffer, 768000.0).getSize() == 0, "Reject high sample rate");
    checks.expect (dualir::IRLoader::encodeWav (buffer, std::numeric_limits<double>::quiet_NaN()).getSize() == 0,
                   "Reject invalid sample rate");
    buffer.setSample (0, 0, std::numeric_limits<float>::quiet_NaN());
    checks.expect (dualir::IRLoader::encodeWav (buffer, 48000.0).getSize() == 0, "Reject NaN samples");
    buffer.setSample (0, 0, std::numeric_limits<float>::infinity());
    checks.expect (dualir::IRLoader::encodeWav (buffer, 48000.0).getSize() == 0, "Reject infinite samples");

    juce::WavAudioFormat wav;
    const auto invalidSamples = makeFixture (wav, buffer, 48000.0, true);
    checks.expect (! dualir::IRLoader::loadEncoded (invalidSamples.getData(), invalidSamples.getSize()).succeeded(),
                   "Decoder rejects infinite samples");
    buffer.clear();
    const auto silentSamples = makeFixture (wav, buffer, 48000.0, true);
    checks.expect (! dualir::IRLoader::loadEncoded (silentSamples.getData(), silentSamples.getSize()).succeeded(),
                   "Decoder rejects silent samples");
    buffer.setSample (0, 0, 0.5f);
    const auto lowRate = makeFixture (wav, buffer, 4000.0, true);
    checks.expect (! dualir::IRLoader::loadEncoded (lowRate.getData(), lowRate.getSize()).succeeded(),
                   "Decoder rejects unsupported sample rate");

    juce::AiffAudioFormat aiff;
    const auto aiffBytes = makeFixture (aiff, buffer, 44100.0, false);
    const auto aiffDecoded = dualir::IRLoader::loadEncoded (aiffBytes.getData(), aiffBytes.getSize());
    checks.expect (aiffDecoded.succeeded() && juce::exactlyEqual (aiffDecoded.sampleRate, 44100.0),
                   "Decoder supports AIFF");
    if (aiffDecoded.succeeded())
        checks.expect (std::abs (aiffDecoded.samples.getSample (0, 0) - 0.5f) < 0.000001f,
                       "Preserve AIFF amplitude");

    juce::AudioBuffer<float> tooLong (1, 96001);
    tooLong.clear();
    tooLong.setSample (0, 0, 1.0f);
    checks.expect (dualir::IRLoader::encodeWav (tooLong, 48000.0).getSize() == 0, "Reject IR longer than 2s");
    const auto tooLongBytes = makeFixture (wav, tooLong, 48000.0, true);
    checks.expect (! dualir::IRLoader::loadEncoded (tooLongBytes.getData(), tooLongBytes.getSize()).succeeded(),
                   "Decoder rejects IR longer than 2s");
    juce::AudioBuffer<float> surround (3, 1);
    surround.clear();
    surround.setSample (0, 0, 1.0f);
    checks.expect (dualir::IRLoader::encodeWav (surround, 48000.0).getSize() == 0, "Reject more than two channels");

    std::cout << checks.total - checks.failed << '/' << checks.total << " checks passed\n";
    return checks.failed == 0 ? 0 : 1;
}
