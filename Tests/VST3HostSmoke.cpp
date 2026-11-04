// A small native VST3 host. This deliberately does not link DualIR's shared code:
// it loads the produced plug-in binary and checks its exported ABI and audio path.
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

namespace
{
using namespace Steinberg;
using namespace Steinberg::Vst;

// Explicit inline identifiers avoid linking SDK .cpp files or static FUID data.
constexpr TUID componentIid = INLINE_UID (0xE831FF31, 0xF2D54301, 0x928EBBEE, 0x25697802);
constexpr TUID processorIid = INLINE_UID (0x42043F99, 0xB7DA453C, 0xA569E79D, 0x9AAEC33D);

struct Checks
{
    void require (bool condition, const std::string& description)
    {
        ++count;
        if (! condition)
            throw std::runtime_error (description);
    }

    void ok (tresult result, const std::string& description)
    {
        require (result == kResultOk, description + " (result " + std::to_string (result) + ")");
    }

    int count = 0;
};

class NativeModule
{
public:
    explicit NativeModule (const char* path)
    {
       #if defined(_WIN32)
        handle = LoadLibraryA (path);
        if (handle == nullptr)
            throw std::runtime_error ("LoadLibrary failed: " + std::to_string (GetLastError()));
       #else
        handle = dlopen (path, RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr)
            throw std::runtime_error (std::string ("dlopen failed: ") + dlerror());
       #endif
    }

    ~NativeModule()
    {
        if (entered && exitFunction != nullptr)
            exitFunction();
       #if defined(_WIN32)
        if (handle != nullptr)
            FreeLibrary (handle);
       #else
        if (handle != nullptr)
            dlclose (handle);
       #endif
    }

    NativeModule (const NativeModule&) = delete;
    NativeModule& operator= (const NativeModule&) = delete;

    template <typename Function>
    Function symbol (const char* name) const
    {
       #if defined(_WIN32)
        const auto result = GetProcAddress (handle, name);
       #else
        const auto result = dlsym (handle, name);
       #endif
        if (result == nullptr)
            throw std::runtime_error (std::string ("Missing VST3 export: ") + name);
        return reinterpret_cast<Function> (result);
    }

    void enter (Checks& checks)
    {
       #if defined(_WIN32)
        exitFunction = symbol<bool (*)()> ("ExitDll");
        entered = symbol<bool (*)()> ("InitDll")();
       #elif defined(__linux__) || defined(__FreeBSD__)
        exitFunction = symbol<bool (*)()> ("ModuleExit");
        entered = symbol<bool (*) (void*)> ("ModuleEntry") (handle);
       #else
        throw std::runtime_error ("Native VST3 host smoke currently supports Windows and Linux/BSD.");
       #endif
        checks.require (entered, "VST3 module initialization succeeds");
    }

private:
   #if defined(_WIN32)
    HMODULE handle = nullptr;
   #else
    void* handle = nullptr;
   #endif
    bool (*exitFunction)() = nullptr;
    bool entered = false;
};

struct FactoryOwner
{
    ~FactoryOwner() { if (factory != nullptr) factory->release(); }
    IPluginFactory* factory = nullptr;
};

struct Instance
{
    ~Instance()
    {
        if (processing && processor != nullptr)
            processor->setProcessing (false);
        if (active && component != nullptr)
            component->setActive (false);
        if (initialized && component != nullptr)
            component->terminate();
        if (processor != nullptr)
            processor->release();
        if (component != nullptr)
            component->release();
    }

    IComponent* component = nullptr;
    IAudioProcessor* processor = nullptr;
    bool initialized = false;
    bool active = false;
    bool processing = false;
};

void checkAudioLayout (Checks& checks, Instance& plugin, int channels)
{
    auto arrangement = channels == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
    const auto label = std::to_string (channels) + " channel(s)";
    checks.ok (plugin.processor->setBusArrangements (&arrangement, 1, &arrangement, 1),
               "Set matched bus arrangement: " + label);
    for (const auto direction : { kInput, kOutput })
    {
        BusInfo info {};
        checks.ok (plugin.component->getBusInfo (kAudio, direction, 0, info), "Read bus info");
        checks.require (info.channelCount == channels, "Bus channel count matches " + label);
        checks.ok (plugin.component->activateBus (kAudio, direction, 0, true), "Activate audio bus");
        SpeakerArrangement actual {};
        checks.ok (plugin.processor->getBusArrangement (direction, 0, actual), "Read arrangement");
        checks.require (actual == arrangement, "Reported arrangement matches selected layout");
    }

    ProcessSetup setup { kRealtime, kSample32, 64, 48000.0 };
    checks.ok (plugin.processor->setupProcessing (setup), "Set up 48 kHz / 64-frame float processing");
    checks.require (plugin.processor->getLatencySamples() == 0, "VST3 reports zero latency");
    checks.ok (plugin.component->setActive (true), "Activate component");
    plugin.active = true;
    checks.ok (plugin.processor->setProcessing (true), "Start processing");
    plugin.processing = true;

    std::array<std::array<float, 64>, 2> input {}, output {};
    std::array<float*, 2> inputPointers { input[0].data(), input[1].data() };
    std::array<float*, 2> outputPointers { output[0].data(), output[1].data() };
    AudioBusBuffers inputBus, outputBus;
    inputBus.numChannels = channels;
    outputBus.numChannels = channels;
    inputBus.channelBuffers32 = inputPointers.data();
    outputBus.channelBuffers32 = outputPointers.data();

    ProcessData data;
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inputBus;
    data.outputs = &outputBus;

    // Separate buffers exercise the actual wrapper's host-buffer mapping.
    for (const auto blockSize : { 64, 1, 64, 0 })
    {
        data.numSamples = blockSize;
        inputBus.silenceFlags = 0;
        outputBus.silenceFlags = 0;
        for (int channel = 0; channel < channels; ++channel)
            for (int sample = 0; sample < blockSize; ++sample)
            {
                input[static_cast<size_t> (channel)][static_cast<size_t> (sample)]
                    = static_cast<float> ((sample * 3 + channel * 5) % 29 - 14) / 32.0f;
                output[static_cast<size_t> (channel)][static_cast<size_t> (sample)] = 123.0f;
            }

        checks.ok (plugin.processor->process (data), "Process host audio block");
        bool transparent = true;
        for (int channel = 0; channel < channels; ++channel)
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto expected = static_cast<float> ((sample * 3 + channel * 5) % 29 - 14) / 32.0f * std::pow (10.0f, -0.6f);
                const auto value = output[static_cast<size_t> (channel)][static_cast<size_t> (sample)];
                transparent = transparent && std::isfinite (value) && std::abs (value - expected) < 1.0e-6f;
            }
        checks.require (transparent, "Native VST3 applies calibrated -12 dB output: " + label);
    }

    for (auto& channel : input)
        channel.fill (0.0f);
    data.numSamples = 64;
    inputBus.silenceFlags = channels == 1 ? 1 : 3;
    checks.ok (plugin.processor->process (data), "Process silent block");
    bool silent = true;
    for (int channel = 0; channel < channels; ++channel)
        for (const auto sample : output[static_cast<size_t> (channel)])
            silent = silent && std::isfinite (sample) && std::abs (sample) < 1.0e-6f;
    checks.require (silent, "Silence remains silent through native VST3");

    checks.ok (plugin.processor->setProcessing (false), "Stop processing");
    plugin.processing = false;
    checks.ok (plugin.component->setActive (false), "Deactivate component");
    plugin.active = false;
}

void checkPlugin (Checks& checks, NativeModule& module)
{
    using GetFactory = IPluginFactory* (PLUGIN_API*)();
    FactoryOwner owner { module.symbol<GetFactory> ("GetPluginFactory")() };
    checks.require (owner.factory != nullptr, "GetPluginFactory creates the native VST3 factory");
    PFactoryInfo factoryInfo {};
    checks.ok (owner.factory->getFactoryInfo (&factoryInfo), "Read factory metadata");
    checks.require (std::strcmp (factoryInfo.vendor, "RottingPitStudio") == 0,
                    "Factory reports RottingPitStudio as developer");

    PClassInfo effectClass {};
    bool found = false;
    const auto classCount = owner.factory->countClasses();
    checks.require (classCount > 0 && classCount < 100, "Factory exposes a bounded class list");
    for (int32 index = 0; index < classCount; ++index)
    {
        PClassInfo info {};
        checks.ok (owner.factory->getClassInfo (index, &info), "Enumerate VST3 class");
        if (std::strcmp (info.category, kVstAudioEffectClass) == 0)
        {
            effectClass = info;
            found = true;
        }
    }
    checks.require (found, "Factory contains an audio module class");
    checks.require (std::strcmp (effectClass.name, "RottingIR") == 0, "Audio module is named RottingIR");

    constexpr TUID stableComponent = INLINE_UID (0xABCDEF01, 0x9182FAEB, 0x446C6972, 0x44754952);
    checks.require (std::memcmp (effectClass.cid, stableComponent, sizeof (TUID)) == 0,
                    "Stable RottingIR VST3 component class ID is preserved");

    Instance plugin;
    checks.ok (owner.factory->createInstance (effectClass.cid, componentIid,
                                              reinterpret_cast<void**> (&plugin.component)),
               "Create native IComponent");
    checks.require (plugin.component != nullptr, "Component pointer is valid");
    // JUCE accepts a null host context for this minimal processing-only host.
    checks.ok (plugin.component->initialize (nullptr), "Initialize component without DAW services");
    plugin.initialized = true;
    checks.ok (plugin.component->queryInterface (processorIid,
                                                reinterpret_cast<void**> (&plugin.processor)),
               "Query IAudioProcessor using VST3 ABI");
    checks.require (plugin.processor != nullptr, "Processor pointer is valid");
    checks.require (plugin.component->getBusCount (kAudio, kInput) == 1
                    && plugin.component->getBusCount (kAudio, kOutput) == 1,
                    "Component exposes one main input and one main output bus");
    checks.ok (plugin.processor->canProcessSampleSize (kSample32), "Supports float32 processing");
    checkAudioLayout (checks, plugin, 1);
    checkAudioLayout (checks, plugin, 2);
    checks.ok (plugin.component->terminate(), "Terminate component cleanly");
    plugin.initialized = false;
}
}

int main (int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: DualIRVST3HostSmoke <native VST3 module binary>\n";
        return 2;
    }

    Checks checks;
    try
    {
        NativeModule module (argv[1]);
        module.enter (checks);
        checkPlugin (checks, module);
        std::cout << checks.count << '/' << checks.count << " native VST3 host checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL after " << checks.count << " native VST3 host checks: " << error.what() << '\n';
        return 1;
    }
}
