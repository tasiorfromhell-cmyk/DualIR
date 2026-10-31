#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class DualIRAudioProcessor;
class MainPanel final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit MainPanel (DualIRAudioProcessor&);
    ~MainPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;
private:
    class SlotPanel;
    void timerCallback() override;
    static bool isAudioFile (const juce::String&);
    DualIRAudioProcessor& owner;
    juce::LookAndFeel_V4 lookAndFeel;
    std::unique_ptr<SlotPanel> slotA, slotB;
    juce::Slider blend, output;
    juce::ToggleButton bypass { "BYPASS" };
    juce::Label blendLabel, outputLabel, routingLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> blendAttachment, outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    juce::TooltipWindow tooltips { this, 650 };
    float inputLevel = 0.0f, outputLevel = 0.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};
