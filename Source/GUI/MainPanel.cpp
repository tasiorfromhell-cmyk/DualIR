#include "MainPanel.h"
#include "../PluginProcessor.h"
#include <cmath>

namespace
{
const juce::Colour background { 0xff13181f }, card { 0xff1c242e }, border { 0xff34404c };
const juce::Colour text { 0xffedf2f6 }, subdued { 0xff9aaab9 }, amber { 0xffe4b268 };
const juce::Colour cyan { 0xff75c6cf }, warning { 0xffff8b7f };
void label (juce::Label& c, const juce::String& title, float size, juce::Colour colour = text)
{
    c.setText (title, juce::dontSendNotification);
    c.setFont (juce::Font (juce::FontOptions { size }));
    c.setColour (juce::Label::textColourId, colour);
    c.setBorderSize (juce::BorderSize<int> (0));
}
void rotary (juce::Slider& s, const juce::String& id, juce::Colour accent)
{
    s.setComponentID (id);
    s.setName (id);
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 82, 22);
    s.setColour (juce::Slider::rotarySliderFillColourId, accent);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, border);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setTextValueSuffix (" dB");
}
void decibelText (juce::Slider& slider)
{
    slider.setTextValueSuffix (" dB");
    slider.textFromValueFunction = [] (double value) { return juce::String (value, 1); };
    slider.valueFromTextFunction = [] (const juce::String& value) { return value.getDoubleValue(); };
    slider.updateText();
}
juce::String peakText (float peak)
{
    return peak <= 0.00001f ? "-inf dBFS"
                           : juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS";
}
void meter (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& name, float level)
{
    g.setFont (juce::Font (juce::FontOptions { 11.0f }));
    g.setColour (subdued);
    g.drawText (name, area.removeFromLeft (33), juce::Justification::centredLeft);
    g.setColour (level >= 1.0f ? warning : text);
    g.drawText (peakText (level), area.removeFromRight (83), juce::Justification::centredRight);
    auto bar = area.reduced (5, 9).toFloat();
    g.setColour (border); g.fillRoundedRectangle (bar, 2.0f);
    bar.setWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f,
        (juce::Decibels::gainToDecibels (juce::jmax (level, 0.000001f)) + 60.0f) / 60.0f));
    g.setColour (level >= 1.0f ? warning : cyan); g.fillRoundedRectangle (bar, 2.0f);
}
}

class MainPanel::SlotPanel final : public juce::Component
{
public:
    SlotPanel (DualIRAudioProcessor& p, int number, juce::Colour colour)
        : owner (p), slot (number), accent (colour)
    {
        const juce::String prefix = slot == 0 ? "a" : "b";
        setComponentID (prefix + "Slot");
        label (title, slot == 0 ? "CABINET A" : "CABINET B", 16.0f, accent);
        title.setFont (title.getFont().boldened());
        label (filename, "No IR loaded", 17.0f);
        filename.setMinimumHorizontalScale (0.75f);
        label (metadata, "Drop a WAV / AIFF here", 12.0f, subdued);
        label (message, "", 11.5f, subdued);
        message.setJustificationType (juce::Justification::topLeft);
        label (gainLabel, "LEVEL", 11.0f, subdued);
        gainLabel.setJustificationType (juce::Justification::centred);
        load.setButtonText ("Load IR..."); load.setComponentID (prefix + "Load");
        load.setTooltip ("Choose a mono/stereo WAV or AIFF IR up to 2 seconds. Audio is embedded in the project state.");
        load.onClick = [this] { openChooser(); };
        clear.setButtonText ("Clear"); clear.setComponentID (prefix + "Clear");
        clear.setTooltip ("Remove this IR. A single populated cabinet always receives the full signal.");
        clear.onClick = [this] { owner.clearIR (slot); };
        enabled.setButtonText ("Enabled"); enabled.setComponentID (prefix + "Enabled");
        enabled.setTooltip ("Mute or enable this cabinet without unloading its IR.");
        inverted.setButtonText ("Invert polarity"); inverted.setComponentID (prefix + "Invert");
        inverted.setTooltip ("Invert this cabinet's output polarity.");
        rotary (gain, prefix + "Gain", accent);
        gain.setTooltip ("Cabinet level, -24 to +24 dB.");
        for (auto* c : std::initializer_list<juce::Component*> {
                 &title, &filename, &metadata, &message, &load, &clear, &enabled, &inverted, &gain, &gainLabel })
            addAndMakeVisible (*c);
        gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
            (owner.parameters, prefix + "Gain", gain);
        decibelText (gain);
        enabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
            (owner.parameters, prefix + "Enabled", enabled);
        invertedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
            (owner.parameters, prefix + "Invert", inverted);
        browse.setButtonText ("Browse Folder"); browse.setComponentID (prefix + "BrowseFolder");
        browse.onClick = [this] { openChooser (true); };
        files.setComponentID (prefix + "Files");
        files.setTextWhenNothingSelected ("Select an IR from this folder");
        files.onChange = [this]
        {
            const int index = files.getSelectedId() - 1;
            if (listing && index >= 0 && index < static_cast<int> (listing->size()))
                owner.requestIRLoad (slot, (*listing)[static_cast<size_t> (index)]);
        };
        label (folderLabel, "No folder selected", 11.0f, subdued);
        addAndMakeVisible (browse); addAndMakeVisible (files); addAndMakeVisible (folderLabel);
        refresh();
    }
    ~SlotPanel() override { chooser.reset(); }
    void refresh()
    {
        const auto state = owner.irBrowsers.snapshot();
        const auto index = static_cast<size_t> (slot);
        const auto path = state.folders[index].getFullPathName();
        folderLabel.setText (path.isEmpty() ? "No folder selected" : path, juce::dontSendNotification);
        folderLabel.setTooltip (path);
        if (listing != state.files[index])
        {
            listing = state.files[index];
            files.clear (juce::dontSendNotification);
            if (listing)
                for (size_t i = 0; i < listing->size(); ++i)
                    files.addItem ((*listing)[i].getFileName(), static_cast<int> (i) + 1);
        }
        const auto status = owner.getIRStatus (slot);
        int selected = 0;
        if (listing)
            for (size_t i = 0; i < listing->size(); ++i)
                if ((*listing)[i].getFullPathName() == status.path) selected = static_cast<int> (i) + 1;
        files.setSelectedId (selected, juce::dontSendNotification);
        filename.setText (status.loaded ? status.name : "No IR loaded", juce::dontSendNotification);
        filename.setTooltip (status.name);
        juce::String details = "Drop a WAV / AIFF here";
        if (status.loading) details = "Loading impulse response...";
        else if (status.loaded)
            details = juce::String (status.sampleRate / 1000.0, 1) + " kHz  /  "
                    + (status.numChannels == 1 ? "Mono" : "Stereo") + "  /  "
                    + juce::String (1000.0 * status.numSamples / status.sampleRate, 1) + " ms";
        metadata.setText (details, juce::dontSendNotification);
        message.setColour (juce::Label::textColourId, status.error.isNotEmpty() ? warning : subdued);
        message.setText (status.error.isNotEmpty() ? status.error
                        : status.loaded ? "IR saved with your project."
                                        : "Empty slots do not add dry audio to a loaded cab.", juce::dontSendNotification);
        message.setTooltip (status.error);
        clear.setEnabled (status.loaded || status.loading);
    }
    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (card); g.fillRoundedRectangle (bounds, 12.0f);
        g.setColour (border); g.drawRoundedRectangle (bounds, 12.0f, 1.0f);
        g.setColour (accent); g.fillRoundedRectangle (20.0f, 20.0f, 3.0f, 23.0f, 1.5f);
    }
    void resized() override
    {
        title.setBounds (32, 20, getWidth() - 52, 23);
        filename.setBounds (20, 56, getWidth() - 40, 27);
        metadata.setBounds (20, 89, getWidth() - 40, 18);
        message.setBounds (20, 116, getWidth() - 40, 38);
        load.setBounds (20, 160, 125, 29); clear.setBounds (155, 160, 78, 29);
        enabled.setBounds (18, 205, 170, 24); inverted.setBounds (18, 236, 174, 24);
        gainLabel.setBounds (getWidth() - 132, 197, 112, 17);
        gain.setBounds (getWidth() - 132, 216, 112, 88);
        browse.setBounds (20, 312, 135, 28);
        folderLabel.setBounds (20, 345, getWidth() - 40, 22);
        files.setBounds (20, 374, getWidth() - 40, 30);
    }
private:
    void openChooser (bool folderOnly = false)
    {
        if (chooserOpen) return;
        chooserOpen = true;
        chooser = std::make_unique<juce::FileChooser>
            (slot == 0 ? "Choose cabinet A impulse response" : "Choose cabinet B impulse response",
             owner.irBrowsers.snapshot().folders[static_cast<size_t> (slot)], "*.wav;*.aif;*.aiff", true);
        const juce::Component::SafePointer<SlotPanel> safeThis (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | (folderOnly ? juce::FileBrowserComponent::canSelectDirectories
                                                              : juce::FileBrowserComponent::canSelectFiles),
            [safeThis, folderOnly] (const juce::FileChooser& selected)
            {
                if (safeThis == nullptr) return;
                safeThis->chooserOpen = false;
                const auto file = selected.getResult();
                if (file == juce::File {}) return;
                if (folderOnly) safeThis->owner.irBrowsers.onFolderChanged (safeThis->slot, file);
                else safeThis->owner.requestIRLoad (safeThis->slot, file);
            });
    }
    DualIRAudioProcessor& owner;
    int slot;
    juce::Colour accent;
    juce::Label title, filename, metadata, message, gainLabel;
    juce::TextButton load, clear, browse;
    juce::Label folderLabel;
    juce::ComboBox files;
    std::shared_ptr<const IRBrowserCoordinator::Files> listing;
    juce::ToggleButton enabled, inverted;
    juce::Slider gain;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment, invertedAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    bool chooserOpen = false;
};

MainPanel::MainPanel (DualIRAudioProcessor& p) : owner (p)
{
    setComponentID ("mainPanel"); setOpaque (true);
    lookAndFeel.setColour (juce::Slider::thumbColourId, text);
    lookAndFeel.setColour (juce::Slider::textBoxTextColourId, text);
    lookAndFeel.setColour (juce::Slider::textBoxBackgroundColourId, background);
    lookAndFeel.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303d4b));
    lookAndFeel.setColour (juce::TextButton::textColourOffId, text);
    lookAndFeel.setColour (juce::ToggleButton::textColourId, text);
    lookAndFeel.setColour (juce::ToggleButton::tickColourId, amber);
    setLookAndFeel (&lookAndFeel);
    slotA = std::make_unique<SlotPanel> (owner, 0, amber); slotB = std::make_unique<SlotPanel> (owner, 1, cyan);
    addAndMakeVisible (*slotA); addAndMakeVisible (*slotB);
    blend.setComponentID ("blend");
    blend.setSliderStyle (juce::Slider::LinearHorizontal);
    blend.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 22);
    blend.setColour (juce::Slider::trackColourId, amber);
    blend.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    blend.setTooltip ("Linear A/B mix. A single loaded IR receives the full signal.");
    rotary (output, "output", cyan);
    output.setSliderStyle (juce::Slider::LinearHorizontal);
    output.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 22);
    output.setColour (juce::Slider::trackColourId, cyan);
    output.setTooltip ("Output trim, -24 to +12 dB. Bypass ignores output trim.");
    bypass.setComponentID ("bypass");
    bypass.setTooltip ("Pass the original input around both cabinets and output trim.");
    label (blendLabel, "A / B BLEND", 11.0f, subdued);
    label (outputLabel, "OUTPUT", 11.0f, subdued); outputLabel.setJustificationType (juce::Justification::centred);
    label (routingLabel, "No IR loaded - clean pass-through", 11.0f, subdued);
    for (auto* c : std::initializer_list<juce::Component*> {
             &blend, &output, &bypass, &blendLabel, &outputLabel, &routingLabel }) addAndMakeVisible (*c);
    blendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (owner.parameters, "blend", blend);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (owner.parameters, "output", output);
    decibelText (output);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (owner.parameters, "bypass", bypass);
    blend.textFromValueFunction = [] (double value) { return juce::String (juce::roundToInt (value * 100.0)) + "% B"; };
    blend.valueFromTextFunction = [] (const juce::String& value) { return value.getDoubleValue() / 100.0; };
    blend.updateText();
    label (settingsLabel, "SETTINGS", 11.0f, subdued);
    addAndMakeVisible (settingsLabel); addAndMakeVisible (linkFolders);
    linkFolders.setComponentID ("linkFolders");
    linkFolders.setToggleState (owner.irBrowsers.snapshot().linkFolders, juce::dontSendNotification);
    linkFolders.onClick = [this]
    {
        owner.irBrowsers.setLinked (linkFolders.getToggleState());
        changeListenerCallback (nullptr);
    };
    owner.irBrowsers.addChangeListener (this);
    setSize (800, 640); startTimerHz (15);
}
MainPanel::~MainPanel() { owner.irBrowsers.removeChangeListener (this); stopTimer(); setLookAndFeel (nullptr); }
void MainPanel::paint (juce::Graphics& g)
{
    g.fillAll (background); g.setColour (amber);
    g.setFont (juce::Font (juce::FontOptions { 31.0f }).boldened());
    g.drawText ("RottingIR", 26, 16, 170, 39, juce::Justification::centredLeft);
    g.setColour (subdued); g.setFont (juce::Font (juce::FontOptions { 12.0f }));
    g.drawText ("DUAL CABINET CONVOLUTION", 198, 25, 300, 20, juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions { 10.0f }));
    g.drawText ("RottingPitStudio", 198, 45, 300, 16, juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions { 12.0f }));
    g.drawText ("A", 30, 551, 20, 25, juce::Justification::centredLeft);
    g.drawText ("B", 436, 551, 20, 25, juce::Justification::centredLeft);
    g.setColour (border); g.drawHorizontalLine (589, 26.0f, 774.0f);
    meter (g, { 26, 596, 254, 27 }, "IN", inputLevel); meter (g, { 299, 596, 254, 27 }, "OUT", outputLevel);
    g.setColour (subdued);
    g.drawText ("ZERO LATENCY  /  MONO + STEREO", 560, 596, 214, 27, juce::Justification::centredRight);
}
void MainPanel::resized()
{
    slotA->setBounds (26, 70, 365, 420); slotB->setBounds (409, 70, 365, 420);
    settingsLabel.setBounds (26, 497, 90, 24); linkFolders.setBounds (116, 497, 265, 24);
    bypass.setBounds (665, 24, 109, 25);
    blendLabel.setBounds (26, 531, 150, 18); blend.setBounds (49, 551, 382, 25);
    routingLabel.setBounds (460, 543, 165, 32);
    outputLabel.setBounds (630, 531, 144, 18); output.setBounds (630, 551, 144, 25);
}
bool MainPanel::isAudioFile (const juce::String& path) { return juce::File (path).hasFileExtension ("wav;aif;aiff"); }
bool MainPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& file : files) if (isAudioFile (file)) return true;
    return false;
}
void MainPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused (y); juce::StringArray audioFiles;
    for (const auto& file : files) if (isAudioFile (file)) audioFiles.add (file);
    if (audioFiles.size() >= 2)
    {
        owner.requestIRLoad (0, juce::File (audioFiles[0])); owner.requestIRLoad (1, juce::File (audioFiles[1]));
    }
    else if (audioFiles.size() == 1) owner.requestIRLoad (x < getWidth() / 2 ? 0 : 1, juce::File (audioFiles[0]));
}
void MainPanel::timerCallback()
{
    slotA->refresh(); slotB->refresh();
    const auto a = owner.getIRStatus (0), b = owner.getIRStatus (1);
    const auto status = a.loading || b.loading ? "Preparing IR..."
                      : ! a.loaded && ! b.loaded ? "No IR: clean pass-through"
                      : a.loaded && b.loaded ? "Parallel A / B convolution"
                                           : a.loaded ? "Cabinet A active" : "Cabinet B active";
    routingLabel.setText (status, juce::dontSendNotification);
    const auto input = owner.getInputPeak(), peak = owner.getOutputPeak();
    inputLevel = juce::jmax (inputLevel * 0.75f, std::isfinite (input) ? input : 0.0f);
    outputLevel = juce::jmax (outputLevel * 0.75f, std::isfinite (peak) ? peak : 0.0f);
    repaint (0, 590, getWidth(), 50);
}

void MainPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    linkFolders.setToggleState (owner.irBrowsers.snapshot().linkFolders, juce::dontSendNotification);
    slotA->refresh(); slotB->refresh();
}
