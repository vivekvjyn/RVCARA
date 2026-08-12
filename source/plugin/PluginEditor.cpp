#include "PluginEditor.h"

#include <engine/VoiceModelLibrary.h>

namespace rvcara::plugin
{

namespace
{
    constexpr int defaultWidth = 640;
    constexpr int defaultHeight = 420;
    constexpr int minimumWidth = 520;
    constexpr int minimumHeight = 360;

    /** Interface refresh rate. Fast enough for a progress bar to look live, slow enough
        that polling several modifications costs nothing.
    */
    constexpr int refreshHz = 15;

    const juce::Colour panelColour { 0xff1a1d22 };
    const juce::Colour textColour { 0xffd7dbe0 };
    const juce::Colour dimTextColour { 0xff7d8590 };
    const juce::Colour accentColour { 0xff5fc9a0 };

    /** Describes what a modification is doing, for the status line. */
    juce::String describeState (const ara::ConversionModification& modification)
    {
        using State = ara::ConversionModification::State;

        switch (modification.getState())
        {
            case State::idle:      return modification.getSettings().isBypassed ? "Bypassed" : "Ready";
            case State::queued:    return "Queued";
            case State::rendering: return "Converting "
                                        + juce::String (juce::roundToInt (modification.getProgress() * 100.0f))
                                        + "%";
            case State::ready:     return modification.isConversionCurrent() ? "Converted" : "Converted (out of date)";
            case State::failed:    return "Failed: " + modification.getError();
        }

        return {};
    }
} // namespace

PluginEditor::PluginEditor (PluginProcessor& processor)
    : juce::AudioProcessorEditor (&processor),
      juce::AudioProcessorEditorARAExtension (&processor),
      processorReference (processor)
{
    titleLabel.setText ("RVCARA", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions { 20.0f, juce::Font::bold });
    titleLabel.setColour (juce::Label::textColourId, textColour);
    addAndMakeVisible (titleLabel);

    statusLabel.setFont (juce::FontOptions { 12.0f });
    statusLabel.setColour (juce::Label::textColourId, dimTextColour);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    voiceLabel.setText ("Voice", juce::dontSendNotification);
    voiceLabel.setFont (juce::FontOptions { 12.0f });
    voiceLabel.setColour (juce::Label::textColourId, dimTextColour);
    addAndMakeVisible (voiceLabel);

    voiceSelector.setTextWhenNoChoicesAvailable ("No voices installed");
    voiceSelector.setTextWhenNothingSelected ("Choose a voice");
    voiceSelector.onChange = [this] { applySelectedVoice(); };
    addAndMakeVisible (voiceSelector);

    rescanButton.onClick = [this]
    {
        if (auto* documentController = processorReference.getConversionDocumentController())
            documentController->getLibrary().rescan();

        refreshVoiceList();
    };
    addAndMakeVisible (rescanButton);

    addAndMakeVisible (pitchCurveView);

    addSlider (pitchControl, PluginProcessor::ParameterId::pitchShiftSemitones, "Pitch");
    addSlider (timbreControl, PluginProcessor::ParameterId::retrievalRatio, "Timbre");
    addSlider (consonantControl, PluginProcessor::ParameterId::consonantProtection, "Consonants");
    addSlider (dynamicsControl, PluginProcessor::ParameterId::envelopeFollowRatio, "Dynamics");
    addSlider (variationControl, PluginProcessor::ParameterId::latentNoiseSeed, "Variation");

    bypassButton.setColour (juce::ToggleButton::textColourId, dimTextColour);
    bypassButton.setColour (juce::ToggleButton::tickColourId, accentColour);
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorReference.getParameters(), PluginProcessor::ParameterId::isBypassed, bypassButton);

    if (auto* documentController = processorReference.getConversionDocumentController())
        documentController->addListener (this);

    refreshVoiceList();
    refreshFromModel();

    setResizeLimits (minimumWidth, minimumHeight, 1600, 1200);
    setSize (defaultWidth, defaultHeight);

    startTimerHz (refreshHz);
}

PluginEditor::~PluginEditor()
{
    stopTimer();

    if (auto* documentController = processorReference.getConversionDocumentController())
        documentController->removeListener (this);
}

void PluginEditor::addSlider (LabelledSlider& control, const char* parameterId, const juce::String& name)
{
    control.label.setText (name, juce::dontSendNotification);
    control.label.setFont (juce::FontOptions { 11.0f });
    control.label.setColour (juce::Label::textColourId, dimTextColour);
    control.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (control.label);

    control.slider.setColour (juce::Slider::rotarySliderFillColourId, accentColour);
    control.slider.setColour (juce::Slider::textBoxTextColourId, textColour);
    control.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    control.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    addAndMakeVisible (control.slider);

    control.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorReference.getParameters(), parameterId, control.slider);
}

void PluginEditor::refreshVoiceList()
{
    auto* documentController = processorReference.getConversionDocumentController();

    if (documentController == nullptr)
        return;

    const juce::ScopedValueSetter<bool> suppress { isRefreshing, true };

    auto& library = documentController->getLibrary();

    if (library.getEntries().empty())
        library.rescan();

    const auto previousSelection = voiceSelector.getText();

    voiceSelector.clear (juce::dontSendNotification);
    voiceNames.clear();

    auto itemId = 1;

    for (const auto& entry : library.getEntries())
    {
        voiceNames.push_back (entry.name);

        auto description = entry.name;

        if (entry.modelSampleRate > 0)
            description += "  " + juce::String (entry.modelSampleRate / 1000) + " kHz";

        if (! entry.hasRetrieval)
            description += "  (no timbre index)";

        voiceSelector.addItem (description, itemId++);
    }

    // Restore the selection by name, since the item ids will have shifted.
    const auto focused = getFocusedModification();
    const auto wanted = focused != nullptr && focused->getVoiceName().isNotEmpty()
                      ? focused->getVoiceName()
                      : previousSelection;

    for (std::size_t index = 0; index < voiceNames.size(); ++index)
    {
        if (voiceNames[index] == wanted)
        {
            voiceSelector.setSelectedId (static_cast<int> (index) + 1, juce::dontSendNotification);
            break;
        }
    }
}

void PluginEditor::applySelectedVoice()
{
    if (isRefreshing)
        return;

    auto* documentController = processorReference.getConversionDocumentController();
    const auto selectedIndex = voiceSelector.getSelectedId() - 1;

    if (documentController == nullptr
        || selectedIndex < 0
        || selectedIndex >= static_cast<int> (voiceNames.size()))
        return;

    const auto& name = voiceNames[static_cast<std::size_t> (selectedIndex)];

    for (auto* modification : processorReference.getEditableModifications())
        documentController->applyVoice (*modification, name);
}

ara::ConversionModification* PluginEditor::getFocusedModification() const
{
    const auto modifications = processorReference.getEditableModifications();
    return modifications.empty() ? nullptr : modifications.front();
}

void PluginEditor::refreshFromModel()
{
    auto* documentController = processorReference.getConversionDocumentController();

    if (documentController == nullptr)
    {
        // Loaded without ARA. Say so, rather than presenting controls that do nothing.
        statusLabel.setText ("No ARA", juce::dontSendNotification);
        pitchCurveView.setRenderState (ara::ConversionModification::State::idle, 0.0f,
                                       "This host loaded RVCARA without ARA, so audio is passing "
                                       "through unchanged.\nLoad it as an ARA plug-in to convert.");
        return;
    }

    if (documentController->isLoadingVoice())
    {
        statusLabel.setText ("Loading " + documentController->getRequestedVoiceName(), juce::dontSendNotification);
        pitchCurveView.setRenderState (ara::ConversionModification::State::queued, 0.0f,
                                       "Loading voice...");
        return;
    }

    if (const auto loadError = documentController->getLoadError(); loadError.isNotEmpty())
    {
        statusLabel.setText ("Load failed", juce::dontSendNotification);
        pitchCurveView.setRenderState (ara::ConversionModification::State::failed, 0.0f, loadError);
        return;
    }

    auto* modification = getFocusedModification();

    if (modification == nullptr)
    {
        statusLabel.setText ("No region", juce::dontSendNotification);
        pitchCurveView.setConversion (nullptr);
        pitchCurveView.setRenderState (ara::ConversionModification::State::idle, 0.0f,
                                       "Add RVCARA to a track with audio on it.");
        return;
    }

    if (documentController->getVoiceModel() == nullptr)
    {
        statusLabel.setText ("No voice", juce::dontSendNotification);
        pitchCurveView.setRenderState (ara::ConversionModification::State::idle, 0.0f,
                                       "Choose a voice to convert this region.\n"
                                       "Export one with tools/rvcara_export.");
        return;
    }

    statusLabel.setText (describeState (*modification), juce::dontSendNotification);

    pitchCurveView.setConversion (modification->getConversion());
    pitchCurveView.setRenderState (modification->getState(),
                                   modification->getProgress(),
                                   modification->getState() == ara::ConversionModification::State::failed
                                       ? modification->getError()
                                       : juce::String());
}

void PluginEditor::conversionStateChanged()
{
    refreshVoiceList();
    refreshFromModel();
}

void PluginEditor::timerCallback()
{
    refreshFromModel();
}

void PluginEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (panelColour);
}

void PluginEditor::resized()
{
    auto bounds = getLocalBounds().reduced (12);

    auto headerBounds = bounds.removeFromTop (28);
    titleLabel.setBounds (headerBounds.removeFromLeft (110));
    statusLabel.setBounds (headerBounds);

    bounds.removeFromTop (8);

    auto voiceBounds = bounds.removeFromTop (26);
    voiceLabel.setBounds (voiceBounds.removeFromLeft (40));
    rescanButton.setBounds (voiceBounds.removeFromRight (72).reduced (0, 1));
    voiceBounds.removeFromRight (6);
    voiceSelector.setBounds (voiceBounds);

    bounds.removeFromTop (10);

    auto controlBounds = bounds.removeFromBottom (110);
    bypassButton.setBounds (controlBounds.removeFromBottom (22).removeFromLeft (100));

    // Five controls sharing the width equally, each with its label above.
    const auto controlWidth = controlBounds.getWidth() / 5;

    for (auto* control : { &pitchControl, &timbreControl, &consonantControl,
                           &dynamicsControl, &variationControl })
    {
        auto slot = controlBounds.removeFromLeft (controlWidth);
        control->label.setBounds (slot.removeFromTop (14));
        control->slider.setBounds (slot.reduced (4, 0));
    }

    bounds.removeFromBottom (8);
    pitchCurveView.setBounds (bounds);
}

} // namespace rvcara::plugin
