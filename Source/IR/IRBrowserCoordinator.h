#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <memory>
#include <vector>
#include <array>
#include <algorithm>

// Browser state is independent of the audio engine. Sync changes never call a
// browser's user-change handler and never request an IR load.
class IRBrowserCoordinator final : public juce::ChangeBroadcaster
{
public:
    enum class Change { user, sync };
    using Files = std::vector<juce::File>;
    struct Snapshot
    {
        std::array<juce::File, 2> folders;
        std::array<std::shared_ptr<const Files>, 2> files;
        bool linkFolders = true;
        int lastActiveBrowser = -1;
    };
    Snapshot snapshot() const { const juce::ScopedLock guard (lock); return state; }
    void onFolderChanged (int slot, const juce::File& folder, Change source = Change::user)
    {
        if (slot < 0 || slot > 1) return;
        {
            const juce::ScopedLock guard (lock);
            if (source == Change::user) state.lastActiveBrowser = slot;
            assign (slot, folder);
            if (state.linkFolders) assign (1 - slot, folder);
        }
        sendChangeMessage();
    }
    void setLinked (bool linked)
    {
        {
            const juce::ScopedLock guard (lock);
            if (state.linkFolders == linked) return;
            state.linkFolders = linked;
            if (linked)
            {
                const int active = state.lastActiveBrowser == 1 ? 1 : 0;
                assign (1 - active, state.folders[static_cast<size_t> (active)]);
            }
        }
        sendChangeMessage();
    }
    void save (juce::ValueTree& tree) const
    {
        const auto s = snapshot();
        tree.setProperty ("linkFolders", s.linkFolders, nullptr);
        tree.setProperty ("folderA", s.folders[0].getFullPathName(), nullptr);
        tree.setProperty ("folderB", s.folders[1].getFullPathName(), nullptr);
        tree.setProperty ("lastActiveBrowser", s.lastActiveBrowser, nullptr);
    }
    void restore (const juce::ValueTree& tree)
    {
        {
            const juce::ScopedLock guard (lock);
            state = {};
            state.linkFolders = static_cast<bool> (tree.getProperty ("linkFolders", true));
            const int active = static_cast<int> (tree.getProperty ("lastActiveBrowser", -1));
            state.lastActiveBrowser = active == 0 || active == 1 ? active : -1;
            auto a = juce::File (tree.getProperty ("folderA").toString());
            auto b = juce::File (tree.getProperty ("folderB").toString());
            if (state.linkFolders) a = b = state.lastActiveBrowser == 1 ? b : a;
            assign (0, a); assign (1, b);
        }
        sendChangeMessage();
    }
private:
    void assign (int slot, const juce::File& folder)
    {
        const auto index = static_cast<size_t> (slot), other = 1u - index;
        if (state.folders[index] == folder && state.files[index]) return;
        state.folders[index] = folder;
        if (state.folders[other] == folder && state.files[other])
            state.files[index] = state.files[other];
        else
        {
            auto files = std::make_shared<Files>();
            if (folder.isDirectory())
            {
                for (const auto& file : folder.findChildFiles (juce::File::findFiles, false))
                    if (file.hasFileExtension ("wav;aif;aiff")) files->push_back (file);
                std::sort (files->begin(), files->end(), [] (const auto& a, const auto& b)
                    { return a.getFileName().compareNatural (b.getFileName()) < 0; });
            }
            state.files[index] = std::move (files);
        }
    }
    mutable juce::CriticalSection lock;
    Snapshot state;
};
