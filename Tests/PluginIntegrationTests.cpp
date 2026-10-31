#include "../Source/PluginProcessor.h"
#include "../Source/IR/IRLoader.h"

#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <string>

namespace
{
constexpr int blockSize = 128;
constexpr double sampleRate = 48000.0;

struct Checks
{
    void expect (bool result, const std::string& description)
    {
        ++total;
        if (! result)
        {
            ++failed;
            std::cerr << "FAIL: " << description << '\n';
        }
    }

    int total = 0;
    int failed = 0;
};

struct Fixture
{
    explicit Fixture (const juce::String& name, const juce::AudioBuffer<float>& samples)
        : file (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("DualIR-integration-" + name, ".wav", false))
    {
        const auto bytes = dualir::IRLoader::encodeWav (samples, sampleRate);
        valid = bytes.getSize() > 0 && file.replaceWithData (bytes.getData(), bytes.getSize());
    }

    ~Fixture() { file.deleteFile(); }

    juce::File file;
    bool valid = false;
};

bool waitFor (const std::function<bool()>& condition)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        if (condition())
            return true;
        juce::Thread::sleep (1);
    }
    return condition();
}

bool slotReady (const DualIRAudioProcessor& processor, int slot, bool loaded)
{
    const auto status = processor.getIRStatus (slot);
    return ! status.loading && status.loaded == loaded;
}

bool setParameter (DualIRAudioProcessor& processor, const juce::String& id, float value)
{
    if (auto* parameter = processor.parameters.getParameter (id))
    {
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        return true;
    }
    return false;
}

float parameterValue (const DualIRAudioProcessor& processor, const juce::String& id)
{
    if (const auto* value = processor.parameters.getRawParameterValue (id))
        return value->load();
    return std::numeric_limits<float>::quiet_NaN();
}

void resetParameters (DualIRAudioProcessor& processor, Checks& checks)
{
    for (const auto& [id, value] : std::array<std::pair<const char*, float>, 9> {
             std::pair { "aGain", 0.0f }, { "bGain", 0.0f },
             { "aEnabled", 1.0f }, { "bEnabled", 1.0f },
             { "aInvert", 0.0f }, { "bInvert", 0.0f },
             { "blend", 0.5f }, { "output", 0.0f }, { "bypass", 0.0f } })
        checks.expect (setParameter (processor, id, value), std::string ("Parameter exists: ") + id);
}

void settle (DualIRAudioProcessor& processor)
{
    juce::AudioBuffer<float> silence (processor.getTotalNumOutputChannels(), blockSize);
    juce::MidiBuffer midi;
    // 8192 samples exceed the parameter and convolution crossfade windows.
    for (int block = 0; block < 64; ++block)
    {
        silence.clear();
        processor.processBlock (silence, midi);
    }
}

juce::AudioBuffer<float> renderImpulse (DualIRAudioProcessor& processor)
{
    juce::AudioBuffer<float> audio (processor.getTotalNumOutputChannels(), blockSize);
    audio.clear();
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        audio.setSample (channel, 0, 1.0f);
    juce::MidiBuffer midi;
    processor.processBlock (audio, midi);
    return audio;
}

bool matches (const juce::AudioBuffer<float>& actual,
              const juce::AudioBuffer<float>& a, float gainA,
              const juce::AudioBuffer<float>& b, float gainB,
              float tolerance = 0.00003f)
{
    for (int channel = 0; channel < actual.getNumChannels(); ++channel)
        for (int sample = 0; sample < actual.getNumSamples(); ++sample)
        {
            const auto av = sample < a.getNumSamples() ? a.getSample (channel, sample) : 0.0f;
            const auto bv = sample < b.getNumSamples() ? b.getSample (channel, sample) : 0.0f;
            const auto expected = av * gainA + bv * gainB;
            const auto value = actual.getSample (channel, sample);
            if (! std::isfinite (value) || std::abs (value - expected) > tolerance)
                return false;
        }
    return true;
}

bool finite (const juce::AudioBuffer<float>& audio)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            if (! std::isfinite (audio.getSample (channel, sample)))
                return false;
    return true;
}

juce::AudioBuffer<float> impulseA()
{
    juce::AudioBuffer<float> samples (2, 8);
    samples.clear();
    samples.setSample (0, 0, 0.75f);
    samples.setSample (0, 3, -0.25f);
    samples.setSample (1, 1, 0.25f);
    return samples;
}

juce::AudioBuffer<float> impulseB()
{
    juce::AudioBuffer<float> samples (2, 8);
    samples.clear();
    samples.setSample (0, 0, 0.25f);
    samples.setSample (0, 2, 0.5f);
    samples.setSample (1, 0, -0.5f);
    samples.setSample (1, 4, 0.25f);
    return samples;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    Checks checks;
    const auto a = impulseA();
    const auto b = impulseB();
    Fixture fileA ("A", a);
    Fixture fileB ("B", b);
    checks.expect (fileA.valid && fileB.valid, "Generate independent stereo WAV IR fixtures");
    if (! fileA.valid || ! fileB.valid)
        return 1;

    DualIRAudioProcessor processor;
    resetParameters (processor, checks);
    processor.requestIRLoad (0, fileA.file);
    processor.requestIRLoad (1, fileB.file);
    const auto loaded = waitFor ([&] { return slotReady (processor, 0, true)
                                          && slotReady (processor, 1, true); });
    checks.expect (loaded, "Both asynchronous file loads complete within 5 seconds");
    if (! loaded)
        return 1;

    const auto statusA = processor.getIRStatus (0);
    const auto statusB = processor.getIRStatus (1);
    checks.expect (statusA.error.isEmpty() && statusB.error.isEmpty(), "Successful loads report no error");
    checks.expect (statusA.numChannels == 2 && statusB.numChannels == 2
                   && statusA.numSamples == 8 && statusB.numSamples == 8,
                   "IR status exposes decoded dimensions");
    checks.expect (juce::exactlyEqual (statusA.sampleRate, sampleRate)
                   && juce::exactlyEqual (statusB.sampleRate, sampleRate),
                   "IR status exposes original sample rates");
    processor.prepareToPlay (sampleRate, blockSize);
    settle (processor);
    checks.expect (matches (renderImpulse (processor), a, 0.5f, b, 0.5f),
                   "Processor outputs the expected stereo dual-cab convolution");

    {
        juce::AudioBuffer<float> monoIR (1, 8);
        monoIR.clear();
        monoIR.setSample (0, 0, 0.5f);
        monoIR.setSample (0, 2, 0.25f);
        Fixture monoFile ("mono", monoIR);
        checks.expect (monoFile.valid, "Generate a mono cabinet IR fixture");
        DualIRAudioProcessor monoProcessor;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::mono());
        layout.outputBuses.add (juce::AudioChannelSet::mono());
        const auto monoLayout = monoProcessor.setBusesLayout (layout);
        checks.expect (monoLayout, "Apply host mono input/output layout");
        monoProcessor.requestIRLoad (0, monoFile.file);
        const auto monoLoaded = waitFor ([&] { return slotReady (monoProcessor, 0, true); });
        checks.expect (monoLoaded, "Load mono IR asynchronously");
        if (monoLayout && monoLoaded)
        {
            monoProcessor.prepareToPlay (sampleRate, blockSize);
            settle (monoProcessor);
            const auto monoOutput = renderImpulse (monoProcessor);
            checks.expect (monoOutput.getNumChannels() == 1
                           && matches (monoOutput, monoIR, 1.0f, monoIR, 0.0f),
                           "Mono processor returns the expected FIR response");
        }

        struct StartupCase
        {
            const char* name;
            bool enabled, bypass;
            float gainDb, outputDb;
        };
        juce::AudioBuffer<float> dryImpulse (1, 8);
        dryImpulse.clear();
        dryImpulse.setSample (0, 0, 1.0f);
        for (const auto& test : std::array<StartupCase, 3> {
                 StartupCase { "Muted cabinet", false, false, 0.0f, 0.0f },
                 { "Bypass with output attenuation", true, true, 0.0f, -24.0f },
                 { "Cabinet and output attenuation", true, false, -6.0f, -6.0f } })
        {
            DualIRAudioProcessor startup;
            const auto startupMono = startup.setBusesLayout (layout);
            checks.expect (startupMono, std::string (test.name) + ": mono startup layout");
            if (! startupMono)
                continue;
            startup.requestIRLoad (0, monoFile.file);
            const auto startupLoaded = waitFor ([&] { return slotReady (startup, 0, true); });
            checks.expect (startupLoaded, std::string (test.name) + ": load IR before prepare");
            if (! startupLoaded)
                continue;

            setParameter (startup, "aEnabled", test.enabled ? 1.0f : 0.0f);
            setParameter (startup, "bypass", test.bypass ? 1.0f : 0.0f);
            setParameter (startup, "aGain", test.gainDb);
            setParameter (startup, "output", test.outputDb);
            startup.prepareToPlay (sampleRate, blockSize);
            // Deliberately no settling blocks: host settings must be respected
            // from sample zero, rather than fading from hardcoded defaults.
            const auto firstBlock = renderImpulse (startup);
            const auto expectedGain = test.enabled
                ? juce::Decibels::decibelsToGain (test.gainDb)
                    * juce::Decibels::decibelsToGain (test.outputDb)
                : 0.0f;
            checks.expect (test.bypass
                               ? matches (firstBlock, dryImpulse, 1.0f, dryImpulse, 0.0f)
                               : matches (firstBlock, monoIR, expectedGain, monoIR, 0.0f),
                           std::string (test.name) + ": settings apply from the first prepared audio sample");
        }
    }

    setParameter (processor, "aGain", 6.0f);
    setParameter (processor, "bGain", -6.0f);
    setParameter (processor, "blend", 0.25f);
    setParameter (processor, "output", -3.0f);
    settle (processor);
    const auto gainA = 0.75f * juce::Decibels::decibelsToGain (6.0f)
                            * juce::Decibels::decibelsToGain (-3.0f);
    const auto gainB = 0.25f * juce::Decibels::decibelsToGain (-6.0f)
                            * juce::Decibels::decibelsToGain (-3.0f);
    checks.expect (matches (renderImpulse (processor), a, gainA, b, gainB),
                   "Host-automated blend and dB gains reach their expected targets");

    setParameter (processor, "aInvert", 1.0f);
    settle (processor);
    checks.expect (matches (renderImpulse (processor), a, -gainA, b, gainB),
                   "A polarity inversion changes convolution sign");
    setParameter (processor, "bEnabled", 0.0f);
    settle (processor);
    checks.expect (matches (renderImpulse (processor), a, -gainA, b, 0.0f),
                   "Muting B preserves the selected A mix weight");

    setParameter (processor, "bEnabled", 1.0f);
    setParameter (processor, "bInvert", 1.0f);
    setParameter (processor, "aEnabled", 0.0f);
    settle (processor);
    checks.expect (matches (renderImpulse (processor), a, 0.0f, b, -gainB),
                   "B polarity and A mute are independently automated");

    setParameter (processor, "bypass", 1.0f);
    setParameter (processor, "output", 6.0f);
    settle (processor);
    auto dry = renderImpulse (processor);
    bool bypassIsDry = true;
    for (int channel = 0; channel < dry.getNumChannels(); ++channel)
        for (int sample = 0; sample < dry.getNumSamples(); ++sample)
            bypassIsDry = bypassIsDry && std::abs (dry.getSample (channel, sample)
                                                  - (sample == 0 ? 1.0f : 0.0f)) < 0.00003f;
    checks.expect (bypassIsDry, "Bypass returns dry audio independently of output gain");

    resetParameters (processor, checks);
    setParameter (processor, "aGain", 3.0f);
    setParameter (processor, "bGain", -4.0f);
    setParameter (processor, "aInvert", 1.0f);
    setParameter (processor, "blend", 0.2f);
    setParameter (processor, "output", -2.0f);
    settle (processor);
    const auto savedA = -0.8f * juce::Decibels::decibelsToGain (3.0f)
                             * juce::Decibels::decibelsToGain (-2.0f);
    const auto savedB = 0.2f * juce::Decibels::decibelsToGain (-4.0f)
                            * juce::Decibels::decibelsToGain (-2.0f);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    checks.expect (state.getSize() > 0, "Serialize nonempty APVTS and embedded IR state");
    checks.expect (fileA.file.deleteFile() && fileB.file.deleteFile(),
                   "Remove original IR files before project recall");

    DualIRAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    // A host can save again immediately after restoring, while IR decoding is
    // still pending. The desired embedded snapshot must survive that save.
    juce::MemoryBlock immediateState;
    restored.getStateInformation (immediateState);
    checks.expect (immediateState.getSize() > 0, "Save immediately after state restore");
    DualIRAudioProcessor immediateRecall;
    immediateRecall.setStateInformation (immediateState.getData(), static_cast<int> (immediateState.getSize()));
    const auto immediateLoaded = waitFor ([&] { return slotReady (immediateRecall, 0, true)
                                                   && slotReady (immediateRecall, 1, true); });
    checks.expect (immediateLoaded, "Immediate re-save retains both pending embedded IRs");
    if (immediateLoaded)
    {
        immediateRecall.prepareToPlay (sampleRate, blockSize);
        settle (immediateRecall);
        checks.expect (matches (renderImpulse (immediateRecall), a, savedA, b, savedB),
                       "Immediate state re-save preserves decoded audio and controls");
    }

    const auto restoredLoaded = waitFor ([&] { return slotReady (restored, 0, true)
                                                  && slotReady (restored, 1, true); });
    checks.expect (restoredLoaded, "Restore both embedded IRs without original files");
    if (! restoredLoaded)
        return 1;
    for (const auto* id : { "aGain", "bGain", "aEnabled", "bEnabled", "aInvert", "bInvert",
                            "blend", "output", "bypass" })
        checks.expect (std::abs (parameterValue (restored, id) - parameterValue (processor, id)) < 0.00001f,
                       std::string ("Restore host parameter ") + id);
    restored.prepareToPlay (sampleRate, blockSize);
    checks.expect (matches (renderImpulse (restored), a, savedA, b, savedB),
                   "Restored gain, polarity, blend and output apply from the first prepared audio sample");
    settle (restored);
    checks.expect (matches (renderImpulse (restored), a, savedA, b, savedB),
                   "Embedded IR recall restores the expected convolution output");

    const char invalidState[] = "not a DualIR state";
    restored.setStateInformation (invalidState, static_cast<int> (sizeof (invalidState)));
    settle (restored);
    checks.expect (matches (renderImpulse (restored), a, savedA, b, savedB),
                   "Malformed state does not replace working IRs or parameters");

    const auto missing = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getNonexistentChildFile ("DualIR-missing-IR", ".wav", false);
    restored.requestIRLoad (0, missing);
    checks.expect (waitFor ([&] { return ! restored.getIRStatus (0).loading; }),
                   "Missing-file load finishes with an error");
    const auto failedLoad = restored.getIRStatus (0);
    checks.expect (failedLoad.loaded && failedLoad.error.isNotEmpty(),
                   "Failed load reports an error while retaining the previous IR");
    settle (restored);
    checks.expect (matches (renderImpulse (restored), a, savedA, b, savedB),
                   "Invalid file replacement preserves working convolution audio");

    resetParameters (restored, checks);
    restored.clearIR (0);
    checks.expect (waitFor ([&] { return slotReady (restored, 0, false); }),
                   "Clear A completes asynchronously");
    restored.releaseResources();
    restored.prepareToPlay (sampleRate, blockSize);
    settle (restored);
    checks.expect (matches (renderImpulse (restored), a, 0.0f, b, 1.0f),
                   "Remaining B receives full weight after A is cleared");

    // A separate instance exercises producer cancellation and running-audio swaps.
    Fixture rapidA ("rapid-A", a);
    Fixture rapidB ("rapid-B", b);
    checks.expect (rapidA.valid && rapidB.valid, "Create rapid-reload fixtures");
    DualIRAudioProcessor swapping;
    for (int request = 0; request < 12; ++request)
    {
        swapping.requestIRLoad (0, rapidA.file);
        swapping.clearIR (0);
    }
    swapping.requestIRLoad (0, rapidB.file);
    checks.expect (waitFor ([&] { return slotReady (swapping, 0, true); }),
                   "Rapid requests finish with the final load");
    swapping.prepareToPlay (sampleRate, blockSize);
    settle (swapping);
    checks.expect (matches (renderImpulse (swapping), a, 0.0f, b, 1.0f),
                   "Last load request wins over obsolete loads and clears");

    swapping.requestIRLoad (0, rapidA.file);
    bool allFinite = true;
    const auto liveSwapSucceeded = waitFor ([&]
    {
        const auto output = renderImpulse (swapping);
        allFinite = allFinite && finite (output);
        return slotReady (swapping, 0, true) && matches (output, a, 1.0f, b, 0.0f);
    });
    checks.expect (allFinite, "Live IR swap produces finite output during processing");
    checks.expect (liveSwapSucceeded, "Running convolution converges to the replacement IR within 5 seconds");
    swapping.clearIR (0);
    swapping.requestIRLoad (0, rapidB.file);
    swapping.clearIR (0);
    checks.expect (waitFor ([&] { return slotReady (swapping, 0, false); }),
                   "Final clear wins over obsolete load requests");
    swapping.releaseResources();
    swapping.prepareToPlay (sampleRate, blockSize);
    settle (swapping);
    const auto cleared = renderImpulse (swapping);
    bool clearIsDry = true;
    for (int channel = 0; channel < cleared.getNumChannels(); ++channel)
        for (int sample = 0; sample < cleared.getNumSamples(); ++sample)
            clearIsDry = clearIsDry && std::abs (cleared.getSample (channel, sample)
                                                - (sample == 0 ? 1.0f : 0.0f)) < 0.00003f;
    checks.expect (clearIsDry, "Clearing both empty slots returns transparent dry audio");

    std::cout << checks.total - checks.failed << '/' << checks.total << " checks passed\n";
    return checks.failed == 0 ? 0 : 1;
}
