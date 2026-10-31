#include "PluginEditor.h"

DualIRAudioProcessorEditor::DualIRAudioProcessorEditor (DualIRAudioProcessor& audioProcessor)
    : AudioProcessorEditor (audioProcessor), mainPanel (audioProcessor)
{
    setOpaque (true);
    addAndMakeVisible (mainPanel);
    setResizable (true, true);
    setResizeLimits (640, 400, 1600, 1000);
    getConstrainer()->setFixedAspectRatio (1.6);
    setSize (800, 500);
}
void DualIRAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour (0xff13181f));
}
void DualIRAudioProcessorEditor::resized()
{
    mainPanel.setBounds (0, 0, 800, 500);
    mainPanel.setTransform (juce::AffineTransform::scale (static_cast<float> (getWidth()) / 800.0f,
                                                        static_cast<float> (getHeight()) / 500.0f));
}
