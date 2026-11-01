#include "PluginEditor.h"

DualIRAudioProcessorEditor::DualIRAudioProcessorEditor (DualIRAudioProcessor& audioProcessor)
    : AudioProcessorEditor (audioProcessor), mainPanel (audioProcessor)
{
    setOpaque (true);
    addAndMakeVisible (mainPanel);
    setResizable (true, true);
    setResizeLimits (800, 640, 1600, 1280);
    getConstrainer()->setFixedAspectRatio (1.25);
    setSize (800, 640);
}
void DualIRAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour (0xff13181f));
}
void DualIRAudioProcessorEditor::resized()
{
    mainPanel.setBounds (0, 0, 800, 640);
    mainPanel.setTransform (juce::AffineTransform::scale (static_cast<float> (getWidth()) / 800.0f,
                                                        static_cast<float> (getHeight()) / 640.0f));
}
