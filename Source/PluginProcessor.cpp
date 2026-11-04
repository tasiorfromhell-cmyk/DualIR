#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "IR/IRLoader.h"
#include <utility>
#include <algorithm>

namespace
{
constexpr auto stateType = "DualIRState";
constexpr int stateVersion = 1;
constexpr int maxStateBytes = 48 * 1024 * 1024;
}

DualIRAudioProcessor::DualIRAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("DualIR IR loader"),
      parameters (*this, nullptr, stateType, createParameterLayout())
{
    for (int slot = 0; slot < 2; ++slot)
    {
        const juce::String prefix = slot == 0 ? "a" : "b";
        auto& raw = rawCabs[static_cast<size_t> (slot)];
        raw.gain = parameters.getRawParameterValue (prefix + "Gain");
        raw.enabled = parameters.getRawParameterValue (prefix + "Enabled");
        raw.inverted = parameters.getRawParameterValue (prefix + "Invert");
    }
    rawBlend = parameters.getRawParameterValue ("blend");
    rawOutput = parameters.getRawParameterValue ("output");
    rawBypass = parameters.getRawParameterValue ("bypass");
    startThread();
}

DualIRAudioProcessor::~DualIRAudioProcessor()
{
    signalThreadShouldExit();
    loaderWake.signal();
    // Join bounded IR work before the engine and slot buffers are destroyed.
    stopThread (-1);
}

juce::AudioProcessorValueTreeState::ParameterLayout DualIRAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto* prefix : { "a", "b" })
    {
        const juce::String id (prefix);
        const auto name = "Cab " + id.toUpperCase();
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id + "Gain", 1 }, name + " gain",
            juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id + "Enabled", 1 }, name + " enabled", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id + "Invert", 1 }, name + " invert polarity", false));
    }
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "blend", 1 }, "A/B blend",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 }, "Output gain",
        juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass", false));
    return layout;
}

void DualIRAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    engine.prepare (sampleRate, juce::jmax (1, maximumExpectedSamplesPerBlock),
                    getTotalNumOutputChannels(), readSettings());
    setLatencySamples (0);
    inputPeak.store (0.0f, std::memory_order_relaxed);
    outputPeak.store (0.0f, std::memory_order_relaxed);
}

void DualIRAudioProcessor::releaseResources()
{
    engine.reset();
    inputPeak.store (0.0f, std::memory_order_relaxed);
    outputPeak.store (0.0f, std::memory_order_relaxed);
}

bool DualIRAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& input = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();
    return input == output && (output == juce::AudioChannelSet::mono()
                              || output == juce::AudioChannelSet::stereo());
}

dualir::EngineSettings DualIRAudioProcessor::readSettings (bool forceBypass) const noexcept
{
    const auto readCab = [] (const RawCabParameters& raw)
    {
        return dualir::CabSettings { raw.gain->load (std::memory_order_relaxed),
                                    raw.enabled->load (std::memory_order_relaxed) >= 0.5f,
                                    raw.inverted->load (std::memory_order_relaxed) >= 0.5f };
    };
    dualir::EngineSettings settings;
    settings.a = readCab (rawCabs[0]);
    settings.b = readCab (rawCabs[1]);
    settings.blend = rawBlend->load (std::memory_order_relaxed);
    settings.outputDb = rawOutput->load (std::memory_order_relaxed);
    settings.bypass = forceBypass || rawBypass->load (std::memory_order_relaxed) >= 0.5f;
    return settings;
}

void DualIRAudioProcessor::processAudio (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept
{
    const juce::ScopedNoDenormals noDenormals;
    for (int channel = getTotalNumInputChannels();
         channel < juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels()); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    const auto magnitude = [&buffer]
    {
        return buffer.getNumSamples() > 0 && buffer.getNumChannels() > 0
                   ? buffer.getMagnitude (0, buffer.getNumSamples()) : 0.0f;
    };
    inputPeak.store (magnitude(), std::memory_order_relaxed);
    engine.process (buffer, readSettings (forceBypass));
    outputPeak.store (magnitude(), std::memory_order_relaxed);
}

void DualIRAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    midiMessages.clear();
    processAudio (buffer, false);
}
void DualIRAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midiMessages)
{
    midiMessages.clear();
    processAudio (buffer, true);
}
juce::AudioProcessorParameter* DualIRAudioProcessor::getBypassParameter() const
{
    return parameters.getParameter ("bypass");
}
bool DualIRAudioProcessor::validSlot (int slot) noexcept { return slot >= 0 && slot < 2; }

void DualIRAudioProcessor::enqueue (int slotIndex, Request request, std::optional<StoredIR> desired)
{
    if (! validSlot (slotIndex))
        return;
    {
        const juce::ScopedLock lock (slotLock);
        auto& slot = slots[static_cast<size_t> (slotIndex)];
        if (! isThreadRunning())
        {
            slot.status.error = "The IR loading thread is unavailable.";
            return;
        }
        slot.status.requestedPath = request.stored.path;
        request.generation = ++slot.generation;
        slot.pending = std::move (request);
        slot.desired = std::move (desired);
        slot.status.loading = true;
        slot.status.error.clear();
    }
    loaderWake.signal();
}
void DualIRAudioProcessor::requestIRLoad (int slot, const juce::File& file)
{
    if (! validSlot (slot)) return;
    if (getBrowserState (slot).folder != file.getParentDirectory().getFullPathName())
        setBrowserFolder (slot, file.getParentDirectory());
    Request request;
    request.kind = RequestKind::file;
    request.file = file;
    request.stored.name = file.getFileName();
    request.stored.path = file.getFullPathName();
    enqueue (slot, std::move (request), std::nullopt);
}
void DualIRAudioProcessor::clearIR (int slot)
{
    enqueue (slot, Request {}, StoredIR {});
}
DualIRAudioProcessor::IRStatus DualIRAudioProcessor::getIRStatus (int slot) const
{
    if (! validSlot (slot))
        return {};
    const juce::ScopedLock lock (slotLock);
    return slots[static_cast<size_t> (slot)].status;
}
float DualIRAudioProcessor::getInputPeak() const noexcept
{
    return inputPeak.load (std::memory_order_relaxed);
}
float DualIRAudioProcessor::getOutputPeak() const noexcept
{
    return outputPeak.load (std::memory_order_relaxed);
}

void DualIRAudioProcessor::queueScanLocked (int slot, const juce::File& folder)
{
    auto& browser = browsers[static_cast<size_t> (slot)];
    browser.folder = folder == juce::File {} ? juce::String {} : folder.getFullPathName();
    browser.files = std::make_shared<const std::vector<juce::File>>();
    browser.error.clear();
    browser.scanning = true;
    ++browser.generation;
    ++browser.revision;
    browser.pendingScan = folder;
}

void DualIRAudioProcessor::setBrowserFolder (int slot, const juce::File& folder)
{
    if (! validSlot (slot)) return;
    {
        const juce::ScopedLock lock (slotLock);
        lastBrowser = slot;
        for (int index = 0; index < 2; ++index)
            if (index == slot || linkFolders)
                queueScanLocked (index, folder);
    }
    loaderWake.signal();
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void DualIRAudioProcessor::setLinkFolders (bool linked)
{
    {
        const juce::ScopedLock lock (slotLock);
        if (linkFolders == linked) return;
        linkFolders = linked;
        if (linked)
        {
            const auto folder = browsers[static_cast<size_t> (lastBrowser)].folder;
            queueScanLocked (1 - lastBrowser, folder.isEmpty() ? juce::File {} : juce::File (folder));
        }
    }
    loaderWake.signal();
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

DualIRAudioProcessor::BrowserState DualIRAudioProcessor::getBrowserState (int slot) const
{
    if (! validSlot (slot)) return {};
    const juce::ScopedLock lock (slotLock);
    const auto& browser = browsers[static_cast<size_t> (slot)];
    const auto& cab = slots[static_cast<size_t> (slot)];
    return { browser.folder, cab.status.loading ? cab.status.requestedPath : cab.status.path, browser.error, browser.files,
             browser.scanning, linkFolders, browser.revision };
}

void DualIRAudioProcessor::selectBrowserIR (int slot, int row)
{
    const auto browser = getBrowserState (slot);
    if (browser.files != nullptr && row >= 0 && row < static_cast<int> (browser.files->size()))
        requestIRLoad (slot, (*browser.files)[static_cast<size_t> (row)]);
}

void DualIRAudioProcessor::stepBrowserIR (int slot, int direction)
{
    const auto browser = getBrowserState (slot);
    if (browser.files == nullptr || browser.files->empty()) return;
    const auto count = static_cast<int> (browser.files->size());
    int row = -1;
    for (int index = 0; index < count; ++index)
        if ((*browser.files)[static_cast<size_t> (index)].getFullPathName() == browser.activeIR) row = index;
    const auto next = row < 0 ? (direction < 0 ? count - 1 : 0) : (row + (direction < 0 ? -1 : 1) + count) % count;
    selectBrowserIR (slot, next);
}

bool DualIRAudioProcessor::scanBrowsers()
{
    bool didWork = false;
    for (int index = 0; index < 2 && ! threadShouldExit(); ++index)
    {
        std::optional<juce::File> folder;
        juce::uint64 generation = 0;
        {
            const juce::ScopedLock lock (slotLock);
            auto& browser = browsers[static_cast<size_t> (index)];
            folder.swap (browser.pendingScan);
            generation = browser.generation;
        }
        if (! folder) continue;
        didWork = true;
        auto files = std::make_shared<std::vector<juce::File>>();
        juce::String error;
        if (*folder != juce::File {})
        {
            if (! folder->isDirectory()) error = "Folder unavailable; embedded IR remains active.";
            else
                for (const auto& entry : juce::RangedDirectoryIterator (*folder, false, "*", juce::File::findFiles))
                {
                    if (threadShouldExit()) return didWork;
                    const auto file = entry.getFile();
                    if (file.hasFileExtension ("wav;aif;aiff")) files->push_back (file);
                }
        }
        std::sort (files->begin(), files->end(), [] (const auto& a, const auto& b)
        {
            return a.getFileName().compareNatural (b.getFileName()) < 0;
        });
        const juce::ScopedLock lock (slotLock);
        auto& browser = browsers[static_cast<size_t> (index)];
        if (browser.generation != generation) continue;
        browser.files = std::move (files);
        browser.error = error;
        browser.scanning = false;
        ++browser.revision;
    }
    return didWork;
}

void DualIRAudioProcessor::run()
{
    while (! threadShouldExit())
    {
        bool didWork = scanBrowsers();
        for (int slotIndex = 0; slotIndex < 2 && ! threadShouldExit(); ++slotIndex)
        {
            std::optional<Request> request;
            {
                const juce::ScopedLock lock (slotLock);
                request.swap (slots[static_cast<size_t> (slotIndex)].pending);
            }
            if (! request)
                continue;
            didWork = true;
            auto result = request->kind == RequestKind::file
                            ? dualir::IRLoader::loadFile (request->file)
                            : request->kind == RequestKind::encoded
                                  ? dualir::IRLoader::loadEncoded (request->stored.encoded.getData(),
                                                                  request->stored.encoded.getSize())
                                  : dualir::IRLoader::Result {};
            if (request->kind != RequestKind::clear && result.succeeded())
            {
                request->stored.encoded = dualir::IRLoader::encodeWav (result.samples, result.sampleRate);
                if (request->stored.encoded.getSize() == 0)
                    result.error = "The impulse response could not be prepared for project saving.";
            }
            if (threadShouldExit())
                return;

            {
                const juce::ScopedLock lock (slotLock);
                auto& slot = slots[static_cast<size_t> (slotIndex)];
                if (request->generation != slot.generation)
                    continue; // A newer selection superseded this decode.

                slot.status.loading = false;
                slot.desired.reset();
                if (request->kind == RequestKind::clear)
                {
                    engine.queueImpulse (slotIndex, {}, 44100.0, false);
                    slot.active = {};
                    slot.status = {};
                }
                else if (result.succeeded())
                {
                    slot.status.name = request->stored.name;
                    slot.status.path = request->stored.path;
                    slot.status.error.clear();
                    slot.status.loaded = true;
                    slot.status.sampleRate = result.sampleRate;
                    slot.status.numSamples = result.samples.getNumSamples();
                    slot.status.numChannels = result.samples.getNumChannels();
                    engine.queueImpulse (slotIndex, std::move (result.samples), result.sampleRate);
                    slot.active = std::move (request->stored);
                }
                else
                {
                    // Preserve the last valid cab and saved samples on failure.
                    slot.status.error = result.error;
                }
            }
            // A completed async load must dirty the host state even if the folder
            // did not change, so a save after decoding includes the new IR.
            updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
        }
        if (! didWork)
            loaderWake.wait (1000);
    }
}

void DualIRAudioProcessor::getStateInformation (juce::MemoryBlock& destinationData)
{
    auto state = parameters.copyState();
    state.setProperty ("schemaVersion", stateVersion, nullptr);
    for (int child = state.getNumChildren(); --child >= 0;)
        if (state.getChild (child).hasType ("IR"))
            state.removeChild (child, nullptr);

    std::array<StoredIR, 2> snapshot;
    {
        const juce::ScopedLock lock (slotLock);
        for (size_t index = 0; index < slots.size(); ++index)
            snapshot[index] = slots[index].desired ? *slots[index].desired : slots[index].active;
        state.setProperty ("folderA", browsers[0].folder, nullptr);
        state.setProperty ("folderB", browsers[1].folder, nullptr);
        state.setProperty ("activeIRA", snapshot[0].path, nullptr);
        state.setProperty ("activeIRB", snapshot[1].path, nullptr);
        state.setProperty ("linkFolders", linkFolders, nullptr);
        state.setProperty ("lastBrowser", lastBrowser, nullptr);
    }
    for (int slot = 0; slot < 2; ++slot)
    {
        const auto& stored = snapshot[static_cast<size_t> (slot)];
        juce::ValueTree node ("IR");
        node.setProperty ("slot", slot, nullptr);
        node.setProperty ("name", stored.name, nullptr);
        node.setProperty ("path", stored.path, nullptr);
        node.setProperty ("data", juce::Base64::toBase64 (stored.encoded.getData(),
                                                        stored.encoded.getSize()), nullptr);
        state.addChild (node, -1, nullptr);
    }
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destinationData);
}

void DualIRAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0 || sizeInBytes > maxStateBytes)
        return;
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (stateType))
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid() || static_cast<int> (state.getProperty ("schemaVersion", 0)) != stateVersion)
        return;

    std::array<StoredIR, 2> restored;
    std::array<bool, 2> seen {};
    for (int child = state.getNumChildren(); --child >= 0;)
    {
        const auto node = state.getChild (child);
        if (! node.hasType ("IR"))
            continue;
        const auto slotIndex = static_cast<int> (node.getProperty ("slot", -1));
        if (! validSlot (slotIndex) || seen[static_cast<size_t> (slotIndex)])
            return;
        const auto index = static_cast<size_t> (slotIndex);
        seen[index] = true;
        auto& stored = restored[index];
        stored.name = node.getProperty ("name").toString().substring (0, 512);
        stored.path = node.getProperty ("path", state.getProperty (slotIndex == 0 ? "activeIRA" : "activeIRB")).toString().substring (0, 32768);
        const auto encoded = node.getProperty ("data").toString();
        constexpr auto maxEncodedCharacters = ((dualir::IRLoader::maxEncodedBytes + 2u) / 3u) * 4u;
        if (encoded.length() > static_cast<int> (maxEncodedCharacters) || encoded.length() % 4 != 0)
            return;
        bool decoded = false;
        {
            juce::MemoryOutputStream output (stored.encoded, false);
            decoded = juce::Base64::convertFromBase64 (output, encoded);
        }
        if (! decoded || stored.encoded.getSize() > dualir::IRLoader::maxEncodedBytes)
            return;
        state.removeChild (child, nullptr);
    }
    parameters.replaceState (state);
    {
        const juce::ScopedLock lock (slotLock);
        linkFolders = static_cast<bool> (state.getProperty ("linkFolders", true));
        lastBrowser = juce::jlimit (0, 1, static_cast<int> (state.getProperty ("lastBrowser", 0)));
        for (int index = 0; index < 2; ++index)
        {
            const auto path = state.getProperty (index == 0 ? "folderA" : "folderB").toString().substring (0, 32768);
            queueScanLocked (index, path.isEmpty() ? juce::File {} : juce::File (path));
        }
    }
    loaderWake.signal();
    for (int slot = 0; slot < 2; ++slot)
    {
        auto& stored = restored[static_cast<size_t> (slot)];
        if (stored.encoded.getSize() == 0)
        {
            clearIR (slot);
            continue;
        }
        Request request;
        request.kind = RequestKind::encoded;
        request.stored = stored;
        enqueue (slot, std::move (request), std::move (stored));
    }
}

juce::AudioProcessorEditor* DualIRAudioProcessor::createEditor()
{
    return new DualIRAudioProcessorEditor (*this);
}
bool DualIRAudioProcessor::hasEditor() const { return true; }
const juce::String DualIRAudioProcessor::getName() const { return JucePlugin_Name; }
bool DualIRAudioProcessor::acceptsMidi() const { return false; }
bool DualIRAudioProcessor::producesMidi() const { return false; }
bool DualIRAudioProcessor::isMidiEffect() const { return false; }
double DualIRAudioProcessor::getTailLengthSeconds() const { return dualir::IRLoader::maxDurationSeconds; }
int DualIRAudioProcessor::getNumPrograms() { return 1; }
int DualIRAudioProcessor::getCurrentProgram() { return 0; }
void DualIRAudioProcessor::setCurrentProgram (int index) { juce::ignoreUnused (index); }
const juce::String DualIRAudioProcessor::getProgramName (int index) { juce::ignoreUnused (index); return {}; }
void DualIRAudioProcessor::changeProgramName (int index, const juce::String& name)
{
    juce::ignoreUnused (index, name);
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DualIRAudioProcessor(); }
