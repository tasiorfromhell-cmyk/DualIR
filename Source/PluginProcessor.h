#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/DualCabEngine.h"
#include <array>
#include <atomic>
#include <optional>

class DualIRAudioProcessor final : public juce::AudioProcessor, private juce::Thread
{
public:
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;
    DualIRAudioProcessor();
    ~DualIRAudioProcessor() override;
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;
    void getStateInformation (juce::MemoryBlock& destinationData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    struct IRStatus
    {
        juce::String name, error;
        bool loaded = false, loading = false;
        double sampleRate = 0.0;
        int numSamples = 0, numChannels = 0;
    };
    // Message/host-state thread APIs: never call these from processBlock.
    void requestIRLoad (int slot, const juce::File& file);
    void clearIR (int slot);
    IRStatus getIRStatus (int slot) const;
    float getInputPeak() const noexcept;
    float getOutputPeak() const noexcept;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState parameters;

private:
    enum class RequestKind { file, encoded, clear };
    struct StoredIR { juce::MemoryBlock encoded; juce::String name; };
    struct Request
    {
        RequestKind kind = RequestKind::clear;
        juce::File file;
        StoredIR stored;
        juce::uint64 generation = 0;
    };
    struct Slot
    {
        IRStatus status;
        StoredIR active;
        // Restore/clear remains serialisable while its worker job is pending.
        std::optional<StoredIR> desired;
        std::optional<Request> pending;
        juce::uint64 generation = 0;
    };
    struct RawCabParameters
    {
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* inverted = nullptr;
    };
    void run() override;
    void enqueue (int slot, Request request, std::optional<StoredIR> desired);
    dualir::EngineSettings readSettings (bool forceBypass = false) const noexcept;
    void processAudio (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept;
    static bool validSlot (int slot) noexcept;

    dualir::DualCabEngine engine;
    std::array<RawCabParameters, 2> rawCabs;
    std::atomic<float>* rawBlend = nullptr;
    std::atomic<float>* rawOutput = nullptr;
    std::atomic<float>* rawBypass = nullptr;
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    mutable juce::CriticalSection slotLock;
    std::array<Slot, 2> slots;
    juce::WaitableEvent loaderWake;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DualIRAudioProcessor)
};
