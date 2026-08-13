#include "PluginEditor.h"

#include "VoiceModelLibrary.h"

namespace rvcara
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

    /** What to say when the machine has no voice on it at all. Names the directory rather
        than describing it, because that is what the user has to go and put a file in.
    */
    juce::String describeMissingVoice()
    {
        return "No voice installed.\nPut an exported voice in\n"
             + VoiceModelLibrary::getUserModelDirectory().getFullPathName();
    }

    /** Describes what a modification is doing, for the status line. */
    juce::String describeState (const ConversionModification& modification)
    {
        using State = ConversionModification::State;

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

PluginEditor::PluginEditor (PluginProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      juce::AudioProcessorEditorARAExtension (&processorToUse),
      processorReference (processorToUse)
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
        processorReference.getVoiceLoader().getLibrary().rescan();
        refreshVoiceList();
    };
    addAndMakeVisible (rescanButton);

    convertButton.onClick = [this] { processorReference.getInsertConverter().requestConversion(); };
    addChildComponent (convertButton);

    clearCaptureButton.onClick = [this] { processorReference.getInsertConverter().reset(); };
    addChildComponent (clearCaptureButton);

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
    const juce::ScopedValueSetter<bool> suppress { isRefreshing, true };

    auto& library = processorReference.getVoiceLoader().getLibrary();

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
    const auto* focused = getFocusedModification();
    const auto requested = processorReference.getVoiceLoader().getRequestedName();

    const auto wanted = focused != nullptr && focused->getVoiceName().isNotEmpty()
                      ? focused->getVoiceName()
                      : (requested.isNotEmpty() ? requested : previousSelection);

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

    const auto selectedIndex = voiceSelector.getSelectedId() - 1;

    if (selectedIndex < 0 || selectedIndex >= static_cast<int> (voiceNames.size()))
        return;

    const auto& name = voiceNames[static_cast<std::size_t> (selectedIndex)];

    if (auto* documentController = processorReference.getConversionDocumentController())
    {
        for (auto* modification : processorReference.getEditableModifications())
            documentController->applyVoice (*modification, name);

        return;
    }

    // Insert mode: load the voice, then convert whatever has already been captured so the
    // change is audible without another pass of the transport.
    processorReference.getVoiceLoader().request (name, [this]
    {
        auto& converter = processorReference.getInsertConverter();

        if (converter.getCapturedSeconds() > 0.0)
            converter.requestConversion();
    });
}

ConversionModification* PluginEditor::getFocusedModification() const
{
    const auto modifications = processorReference.getEditableModifications();
    return modifications.empty() ? nullptr : modifications.front();
}

void PluginEditor::refreshFromModel()
{
    auto* documentController = processorReference.getConversionDocumentController();

    if (documentController == nullptr)
    {
        refreshFromInsertConverter();
        return;
    }

    convertButton.setVisible (false);
    clearCaptureButton.setVisible (false);

    if (documentController->isLoadingVoice())
    {
        statusLabel.setText ("Loading " + documentController->getRequestedVoiceName(), juce::dontSendNotification);
        pitchCurveView.setRenderState (ConversionModification::State::queued, 0.0f,
                                       "Loading voice...");
        return;
    }

    if (const auto loadError = documentController->getLoadError(); loadError.isNotEmpty())
    {
        statusLabel.setText ("Load failed", juce::dontSendNotification);
        pitchCurveView.setRenderState (ConversionModification::State::failed, 0.0f, loadError);
        return;
    }

    auto* modification = getFocusedModification();

    if (modification == nullptr)
    {
        statusLabel.setText ("No region", juce::dontSendNotification);
        pitchCurveView.setConversion (nullptr);
        pitchCurveView.setRenderState (ConversionModification::State::idle, 0.0f,
                                       "Add RVCARA to a track with audio on it.");
        return;
    }

    if (documentController->getVoiceModel() == nullptr)
    {
        statusLabel.setText ("No voice", juce::dontSendNotification);
        pitchCurveView.setRenderState (ConversionModification::State::idle, 0.0f, describeMissingVoice());
        return;
    }

    statusLabel.setText (describeState (*modification), juce::dontSendNotification);

    pitchCurveView.setConversion (modification->getConversion());
    pitchCurveView.setRenderState (modification->getState(),
                                   modification->getProgress(),
                                   modification->getState() == ConversionModification::State::failed
                                       ? modification->getError()
                                       : juce::String());
}

void PluginEditor::refreshFromInsertConverter()
{
    using State = InsertConverter::State;

    auto& loader = processorReference.getVoiceLoader();
    auto& converter = processorReference.getInsertConverter();

    const auto capturedSeconds = converter.getCapturedSeconds();

    convertButton.setVisible (true);
    clearCaptureButton.setVisible (true);
    convertButton.setEnabled (capturedSeconds > 0.0 && loader.getVoice() != nullptr);
    clearCaptureButton.setEnabled (capturedSeconds > 0.0);

    pitchCurveView.setConversion (converter.getConversion());

    if (loader.isLoading())
    {
        statusLabel.setText ("Loading " + loader.getRequestedName(), juce::dontSendNotification);
        pitchCurveView.setRenderState (ConversionModification::State::queued, 0.0f, "Loading voice...");
        return;
    }

    if (const auto loadError = loader.getError(); loadError.isNotEmpty())
    {
        statusLabel.setText ("Load failed", juce::dontSendNotification);
        pitchCurveView.setRenderState (ConversionModification::State::failed, 0.0f, loadError);
        return;
    }

    if (loader.getVoice() == nullptr)
    {
        statusLabel.setText ("No voice", juce::dontSendNotification);
        pitchCurveView.setRenderState (ConversionModification::State::idle, 0.0f, describeMissingVoice());
        return;
    }

    // The state names map onto the pitch view's, which was written for the ARA path; the two
    // vocabularies are the same idea and sharing the view keeps them looking identical.
    const auto mapped = [] (State state)
    {
        switch (state)
        {
            case State::idle:      return ConversionModification::State::idle;
            case State::capturing: return ConversionModification::State::idle;
            case State::queued:    return ConversionModification::State::queued;
            case State::rendering: return ConversionModification::State::rendering;
            case State::ready:     return ConversionModification::State::ready;
            case State::failed:    return ConversionModification::State::failed;
        }

        return ConversionModification::State::idle;
    };

    const auto state = converter.getState();
    juce::String message;
    juce::String status;

    switch (state)
    {
        case State::idle:
            status = "Waiting";
            message = "Play the track. RVCARA captures the vocal as it goes, converts it, "
                      "and plays the converted voice back in place on the next pass.";
            break;

        case State::capturing:
            status = "Captured " + juce::String (capturedSeconds, 1) + " s";
            message = converter.hasReachedCaptureLimit()
                        ? "Capture buffer is full; only the first "
                              + juce::String (juce::roundToInt (InsertConverter::maximumCaptureSeconds / 60.0))
                              + " minutes will convert."
                        : juce::String();
            break;

        case State::queued:
            status = "Queued";
            break;

        case State::rendering:
            status = "Converting " + juce::String (juce::roundToInt (converter.getProgress() * 100.0f)) + "%";
            break;

        case State::ready:
            status = converter.isConversionStale() ? "Converted (out of date)" : "Converted";
            message = converter.isConversionStale()
                        ? juce::String ("Settings changed since this render. Press Convert to update.")
                        : juce::String();
            break;

        case State::failed:
            status = "Failed";
            message = converter.getError();
            break;
    }

    statusLabel.setText (status, juce::dontSendNotification);
    pitchCurveView.setRenderState (mapped (state), converter.getProgress(), message);
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

    auto buttonRow = controlBounds.removeFromBottom (22);
    bypassButton.setBounds (buttonRow.removeFromLeft (90));
    convertButton.setBounds (buttonRow.removeFromLeft (84).reduced (2, 0));
    clearCaptureButton.setBounds (buttonRow.removeFromLeft (68).reduced (2, 0));

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

} // namespace rvcara
