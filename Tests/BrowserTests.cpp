#include "../Source/PluginProcessor.h"
#include "../Source/IR/IRLoader.h"
#include <cmath>
#include <functional>
#include <iostream>

namespace
{
int total = 0, failed = 0;
void check (bool value, const char* message)
{
    ++total;
    if (! value) { ++failed; std::cerr << "FAIL: " << message << '\n'; }
}
bool waitFor (const std::function<bool()>& ready)
{
    const auto end = juce::Time::getMillisecondCounterHiRes() + 10000.0;
    while (juce::Time::getMillisecondCounterHiRes() < end)
    {
        if (ready()) return true;
        juce::Thread::sleep (2);
    }
    return ready();
}
bool scanned (DualIRAudioProcessor& p)
{
    return waitFor ([&] { return ! p.getBrowserState (0).scanning && ! p.getBrowserState (1).scanning; });
}
bool loaded (DualIRAudioProcessor& p, int slot, const juce::File& file)
{
    return waitFor ([&] { const auto s = p.getIRStatus (slot);
        return ! s.loading && s.loaded && s.path == file.getFullPathName(); });
}
juce::MemoryBlock save (DualIRAudioProcessor& p)
{
    juce::MemoryBlock state; p.getStateInformation (state); return state;
}
struct Folders
{
    juce::File root = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("RottingIR-browser-tests", {}, false);
    ~Folders() { root.deleteRecursively(); }
};
}
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    Folders fixture;
    const auto a = fixture.root.getChildFile ("A"), b = fixture.root.getChildFile ("B");
    check (a.createDirectory() && b.createDirectory(), "Create isolated browser folders");
    juce::AudioBuffer<float> ir (1, 32); ir.clear(); ir.setSample (0, 0, 0.5f);
    const auto encoded = dualir::IRLoader::encodeWav (ir, 48000.0);
    for (const auto& file : { a.getChildFile ("IR10.wav"), a.getChildFile ("IR2.wav"),
                             a.getChildFile ("IR1.wav"), b.getChildFile ("Other.wav") })
        check (file.replaceWithData (encoded.getData(), encoded.getSize()), "Create real WAV fixture");
    // Extension coverage, including case, must not depend on the host filesystem's wildcard case rules.
    a.getChildFile ("Extra.AIF").replaceWithData (encoded.getData(), encoded.getSize());
    a.getChildFile ("Extra.AIFF").replaceWithData (encoded.getData(), encoded.getSize());
    a.getChildFile ("ignore.txt").replaceWithText ("ignore");
    a.getChildFile ("Subfolder.wav").createDirectory();
    DualIRAudioProcessor p;
    check (p.getBrowserState (0).linked, "Link defaults ON");
    p.setBrowserFolder (0, a);
    check (scanned (p), "A folder scan completes outside the audio callback");
    auto sa = p.getBrowserState (0), sb = p.getBrowserState (1);
    check (sa.files->size() == 5 && sb.files->size() == 5, "Folder A lists all and only WAV/AIF/AIFF files");
    check (sa.folder == a.getFullPathName() && sa.folder == sb.folder, "Link ON synchronizes A to B");
    check ((*sa.files)[2].getFileName() == "IR1.wav" && (*sa.files)[3].getFileName() == "IR2.wav"
        && (*sa.files)[4].getFileName() == "IR10.wav", "Natural IR1 IR2 IR10 ordering");
    p.setBrowserFolder (1, b);
    check (scanned (p) && p.getBrowserState (0).folder == b.getFullPathName()
        && p.getBrowserState (1).files->size() == 1, "Folder B lists IRs and synchronizes B to A");
    p.setLinkFolders (false);
    p.setBrowserFolder (0, a);
    check (scanned (p) && p.getBrowserState (1).folder == b.getFullPathName(), "Link OFF keeps independent folders");
    p.selectBrowserIR (0, 2);
    check (loaded (p, 0, a.getChildFile ("IR1.wav")), "Selecting a row loads the exact WAV asynchronously");
    p.selectBrowserIR (1, 0);
    check (loaded (p, 1, b.getChildFile ("Other.wav")), "CAB B selection loads independently");
    p.stepBrowserIR (0, 1);
    check (loaded (p, 0, a.getChildFile ("IR2.wav")), "Next selects the next natural filename");
    p.stepBrowserIR (0, -1);
    check (loaded (p, 0, a.getChildFile ("IR1.wav")), "Previous selects preceding IR");
    p.selectBrowserIR (0, 4);
    check (loaded (p, 0, a.getChildFile ("IR10.wav")), "Load final row");
    p.stepBrowserIR (0, 1);
    check (loaded (p, 0, a.getChildFile ("Extra.AIF")), "Next wraps last to first");
    p.stepBrowserIR (0, -1);
    check (loaded (p, 0, a.getChildFile ("IR10.wav")), "Previous wraps first to last");
    const auto beforeB = p.getIRStatus (1).path;
    p.setLinkFolders (true);
    check (scanned (p) && p.getBrowserState (1).folder == a.getFullPathName(),
           "OFF to ON uses last manually selected browser folder");
    check (p.getIRStatus (1).path == beforeB && p.getIRStatus (1).loaded,
           "Linked folder change preserves the other CAB active IR outside the folder");
    p.setLinkFolders (false);
    check (p.getBrowserState (0).folder == p.getBrowserState (1).folder,
           "ON to OFF retains both current folders");
    p.requestIRLoad (1, b.getChildFile ("Other.wav"));
    check (loaded (p, 1, b.getChildFile ("Other.wav")) && scanned (p)
        && p.getBrowserState (1).folder == b.getFullPathName()
        && p.getBrowserState (1).files->size() == 1, "Direct/drop loading updates and scans browser folder");
    const auto state = save (p);
    DualIRAudioProcessor recalled;
    recalled.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (scanned (recalled) && loaded (recalled, 0, a.getChildFile ("IR10.wav"))
        && loaded (recalled, 1, b.getChildFile ("Other.wav")), "Session recall restores folders and both embedded IRs asynchronously");
    check (! recalled.getBrowserState (0).linked && recalled.getBrowserState (0).folder == a.getFullPathName()
        && recalled.getBrowserState (1).folder == b.getFullPathName(), "Recall restores link OFF and independent folder paths");
    recalled.setLinkFolders (true);
    check (scanned (recalled) && recalled.getBrowserState (0).folder == b.getFullPathName(),
           "Recall retains last manually changed CAB for OFF to ON");
    const auto linkedState = save (recalled);
    DualIRAudioProcessor linkedRecall;
    linkedRecall.setStateInformation (linkedState.getData(), static_cast<int> (linkedState.getSize()));
    check (scanned (linkedRecall) && linkedRecall.getBrowserState (0).linked
        && linkedRecall.getBrowserState (0).folder == linkedRecall.getBrowserState (1).folder,
        "Session recall restores link ON");
    // Previous releases used the same schema and embedded IR nodes, without browser properties.
    const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    check (xml != nullptr, "Read serialized state for backwards-compatibility fixture");
    if (xml != nullptr)
    {
        for (const auto* property : { "folderA", "folderB", "activeIRA", "activeIRB", "linkFolders", "lastBrowser" })
            xml->removeAttribute (property);
        for (auto* node : xml->getChildIterator())
            if (node->hasTagName ("IR")) node->removeAttribute ("path");
        juce::MemoryBlock legacyState;
        juce::AudioProcessor::copyXmlToBinary (*xml, legacyState);
        DualIRAudioProcessor legacy;
        legacy.setStateInformation (legacyState.getData(), static_cast<int> (legacyState.getSize()));
        check (waitFor ([&] { return legacy.getIRStatus (0).loaded && legacy.getIRStatus (1).loaded
            && ! legacy.getIRStatus (0).loading && ! legacy.getIRStatus (1).loading; }),
            "Legacy v1.0 state restores both embedded IRs without browser fields");
        check (legacy.getBrowserState (0).linked, "Legacy state defaults Link to ON");
        legacy.prepareToPlay (48000.0, 64);
        juce::AudioBuffer<float> signal (2, 64); signal.clear(); signal.setSample (0, 0, 1.0f);
        juce::MidiBuffer midi; legacy.processBlock (signal, midi);
        check (std::abs (signal.getSample (0, 0) - 0.5f * std::pow (10.0f, -0.6f)) < 1.0e-6f,
            "Legacy preset produces original cabinet response with the requested -12 dB calibration");
    }
    // Prove embedded recall survives missing source files/directories.
    a.deleteRecursively(); b.deleteRecursively();
    DualIRAudioProcessor missing;
    missing.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    check (scanned (missing) && loaded (missing, 0, a.getChildFile ("IR10.wav"))
        && loaded (missing, 1, b.getChildFile ("Other.wav")), "Missing folders never discard embedded cabinet audio");
    check (missing.getBrowserState (0).files->empty() && missing.getBrowserState (0).error.isNotEmpty(),
           "Missing folder reports an error with an empty list");
    // Thousands of entries, repeated folder changes, and stale scan cancellation.
    const auto large = fixture.root.getChildFile ("large"); large.createDirectory();
    for (int i = 0; i < 3000; ++i) large.getChildFile ("IR" + juce::String (i) + ".wav").create();
    p.setBrowserFolder (0, large);
    check (scanned (p) && p.getBrowserState (0).files->size() == 3000,
           "Virtualized browser model supports 3000 naturally sorted files");
    const auto snapshot = p.getBrowserState (0).files;
    check ((*snapshot)[2].getFileName() == "IR2.wav" && (*snapshot)[10].getFileName() == "IR10.wav",
           "Large folder retains natural ordering");
    p.setBrowserFolder (0, large); p.setBrowserFolder (0, fixture.root); p.setBrowserFolder (0, large);
    check (scanned (p) && p.getBrowserState (0).folder == large.getFullPathName()
        && p.getBrowserState (0).files->size() == 3000, "Latest folder request wins over obsolete scans");
    // Exact -12 dB calibration of BOTH CABs, mix, existing output trim and meter.
    p.parameters.getParameter ("output")->setValueNotifyingHost (
        p.parameters.getParameter ("output")->convertTo0to1 (6.0f));
    p.parameters.getParameter ("blend")->setValueNotifyingHost (0.25f);
    p.prepareToPlay (48000.0, 128);
    juce::AudioBuffer<float> audio (2, 128); juce::MidiBuffer midi;
    audio.clear(); audio.setSample (0, 0, 1.0f); audio.setSample (1, 0, 1.0f);
    p.processBlock (audio, midi);
    const auto previousVersionLevel = 0.5f * std::pow (10.0f, 6.0f / 20.0f);
    const auto ratio = audio.getSample (0, 0) / previousVersionLevel;
    check (std::abs (20.0f * std::log10 (ratio) + 12.0f) < 0.0001f,
           "Final mixed and trimmed output is exactly 12 dB below previous-version golden response");
    check (std::abs (p.getOutputPeak() - audio.getMagnitude (0, 128)) < 1.0e-7f,
           "OUTPUT meter measures the attenuated host signal");
    check (std::abs (p.parameters.getRawParameterValue ("output")->load() - 6.0f) < 0.00001f,
           "Calibration does not alter the existing OUTPUT parameter");
    // Existing parameter identities and values survive browser navigation and live switching.
    check (p.getParameters().size() == 9, "No existing DSP parameters removed or new output knob added");
    for (const auto* id : { "aGain", "bGain", "aEnabled", "bEnabled", "aInvert", "bInvert", "blend", "output", "bypass" })
        check (p.parameters.getParameter (id) != nullptr, "Existing stable parameter ID is retained");
    ir.setSample (0, 0, 0.25f);
    const auto alternateData = dualir::IRLoader::encodeWav (ir, 48000.0);
    const auto alternate = fixture.root.getChildFile ("Switch.wav");
    alternate.replaceWithData (alternateData.getData(), alternateData.getSize());
    auto fillDC = [&] { for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (audio.getWritePointer (c), 1.0f, 128); };
    for (int block = 0; block < 32; ++block) { fillDC(); p.processBlock (audio, midi); }
    auto previousSample = audio.getSample (0, 127);
    p.requestIRLoad (0, alternate);
    float largestStep = 0.0f;
    const auto target = (0.25f * 0.75f + 0.5f * 0.25f) * std::pow (10.0f, -6.0f / 20.0f);
    const auto converged = waitFor ([&]
    {
        fillDC(); p.processBlock (audio, midi);
        for (int sample = 0; sample < 128; ++sample)
        {
            const auto value = audio.getSample (0, sample);
            largestStep = juce::jmax (largestStep, std::abs (value - previousSample));
            previousSample = value;
        }
        return ! p.getIRStatus (0).loading && std::abs (previousSample - target) < 1.0e-5f;
    });
    check (converged, "Live convolution switch converges without resetting mix or output trim");
    check (largestStep < 0.01f, "Live IR change crossfades instead of stepping on a constant input");
    check (std::abs (p.parameters.getRawParameterValue ("blend")->load() - 0.25f) < 1.0e-6f
        && std::abs (p.parameters.getRawParameterValue ("output")->load() - 6.0f) < 1.0e-6f,
        "Changing IR preserves DSP parameters");
    std::cout << total - failed << '/' << total << " browser/recall/calibration checks passed\n";
    return failed == 0 ? 0 : 1;
}
