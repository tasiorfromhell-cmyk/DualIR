#pragma once
#include "PluginProcessor.h"
#include "GUI/MainPanel.h"

class DualIRAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit DualIRAudioProcessorEditor (DualIRAudioProcessor& audioProcessor);
    ~DualIRAudioProcessorEditor() override = default;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    MainPanel mainPanel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DualIRAudioProcessorEditor)
};
