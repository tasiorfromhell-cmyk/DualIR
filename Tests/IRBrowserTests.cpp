#include "../Source/PluginProcessor.h"
#include "../Source/IR/IRLoader.h"
#include <iostream>

int main()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* message)
    { if (! ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; } };
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("DualIR-browser", "", false);
    const auto a = root.getChildFile ("A"), b = root.getChildFile ("B");
    a.createDirectory(); b.createDirectory();
    juce::AudioBuffer<float> impulse (1, 64); impulse.clear(); impulse.setSample (0, 0, 1.0f);
    const auto data = dualir::IRLoader::encodeWav (impulse, 48000.0);
    const auto irA = a.getChildFile ("first.wav"), irB = b.getChildFile ("second.wav");
    irA.replaceWithData (data.getData(), data.getSize());
    irB.replaceWithData (data.getData(), data.getSize());
    a.getChildFile ("ignored.txt").replaceWithText ("ignored");
    {
        IRBrowserCoordinator c;
        check (c.snapshot().linkFolders, "Link defaults ON");
        c.onFolderChanged (0, a);
        auto s = c.snapshot();
        check (s.folders[0] == a && s.folders[1] == a, "A synchronises B");
        check (s.files[0] == s.files[1] && s.files[0]->size() == 1, "One shared audio-only scan");
        c.onFolderChanged (1, b);
        check (c.snapshot().folders[0] == b, "B synchronises A");
        c.setLinked (false);
        check (c.snapshot().folders[0] == b && c.snapshot().folders[1] == b, "OFF retains both folders");
        c.onFolderChanged (0, a);
        check (c.snapshot().folders[1] == b, "OFF isolates A");
        c.onFolderChanged (1, b);
        c.setLinked (true);
        check (c.snapshot().folders[0] == b, "OFF to ON uses last user browser B");
        c.setLinked (false); c.onFolderChanged (0, a); c.setLinked (true);
        check (c.snapshot().folders[1] == a, "OFF to ON uses last user browser A");
        juce::ValueTree state ("test");
        state.setProperty ("folderA", a.getFullPathName(), nullptr);
        state.setProperty ("folderB", b.getFullPathName(), nullptr);
        c.restore (state);
        check (c.snapshot().linkFolders && c.snapshot().folders[1] == a, "Legacy restore defaults ON and prefers A");
        state.setProperty ("lastActiveBrowser", 1, nullptr); c.restore (state);
        check (c.snapshot().folders[0] == b, "Linked restore prefers B when saved last active");
        state.setProperty ("linkFolders", false, nullptr); c.restore (state);
        check (c.snapshot().folders[0] == a && c.snapshot().folders[1] == b, "Unlinked restore preserves both");
        c.onFolderChanged (0, a, IRBrowserCoordinator::Change::sync);
        check (c.snapshot().lastActiveBrowser == 1, "Sync does not overwrite user activity");
    }
    {
        DualIRAudioProcessor p;
        p.requestIRLoad (0, irA);
        auto wait = [&] (DualIRAudioProcessor& processor)
        {
            for (int i = 0; i < 5000; ++i)
            {
                if (! processor.getIRStatus (0).loading && ! processor.getIRStatus (1).loading) return true;
                juce::Thread::sleep (1);
            }
            return false;
        };
        check (wait (p) && p.getIRStatus (0).loaded, "Load A completes");
        check (! p.getIRStatus (1).loaded && p.irBrowsers.snapshot().folders[1] == a,
               "Linked file load changes peer folder without loading peer IR");
        p.irBrowsers.setLinked (false); p.requestIRLoad (1, irB);
        check (wait (p) && p.getIRStatus (1).loaded, "Load B completes");
        check (p.irBrowsers.snapshot().folders[0] == a, "Unlinked file load isolates folders");
        juce::MemoryBlock saved; p.getStateInformation (saved);
        DualIRAudioProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        check (wait (restored), "Restore completes");
        check (! restored.irBrowsers.snapshot().linkFolders
               && restored.irBrowsers.snapshot().folders[0] == a
               && restored.irBrowsers.snapshot().folders[1] == b, "Processor restores independent folders");
        restored.irBrowsers.setLinked (true);
        check (restored.irBrowsers.snapshot().folders[0] == b, "Restore retains last user browser");
        check (restored.getIRStatus (0).path == irA.getFullPathName()
               && restored.getIRStatus (1).path == irB.getFullPathName(), "Active IRs remain independent on relink");
        restored.getStateInformation (saved);
        p.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        check (wait (p) && p.irBrowsers.snapshot().linkFolders
               && p.irBrowsers.snapshot().folders[0] == b && p.irBrowsers.snapshot().folders[1] == b,
               "Linked processor state round trip");
        check (p.getIRStatus (0).path == irA.getFullPathName() && p.getIRStatus (1).path == irB.getFullPathName(),
               "State restores both active IRs separately");
        irA.deleteFile(); irB.deleteFile();
        p.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        check (wait (p) && p.getIRStatus (0).loaded && p.getIRStatus (1).loaded,
               "Embedded IR restore works without original files");
    }
    root.deleteRecursively();
    std::cout << "Browser failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
