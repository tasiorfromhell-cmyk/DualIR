#include "../Source/PluginProcessor.h"
#include "../Source/IR/IRLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

namespace
{
class Checks
{
public:
    void expect (bool condition, const std::string& description)
    {
        ++total;
        if (! condition)
        {
            ++failed;
            std::cerr << "FAIL: " << description << '\n';
        }
    }

    int finish() const
    {
        std::cout << total - failed << '/' << total << " checks passed\n";
        return failed == 0 ? 0 : 1;
    }

private:
    int total = 0;
    int failed = 0;
};

juce::AudioProcessor::BusesLayout makeLayout (const juce::AudioChannelSet& input,
                                             const juce::AudioChannelSet& output)
{
    juce::AudioProcessor::BusesLayout result;
    result.inputBuses.add (input);
    result.outputBuses.add (output);
    return result;
}

float testSample (int channel, int sample)
{
    // Exactly representable binary fractions make an exact pass-through check valid.
    return static_cast<float> (((sample * 7 + channel * 3) % 31) - 15) / 16.0f;
}

void checkAudio (Checks& checks, DualIRAudioProcessor& processor, int channels,
                 double sampleRate, int blockSize)
{
    const auto description = std::to_string (channels) + " channels, "
                           + std::to_string (static_cast<int> (sampleRate)) + " Hz, "
                           + std::to_string (blockSize) + " samples";

    processor.prepareToPlay (sampleRate, std::max (blockSize, 1));
    juce::AudioBuffer<float> buffer (channels, blockSize);
    juce::MidiBuffer midi;

    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < blockSize; ++sample)
            buffer.setSample (channel, sample, testSample (channel, sample));

    processor.processBlock (buffer, midi);

    bool identical = true;
    bool finite = true;
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto actual = buffer.getSample (channel, sample);
            identical = identical && juce::exactlyEqual (actual, testSample (channel, sample));
            finite = finite && std::isfinite (actual);
        }

    checks.expect (identical, "Signal remains identical: " + description);
    checks.expect (finite, "Signal contains no NaN or infinity: " + description);
    checks.expect (midi.isEmpty(), "No MIDI events generated: " + description);

    buffer.clear();
    processor.processBlock (buffer, midi);

    bool silent = true;
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto actual = buffer.getSample (channel, sample);
            silent = silent && std::isfinite (actual) && juce::exactlyEqual (actual, 0.0f);
        }

    checks.expect (silent, "Silence stays finite and silent: " + description);
    checks.expect (processor.getLatencySamples() == 0, "Zero latency: " + description);
    processor.releaseResources();
}

juce::Component* findComponent (juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findComponent (*child, id))
            return found;
    return nullptr;
}

bool waitForIRWorkers (DualIRAudioProcessor& processor)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        if (! processor.getIRStatus (0).loading && ! processor.getIRStatus (1).loading)
            return true;
        juce::Thread::sleep (1);
    }
    return false;
}

void checkEditorControls (Checks& checks, juce::AudioProcessorEditor& editor,
                          DualIRAudioProcessor& processor)
{
    for (const auto* id : { "aLoad", "bLoad", "aClear", "bClear", "aEnabled", "bEnabled",
                            "aInvert", "bInvert", "bypass" })
        checks.expect (dynamic_cast<juce::Button*> (findComponent (editor, id)) != nullptr,
                       "Editor contains working button " + std::string (id));

    struct SliderCheck { const char* id; double value; };
    for (const auto item : { SliderCheck { "aGain", -6.5 }, SliderCheck { "bGain", -3.0 },
                             SliderCheck { "blend", 0.25 }, SliderCheck { "output", -2.0 } })
    {
        auto* slider = dynamic_cast<juce::Slider*> (findComponent (editor, item.id));
        checks.expect (slider != nullptr, "Editor contains slider " + std::string (item.id));
        if (slider == nullptr)
            continue;
        if (juce::String (item.id) != "blend")
        {
            checks.expect (slider->getTextFromValue (0.0) == "0.0 dB",
                           "Gain display contains one dB unit");
            checks.expect (std::abs (slider->getValueFromText ("-6.5 dB") + 6.5) < 0.001,
                           "Gain text entry accepts dB values");
        }
        slider->setValue (item.value, juce::sendNotificationSync);
        const auto* parameter = processor.parameters.getRawParameterValue (item.id);
        checks.expect (parameter != nullptr
                       && std::abs (parameter->load() - static_cast<float> (item.value)) < 0.011f,
                       "Slider updates automatable parameter " + std::string (item.id));
    }

    struct ToggleCheck { const char* id; bool value; };
    for (const auto item : { ToggleCheck { "aEnabled", false }, ToggleCheck { "bEnabled", false },
                             ToggleCheck { "aInvert", true }, ToggleCheck { "bInvert", true },
                             ToggleCheck { "bypass", true } })
    {
        auto* button = dynamic_cast<juce::Button*> (findComponent (editor, item.id));
        if (button == nullptr)
            continue;
        button->setToggleState (item.value, juce::sendNotificationSync);
        const auto* parameter = processor.parameters.getRawParameterValue (item.id);
        checks.expect (parameter != nullptr && (parameter->load() > 0.5f) == item.value,
                       "Toggle updates automatable parameter " + std::string (item.id));
    }

    auto* gain = dynamic_cast<juce::Slider*> (findComponent (editor, "aGain"));
    auto* gainParameter = processor.parameters.getParameter ("aGain");
    if (gain != nullptr && gainParameter != nullptr)
    {
        gainParameter->setValueNotifyingHost (gainParameter->convertTo0to1 (-12.0f));
        checks.expect (std::abs (gain->getValue() + 12.0) < 0.011,
                       "Host parameter changes update the gain slider");
    }

    editor.setSize (800, 500);
    auto* panel = findComponent (editor, "mainPanel");
    auto* slotA = findComponent (editor, "aSlot");
    auto* slotB = findComponent (editor, "bSlot");
    checks.expect (panel != nullptr && slotA != nullptr && slotB != nullptr,
                   "Editor exposes main panel and both IR slots");
    auto* dropTarget = dynamic_cast<juce::FileDragAndDropTarget*> (panel);
    checks.expect (dropTarget != nullptr, "Main panel accepts IR file drag and drop");
    if (dropTarget == nullptr || slotA == nullptr || slotB == nullptr)
        return;

    checks.expect (waitForIRWorkers (processor), "Previous restore work finishes before editor load checks");
    juce::AudioBuffer<float> impulse (1, 64);
    impulse.clear();
    impulse.setSample (0, 0, 0.5f);
    impulse.setSample (0, 3, -0.25f);
    const auto encoded = dualir::IRLoader::encodeWav (impulse, 48000.0);
    juce::TemporaryFile first (".wav"), second (".wav");
    const auto firstWritten = first.getFile().replaceWithData (encoded.getData(), encoded.getSize());
    const auto secondWritten = second.getFile().replaceWithData (encoded.getData(), encoded.getSize());
    checks.expect (encoded.getSize() > 0 && firstWritten && secondWritten,
                   "Create temporary WAVs for actual editor loading");
    if (encoded.getSize() == 0 || ! firstWritten || ! secondWritten)
        return;

    const auto aPoint = panel->getLocalPoint (slotA, slotA->getLocalBounds().getCentre());
    const auto bPoint = panel->getLocalPoint (slotB, slotB->getLocalBounds().getCentre());
    const juce::StringArray firstPath { first.getFile().getFullPathName() };
    const juce::StringArray secondPath { second.getFile().getFullPathName() };
    checks.expect (dropTarget->isInterestedInFileDrag (firstPath), "Panel accepts a WAV drag");
    dropTarget->filesDropped (firstPath, aPoint.x, aPoint.y);
    checks.expect (waitForIRWorkers (processor), "IR A drop decoding completes within timeout");
    const auto loadedA = processor.getIRStatus (0);
    checks.expect (loadedA.loaded && loadedA.error.isEmpty()
                   && loadedA.name == first.getFile().getFileName(), "Drop on IR A loads the selected WAV");
    dropTarget->filesDropped (secondPath, bPoint.x, bPoint.y);
    checks.expect (waitForIRWorkers (processor), "IR B drop decoding completes within timeout");
    const auto loadedB = processor.getIRStatus (1);
    checks.expect (loadedB.loaded && loadedB.error.isEmpty()
                   && loadedB.name == second.getFile().getFileName(), "Drop on IR B loads the selected WAV");

    if (auto* clear = dynamic_cast<juce::Button*> (findComponent (editor, "aClear")))
    {
        checks.expect (static_cast<bool> (clear->onClick), "IR A clear button has an action");
        if (clear->onClick)
            clear->onClick();
        checks.expect (waitForIRWorkers (processor), "IR A clear completes within timeout");
        checks.expect (! processor.getIRStatus (0).loaded && processor.getIRStatus (1).loaded,
                       "Clear A removes its IR while preserving B");
    }

    // Dropping two files is a convenient way to fill the complete cabinet pair.
    const juce::StringArray bothPaths { second.getFile().getFullPathName(), first.getFile().getFullPathName() };
    dropTarget->filesDropped (bothPaths, aPoint.x, aPoint.y);
    checks.expect (waitForIRWorkers (processor), "Two-file drop decoding completes within timeout");
    const auto pairA = processor.getIRStatus (0);
    const auto pairB = processor.getIRStatus (1);
    checks.expect (pairA.loaded && pairB.loaded && pairA.error.isEmpty() && pairB.error.isEmpty()
                   && pairA.name == second.getFile().getFileName()
                   && pairB.name == first.getFile().getFileName(), "Two-file drop maps files to IR A and B");
}

void checkEditor (Checks& checks, DualIRAudioProcessor& processor)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    checks.expect (editor != nullptr, "Creates the custom editor");
    if (editor == nullptr)
        return;

    checks.expect (editor->getWidth() == 800 && editor->getHeight() == 500,
                   "Editor opens at 800 x 500");
    checks.expect (editor->isResizable(), "Editor supports resizing");

    const auto* constrainer = editor->getConstrainer();
    checks.expect (constrainer != nullptr, "Editor has a resize constrainer");
    if (constrainer != nullptr)
    {
        checks.expect (constrainer->getMinimumWidth() == 640
                       && constrainer->getMinimumHeight() == 400,
                       "Editor minimum dimensions are 640 x 400");
        checks.expect (constrainer->getMaximumWidth() == 1600
                       && constrainer->getMaximumHeight() == 1000,
                       "Editor maximum dimensions are 1600 x 1000");
    }

    for (const auto size : std::array { juce::Point<int> { 640, 400 },
                                       juce::Point<int> { 800, 500 },
                                       juce::Point<int> { 1000, 625 },
                                       juce::Point<int> { 1200, 750 },
                                       juce::Point<int> { 1400, 875 },
                                       juce::Point<int> { 1136, 710 },
                                       juce::Point<int> { 1600, 1000 } })
    {
        const auto description = std::to_string (size.x) + " x " + std::to_string (size.y);
        editor->setSize (size.x, size.y);
        checks.expect (editor->getWidth() == size.x && editor->getHeight() == size.y,
                       "Editor resizes to " + description);

        const auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds());
        const auto valid = snapshot.isValid();
        checks.expect (valid, "Editor paints a valid snapshot at " + description);
        if (! valid)
            continue;

        checks.expect (snapshot.getWidth() == size.x && snapshot.getHeight() == size.y,
                       "Snapshot matches editor dimensions at " + description);
        checks.expect (snapshot.getPixelAt (0, 0).getAlpha() == 255
                       && snapshot.getPixelAt (size.x / 2, size.y / 2).getAlpha() == 255
                       && snapshot.getPixelAt (size.x - 1, size.y - 1).getAlpha() == 255,
                       "Editor paints opaque background and content at " + description);

        const auto snapshotPath = juce::SystemStats::getEnvironmentVariable ("DUALIR_EDITOR_SNAPSHOT", {});
        if (snapshotPath.isNotEmpty())
        {
            juce::MemoryOutputStream pngData;
            juce::PNGImageFormat png;
            const auto encoded = png.writeImageToStream (snapshot, pngData);
            checks.expect (encoded && juce::File (snapshotPath + "-" + juce::String (size.x) + "x"
                                              + juce::String (size.y) + ".png").replaceWithData (
                               pngData.getData(), pngData.getDataSize()),
                           "Write requested editor snapshot");
        }
    }

    checkEditorControls (checks, *editor, processor);
}
}

int main (int argc, char* argv[])
{
    const auto testEditor = argc == 2 && std::string (argv[1]) == "--editor";
    if (argc > 1 && ! testEditor)
    {
        std::cerr << "Usage: " << argv[0] << " [--editor]\n";
        return 2;
    }

    // Core checks need no native window; --editor also renders offscreen snapshots.
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    Checks checks;
    DualIRAudioProcessor processor;

    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto surround = juce::AudioChannelSet::create5point1();
    checks.expect (processor.isBusesLayoutSupported (makeLayout (mono, mono)), "Accept mono");
    checks.expect (processor.isBusesLayoutSupported (makeLayout (stereo, stereo)), "Accept stereo");
    checks.expect (! processor.isBusesLayoutSupported (makeLayout (mono, stereo)),
                   "Reject mono-to-stereo mismatch");
    checks.expect (! processor.isBusesLayoutSupported (makeLayout (stereo, mono)),
                   "Reject stereo-to-mono mismatch");
    checks.expect (! processor.isBusesLayoutSupported (makeLayout (surround, surround)),
                   "Reject unsupported surround layout");
    checks.expect (! processor.isBusesLayoutSupported (
                       makeLayout (juce::AudioChannelSet::disabled(), juce::AudioChannelSet::disabled())),
                   "Reject disabled audio buses");

    for (const auto channels : std::array { 1, 2 })
    {
        const auto channelSet = channels == 1 ? mono : stereo;
        const auto layoutApplied = processor.setBusesLayout (makeLayout (channelSet, channelSet));
        checks.expect (layoutApplied, "Apply " + std::to_string (channels) + " channel layout");
        if (! layoutApplied)
            continue;

        for (const auto sampleRate : std::array { 44100.0, 48000.0, 96000.0 })
            for (const auto blockSize : std::array { 0, 1, 64, 512 })
                checkAudio (checks, processor, channels, sampleRate, blockSize);
    }

    checks.expect (! processor.acceptsMidi(), "Does not accept MIDI");
    checks.expect (! processor.producesMidi(), "Does not produce MIDI");
    checks.expect (! processor.isMidiEffect(), "Is an audio effect");
    checks.expect (juce::exactlyEqual (processor.getTailLengthSeconds(), 2.0),
                   "Reports the supported two-second IR tail");
    checks.expect (processor.hasEditor(), "Advertises a custom editor");
    checks.expect (processor.getName() == "RottingIR", "Reports expected plug-in name");
    checks.expect (processor.getNumPrograms() == 1, "Exposes one default program");
    checks.expect (processor.getCurrentProgram() == 0, "Selects the default program");

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    juce::MemoryBlock restoredState;
    processor.getStateInformation (restoredState);
    checks.expect (state == restoredState, "State remains stable after round-trip");
    checkAudio (checks, processor, processor.getTotalNumInputChannels(), 48000.0, 64);

    std::unique_ptr<juce::AudioProcessor> factoryProcessor (createPluginFilter());
    checks.expect (factoryProcessor != nullptr, "Plug-in factory creates a processor");
    if (factoryProcessor != nullptr)
    {
        checks.expect (factoryProcessor->getName() == "RottingIR", "Factory creates RottingIR");
        checks.expect (factoryProcessor->getTotalNumInputChannels() == 2
                       && factoryProcessor->getTotalNumOutputChannels() == 2,
                       "Factory defaults to stereo input and output");
    }

    if (testEditor)
        checkEditor (checks, processor);

    return checks.finish();
}
