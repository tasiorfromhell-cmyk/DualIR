#include "MainPanel.h"
#include <DualIRGuiAssets.h>
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

class MainPanel::SlotPanel final : public juce::Component, private juce::ListBoxModel
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
        message.setComponentID (prefix + "BrowserMessage");
        message.setJustificationType (juce::Justification::topLeft);
        label (gainLabel, "LEVEL", 11.0f, subdued);
        gainLabel.setJustificationType (juce::Justification::centred);
        folderPath.setComponentID (prefix + "Folder");
        label (folderPath, "Choose an IR folder", 11.0f, subdued);
        list.setComponentID (prefix + "IRList");
        list.setModel (this);
        list.setRowHeight (22);
        list.setColour (juce::ListBox::backgroundColourId, background.withAlpha (0.94f));
        list.setColour (juce::ListBox::outlineColourId, border);
        list.setOutlineThickness (1);
        list.setMultipleSelectionEnabled (false);
        list.setClickingTogglesRowSelection (false);
        previous.setButtonText ("<"); previous.setComponentID (prefix + "Previous");
        next.setButtonText (">"); next.setComponentID (prefix + "Next");
        previous.onClick = [this] { owner.stepBrowserIR (slot, -1); refresh(); };
        next.onClick = [this] { owner.stepBrowserIR (slot, 1); refresh(); };
        load.setButtonText ("BROWSE"); load.setComponentID (prefix + "Load");
        load.setTooltip ("Choose a folder containing WAV / AIF / AIFF impulse responses.");
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
                 &title, &folderPath, &list, &previous, &next, &filename, &metadata, &message, &load, &clear, &enabled, &inverted, &gain, &gainLabel })
            addAndMakeVisible (*c);
        gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
            (owner.parameters, prefix + "Gain", gain);
        decibelText (gain);
        enabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
            (owner.parameters, prefix + "Enabled", enabled);
        invertedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
            (owner.parameters, prefix + "Invert", inverted);
        refresh();
    }
    ~SlotPanel() override { chooser.reset(); list.setModel (nullptr); }
    int getNumRows() override { return files != nullptr ? static_cast<int> (files->size()) : 0; }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows()) return;
        if (selected) { g.setColour (accent.withAlpha (0.24f)); g.fillAll(); }
        g.setColour (selected ? accent : text);
        g.setFont (juce::Font (juce::FontOptions { 12.0f }));
        g.drawText ((*files)[static_cast<size_t> (row)].getFileName(), 7, 0, width - 14, height,
                    juce::Justification::centredLeft, true);
    }
    void selectedRowsChanged (int row) override
    {
        if (! updatingSelection && row >= 0) owner.selectBrowserIR (slot, row);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        // A second click on a highlighted, previously failed file retries loading it.
        if (row >= 0 && owner.getIRStatus (slot).error.isNotEmpty()) owner.selectBrowserIR (slot, row);
    }
    void refresh()
    {
        const auto status = owner.getIRStatus (slot);
        const auto browser = owner.getBrowserState (slot);
        if (files != browser.files)
        {
            files = browser.files;
            updatingSelection = true;
            list.updateContent();
            updatingSelection = false;
        }
        folderPath.setText (browser.folder.isEmpty() ? "Choose an IR folder" : browser.folder,
                            juce::dontSendNotification);
        folderPath.setTooltip (browser.folder);
        int selected = -1;
        for (int row = 0; row < getNumRows(); ++row)
            if ((*files)[static_cast<size_t> (row)].getFullPathName() == browser.activeIR) selected = row;
        updatingSelection = true;
        if (selected != list.getSelectedRow())
        {
            if (selected >= 0) list.selectRow (selected);
            else list.deselectAllRows();
        }
        updatingSelection = false;
        previous.setEnabled (getNumRows() > 0); next.setEnabled (getNumRows() > 0);
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
        if (status.error.isEmpty())
        {
            const auto outside = status.loaded && (status.path.isEmpty()
                || juce::File (status.path).getParentDirectory().getFullPathName() != browser.folder
                || (! status.loading && selected < 0));
            message.setText (browser.scanning ? "Scanning IR folder..."
                : browser.error.isNotEmpty() ? browser.error
                : outside ? "Active IR is outside this folder."
                : getNumRows() == 0 ? "No WAV / AIF / AIFF files in this folder."
                : "IR saved with your project.", juce::dontSendNotification);
        }
        message.setTooltip (status.error.isNotEmpty() ? status.error : browser.error);
        clear.setEnabled (status.loaded || status.loading);
    }
    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (card.withAlpha (0.72f)); g.fillRoundedRectangle (bounds, 12.0f);
        g.setColour (border); g.drawRoundedRectangle (bounds, 12.0f, 1.0f);
        g.setColour (accent); g.fillRoundedRectangle (20.0f, 20.0f, 3.0f, 23.0f, 1.5f);
    }
    void resized() override
    {
        title.setBounds (32, 16, getWidth() - 52, 23);
        folderPath.setBounds (20, 43, getWidth() - 40, 20);
        list.setBounds (20, 68, getWidth() - 40, 96);
        load.setBounds (20, 170, 110, 25);
        previous.setBounds (138, 170, 32, 25); next.setBounds (178, 170, 32, 25);
        clear.setBounds (220, 170, getWidth() - 240, 25);
        filename.setBounds (20, 201, getWidth() - 40, 20);
        metadata.setBounds (20, 225, 222, 16);
        message.setBounds (20, 245, 220, 26);
        enabled.setBounds (18, 277, 108, 24); inverted.setBounds (123, 277, 126, 24);
        gainLabel.setBounds (getWidth() - 114, 221, 94, 17);
        gain.setBounds (getWidth() - 114, 238, 94, 67);
    }
private:
    void openChooser()
    {
        if (chooserOpen) return;
        chooserOpen = true;
        chooser = std::make_unique<juce::FileChooser>
            (slot == 0 ? "Choose cabinet A IR folder" : "Choose cabinet B IR folder",
             juce::File (owner.getBrowserState (slot).folder), "*", true);
        const juce::Component::SafePointer<SlotPanel> safeThis (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safeThis] (const juce::FileChooser& selected)
            {
                if (safeThis == nullptr) return;
                safeThis->chooserOpen = false;
                const auto file = selected.getResult();
                if (file != juce::File {}) safeThis->owner.setBrowserFolder (safeThis->slot, file);
            });
    }
    DualIRAudioProcessor& owner;
    int slot;
    juce::Colour accent;
    juce::Label title, filename, metadata, message, gainLabel, folderPath;
    juce::ListBox list;
    std::shared_ptr<const std::vector<juce::File>> files;
    bool updatingSelection = false;
    juce::TextButton previous, next;
    juce::TextButton load, clear;
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
    // Decode once, retain the original pixels, and omit the heavy outer frame.
    const auto source = juce::ImageCache::getFromMemory
        (DualIRGuiAssets::logo_jpg, DualIRGuiAssets::logo_jpgSize);
    if (source.isValid())
    {
        const auto crop = source.getClippedImage ({ source.getWidth() * 17 / 100,
            source.getHeight() * 15 / 100, source.getWidth() * 66 / 100,
            source.getHeight() * 72 / 100 });
        backgroundArt = juce::Image (juce::Image::ARGB, crop.getWidth(), crop.getHeight(), true);
        juce::Graphics artGraphics (backgroundArt);
        artGraphics.drawImageAt (crop, 0, 0);
        // Fade all four edges so the rectangular photograph blends into the panel.
        juce::Image::BitmapData pixels (backgroundArt, juce::Image::BitmapData::readWrite);
        const auto fade = static_cast<float> (juce::jmin (pixels.width, pixels.height)) * 0.14f;
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
            {
                const auto distance = juce::jmin (x, y, pixels.width - 1 - x, pixels.height - 1 - y);
                const auto alpha = juce::jlimit (0.0f, 1.0f, static_cast<float> (distance) / fade);
                pixels.setPixelColour (x, y, pixels.getPixelColour (x, y).withAlpha (alpha));
            }
    }
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
    output.setTooltip ("Output trim, -24 to +12 dB, with fixed -12 dB output calibration. Bypass ignores output trim.");
    linkFolders.setComponentID ("linkFolders");
    linkFolders.setToggleState (owner.getBrowserState (0).linked, juce::dontSendNotification);
    linkFolders.onClick = [this] { owner.setLinkFolders (linkFolders.getToggleState()); };
    addAndMakeVisible (linkFolders);
    bypass.setComponentID ("bypass");
    bypass.setTooltip ("Pass the original input around both cabinets and output trim.");
    label (blendLabel, "A / B BLEND", 11.0f, subdued);
    label (outputLabel, "OUTPUT", 11.0f, subdued); outputLabel.setJustificationType (juce::Justification::centred);
    label (routingLabel, "No IR loaded - calibrated output", 11.0f, subdued);
    for (auto* c : std::initializer_list<juce::Component*> {
             &blend, &output, &bypass, &blendLabel, &outputLabel, &routingLabel }) addAndMakeVisible (*c);
    blendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (owner.parameters, "blend", blend);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (owner.parameters, "output", output);
    decibelText (output);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (owner.parameters, "bypass", bypass);
    blend.textFromValueFunction = [] (double value) { return juce::String (juce::roundToInt (value * 100.0)) + "% B"; };
    blend.valueFromTextFunction = [] (const juce::String& value) { return value.getDoubleValue() / 100.0; };
    blend.updateText();
    setSize (800, 500); startTimerHz (15);
}
MainPanel::~MainPanel() { stopTimer(); setLookAndFeel (nullptr); }
void MainPanel::paint (juce::Graphics& g)
{
    g.fillAll (background);
    if (backgroundArt.isValid())
    {
        juce::Graphics::ScopedSaveState state (g);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (0.20f);
        // MainPanel's transform scales artwork and controls together at every size.
        g.drawImageWithin (backgroundArt, 100, 14, 600, 438,
                           juce::RectanglePlacement::centred);
    }
    g.setColour (amber);
    g.setFont (juce::Font (juce::FontOptions { 31.0f }).boldened());
    g.drawText ("RottingIR", 26, 16, 170, 39, juce::Justification::centredLeft);
    g.setColour (subdued); g.setFont (juce::Font (juce::FontOptions { 12.0f }));
    g.drawText ("DUAL CABINET CONVOLUTION", 198, 25, 300, 20, juce::Justification::centredLeft);
    g.drawText ("A", 30, 411, 20, 25, juce::Justification::centredLeft);
    g.drawText ("B", 436, 411, 20, 25, juce::Justification::centredLeft);
    g.setColour (border); g.drawHorizontalLine (449, 26.0f, 774.0f);
    meter (g, { 26, 456, 254, 27 }, "IN", inputLevel); meter (g, { 299, 456, 254, 27 }, "OUT", outputLevel);
    g.setColour (subdued);
    g.drawText ("ZERO LATENCY  /  MONO + STEREO", 560, 456, 214, 27, juce::Justification::centredRight);
}
void MainPanel::resized()
{
    slotA->setBounds (26, 70, 365, 310); slotB->setBounds (409, 70, 365, 310);
    bypass.setBounds (665, 24, 109, 25);
    linkFolders.setBounds (409, 46, 280, 21);
    blendLabel.setBounds (26, 391, 150, 18); blend.setBounds (49, 411, 382, 25);
    routingLabel.setBounds (460, 403, 165, 32);
    outputLabel.setBounds (630, 391, 144, 18); output.setBounds (630, 411, 144, 25);
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
        owner.setBrowserFolder (0, juce::File (audioFiles[0]).getParentDirectory());
        owner.requestIRLoad (0, juce::File (audioFiles[0]));
        owner.setBrowserFolder (1, juce::File (audioFiles[1]).getParentDirectory());
        owner.requestIRLoad (1, juce::File (audioFiles[1]));
    }
    else if (audioFiles.size() == 1)
    {
        const auto slot = x < getWidth() / 2 ? 0 : 1;
        const juce::File file (audioFiles[0]);
        owner.setBrowserFolder (slot, file.getParentDirectory());
        owner.requestIRLoad (slot, file);
    }
}
void MainPanel::timerCallback()
{
    slotA->refresh(); slotB->refresh();
    linkFolders.setToggleState (owner.getBrowserState (0).linked, juce::dontSendNotification);
    const auto a = owner.getIRStatus (0), b = owner.getIRStatus (1);
    const auto status = a.loading || b.loading ? "Preparing IR..."
                      : ! a.loaded && ! b.loaded ? "No IR: calibrated output"
                      : a.loaded && b.loaded ? "Parallel A / B convolution"
                                           : a.loaded ? "Cabinet A active" : "Cabinet B active";
    routingLabel.setText (status, juce::dontSendNotification);
    const auto input = owner.getInputPeak(), peak = owner.getOutputPeak();
    inputLevel = juce::jmax (inputLevel * 0.75f, std::isfinite (input) ? input : 0.0f);
    outputLevel = juce::jmax (outputLevel * 0.75f, std::isfinite (peak) ? peak : 0.0f);
    repaint (0, 450, getWidth(), 50);
}
