#include "Editor.h"

#include "model/VoiceModelLibrary.h"

namespace rvcara
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using TypeScale = PanelLookAndFeel::TypeScale;
    using Metrics = PanelLookAndFeel::Metrics;

    constexpr int defaultWidth = 620;
    constexpr int defaultHeight = 340;
    constexpr int minimumWidth = 460;
    constexpr int minimumHeight = 252;
    constexpr int maximumWidth = 1240;
    constexpr int maximumHeight = 680;

    constexpr int refreshHz = 15;

    constexpr int buttonWidth = 74;
    constexpr int bypassWidth = 68;
    constexpr int voiceButtonWidth = 190;

    constexpr int rescanItemId = 10000;

    const char* qualifierGap = "   ";

    juce::String describeEntry (const VoiceModelLibrary::Entry& entry)
    {
        auto description = entry.name;

        if (entry.modelSampleRate > 0)
            description += qualifierGap + juce::String (entry.modelSampleRate / 1000) + " kHz";

        if (! entry.hasRetrieval)
            description += qualifierGap + juce::String ("no timbre index");

        return description;
    }
}

Editor::Editor (Processor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      juce::AudioProcessorEditorARAExtension (&processorToUse),
      processorReference (processorToUse)
{
    setLookAndFeel (&lookAndFeel);

    voiceButton.onClick = [this] { showVoiceMenu(); };
    voiceButton.setTriggeredOnMouseDown (true);
    addAndMakeVisible (voiceButton);

    bypassButton.setClickingTogglesState (true);
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorReference.getParameters(), Processor::ParameterId::isBypassed, bypassButton);

    convertButton.onClick = [this] { processorReference.getInsertConverter().requestConversion(); };
    addChildComponent (convertButton);

    clearCaptureButton.onClick = [this] { processorReference.getInsertConverter().reset(); };
    addChildComponent (clearCaptureButton);

    addAndMakeVisible (pitchCurveView);

    if (auto* documentController = processorReference.getConversionDocumentController())
        documentController->addListener (this);

    refresh();

    setResizeLimits (minimumWidth, minimumHeight, maximumWidth, maximumHeight);

    if (auto* boundsConstrainer = getConstrainer())
        boundsConstrainer->setFixedAspectRatio (static_cast<double> (defaultWidth) / defaultHeight);

    setSize (defaultWidth, defaultHeight);

    startTimerHz (refreshHz);
}

Editor::~Editor()
{
    stopTimer();

    if (auto* documentController = processorReference.getConversionDocumentController())
        documentController->removeListener (this);

    setLookAndFeel (nullptr);
}

juce::String Editor::describeLoadedVoice() const
{
    auto& loader = processorReference.getVoiceLoader();

    if (loader.isLoading())
        return "Loading " + loader.getRequestedName();

    if (auto voice = loader.getVoice())
    {
        const auto rate = voice->getManifest().modelSampleRate;
        return voice->getName()
             + (rate > 0 ? qualifierGap + juce::String (rate / 1000) + " kHz" : juce::String());
    }

    return "No voice";
}

void Editor::showVoiceMenu()
{
    auto& library = processorReference.getVoiceLoader().getLibrary();

    if (library.getEntries().empty())
        library.rescan();

    const auto& entries = library.getEntries();
    const auto loadedName = processorReference.getVoiceLoader().getRequestedName();

    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);

    for (std::size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
    {
        const auto& entry = entries[entryIndex];
        menu.addItem (static_cast<int> (entryIndex) + 1,
                      describeEntry (entry),
                      true,
                      entry.name == loadedName);
    }

    if (entries.empty())
        menu.addItem (-1, "No voices in " + VoiceModelLibrary::getUserModelDirectory().getFullPathName(), false, false);

    menu.addSeparator();
    menu.addItem (rescanItemId, "Rescan");

    menu.showMenuAsync (juce::PopupMenu::Options {}
                            .withTargetComponent (voiceButton)
                            .withMinimumWidth (voiceButton.getWidth())
                            .withStandardItemHeight (24),
                        [this, entries] (int chosenId)
                        {
                            if (chosenId == rescanItemId)
                            {
                                processorReference.getVoiceLoader().getLibrary().rescan();
                                refresh();
                                return;
                            }

                            const auto chosenIndex = chosenId - 1;

                            if (chosenIndex >= 0 && chosenIndex < static_cast<int> (entries.size()))
                                applyVoice (entries[static_cast<std::size_t> (chosenIndex)].name);
                        });
}

void Editor::applyVoice (const juce::String& name)
{
    if (auto* documentController = processorReference.getConversionDocumentController())
    {
        for (auto* modification : processorReference.getEditableModifications())
            documentController->applyVoice (*modification, name);

        return;
    }

    processorReference.getVoiceLoader().request (name, [this]
    {
        auto& converter = processorReference.getInsertConverter();

        if (converter.getCapturedSeconds() > 0.0)
            converter.requestConversion();
    });
}

ConversionModification* Editor::getFocusedModification() const
{
    const auto modifications = processorReference.getEditableModifications();
    return modifications.empty() ? nullptr : modifications.front();
}

Editor::Report Editor::describeModification() const
{
    using State = ConversionModification::State;

    Report result;
    auto* documentController = processorReference.getConversionDocumentController();

    if (const auto loadError = documentController->getLoadError(); loadError.isNotEmpty())
    {
        result.status = "Load failed";
        result.caption = loadError;
        result.isAlert = true;
        return result;
    }

    if (documentController->isLoadingVoice())
    {
        result.status = "Loading";
        result.caption = "Loading " + documentController->getRequestedVoiceName() + "...";
        result.isBusy = true;
        return result;
    }

    auto* modification = getFocusedModification();

    if (modification == nullptr)
    {
        result.status = "No region";
        result.caption = "Add RVCARA to a track with audio on it.";
        return result;
    }

    if (documentController->getVoiceModel() == nullptr)
    {
        result.status = "No voice";
        result.caption = "No voice installed.\nPut an exported voice in\n"
                       + VoiceModelLibrary::getUserModelDirectory().getFullPathName();
        return result;
    }

    result.progress = modification->getProgress();

    switch (modification->getState())
    {
        case State::idle:
            result.status = modification->getSettings().isBypassed ? "Bypassed" : "Ready";
            break;

        case State::queued:
            result.status = "Queued";
            result.isBusy = true;
            break;

        case State::rendering:
            result.status = "Converting";
            result.detail = juce::String (juce::roundToInt (result.progress * 100.0f)) + "%";
            result.isBusy = true;
            break;

        case State::ready:
            result.status = "Converted";

            if (! modification->isConversionCurrent())
                result.detail = "out of date";

            break;

        case State::failed:
            result.status = "Failed";
            result.caption = modification->getError();
            result.isAlert = true;
            break;
    }

    return result;
}

Editor::Report Editor::describeCapture() const
{
    using State = InsertConverter::State;

    Report result;
    auto& loader = processorReference.getVoiceLoader();
    auto& converter = processorReference.getInsertConverter();

    if (const auto loadError = loader.getError(); loadError.isNotEmpty())
    {
        result.status = "Load failed";
        result.caption = loadError;
        result.isAlert = true;
        return result;
    }

    if (loader.isLoading())
    {
        result.status = "Loading";
        result.caption = "Loading " + loader.getRequestedName() + "...";
        result.isBusy = true;
        return result;
    }

    if (loader.getVoice() == nullptr)
    {
        result.status = "No voice";
        result.caption = "No voice installed.\nPut an exported voice in\n"
                       + VoiceModelLibrary::getUserModelDirectory().getFullPathName();
        return result;
    }

    const auto capturedSeconds = converter.getCapturedSeconds();
    result.progress = converter.getProgress();

    switch (converter.getState())
    {
        case State::idle:
            result.status = "Waiting";
            result.caption = "Play the track. RVCARA takes the vocal in as it goes, converts it "
                             "when the transport stops, and plays the converted voice back in "
                             "place on the next pass.";
            break;

        case State::capturing:
            result.status = "Listening";
            result.detail = juce::String (capturedSeconds, 1) + " s";

            if (converter.hasReachedCaptureLimit())
                result.caption = "Capture is full; only the first "
                               + juce::String (juce::roundToInt (InsertConverter::maximumCaptureSeconds / 60.0))
                               + " minutes will convert.";

            break;

        case State::queued:
            result.status = "Queued";
            result.isBusy = true;
            break;

        case State::rendering:
            result.status = "Converting";
            result.detail = juce::String (juce::roundToInt (result.progress * 100.0f)) + "%";
            result.isBusy = true;
            break;

        case State::ready:
            result.status = "Converted";

            if (converter.isConversionStale())
                result.detail = "out of date";
            else
                result.detail = juce::String (capturedSeconds, 1) + " s";

            break;

        case State::failed:
            result.status = "Failed";
            result.caption = converter.getError();
            result.isAlert = true;
            break;
    }

    return result;
}

Editor::Report Editor::describeState() const
{
    return processorReference.isUsingARA() ? describeModification() : describeCapture();
}

void Editor::refresh()
{
    report = describeState();

    voiceButton.setButtonText (describeLoadedVoice());

    const auto isUsingARA = processorReference.isUsingARA();
    auto& converter = processorReference.getInsertConverter();
    const auto capturedSeconds = isUsingARA ? 0.0 : converter.getCapturedSeconds();

    convertButton.setVisible (! isUsingARA);
    clearCaptureButton.setVisible (! isUsingARA);
    convertButton.setEnabled (capturedSeconds > 0.0
                              && processorReference.getVoiceLoader().getVoice() != nullptr);
    clearCaptureButton.setEnabled (capturedSeconds > 0.0);

    pitchCurveView.setConversion (isUsingARA
                                      ? (getFocusedModification() != nullptr
                                             ? getFocusedModification()->getConversion()
                                             : nullptr)
                                      : converter.getConversion());

    pitchCurveView.setCaption (report.caption, report.isAlert);
    repaint();
}

void Editor::conversionStateChanged() { refresh(); }
void Editor::timerCallback()          { refresh(); }

void Editor::paintHeader (juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour (Palette::bar);
    graphics.fillRect (bounds);

    graphics.setColour (Palette::edge.withAlpha (0.6f));
    graphics.fillRect (bounds.getX(), bounds.getY(), bounds.getWidth(), 1);

    graphics.setColour (Palette::accent.withAlpha (0.55f));
    graphics.fillRect (bounds.getX(), bounds.getBottom() - 1, bounds.getWidth(), 1);

    auto textBounds = bounds.reduced (Metrics::margin, 0).toFloat();

    PanelLookAndFeel::drawTrackedText (graphics,
                                       "RVCARA",
                                       textBounds,
                                       juce::Justification::left,
                                       TypeScale::title,
                                       Metrics::tracking + 1.0f,
                                       Palette::text);

    const auto titleWidth = PanelLookAndFeel::getTrackedTextWidth ("RVCARA",
                                                                   TypeScale::title,
                                                                   Metrics::tracking + 1.0f);

    textBounds.removeFromLeft (titleWidth + 12.0f);

    PanelLookAndFeel::drawTrackedText (graphics,
                                       processorReference.isUsingARA() ? "ARA" : "INSERT",
                                       textBounds,
                                       juce::Justification::left,
                                       TypeScale::label,
                                       Metrics::tracking,
                                       Palette::dimText);
}

void Editor::paintFooter (juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour (Palette::bar);
    graphics.fillRect (bounds);
    graphics.setColour (Palette::rule);
    graphics.fillRect (bounds.getX(), bounds.getY(), bounds.getWidth(), 1);

    auto textBounds = bounds.reduced (Metrics::margin, 0).toFloat();

    const auto lampColour = report.isAlert ? Palette::alert
                          : report.isBusy  ? Palette::accent
                                           : Palette::dimText;

    const auto lamp = juce::Rectangle<float> { textBounds.getX(), textBounds.getCentreY() - 3.0f, 6.0f, 6.0f };
    graphics.setColour (lampColour);
    graphics.fillEllipse (lamp);

    textBounds.removeFromLeft (lamp.getWidth() + 8.0f);

    PanelLookAndFeel::drawTrackedText (graphics,
                                       report.status.toUpperCase(),
                                       textBounds,
                                       juce::Justification::left,
                                       TypeScale::label,
                                       Metrics::tracking,
                                       report.isAlert ? Palette::alert : Palette::text);

    if (report.detail.isNotEmpty())
    {
        const auto detailBounds = textBounds.withTrimmedRight (
            static_cast<float> (processorReference.isUsingARA() ? 0 : 2 * buttonWidth + Metrics::gap));

        PanelLookAndFeel::drawTrackedText (graphics,
                                           report.detail.toUpperCase(),
                                           detailBounds,
                                           juce::Justification::right,
                                           TypeScale::label,
                                           Metrics::tracking,
                                           Palette::dimText);
    }

    if (report.isBusy && report.progress > 0.0f)
    {
        graphics.setColour (Palette::accent);
        graphics.fillRect (bounds.getX(),
                           bounds.getY(),
                           juce::roundToInt (static_cast<float> (bounds.getWidth()) * report.progress),
                           1);
    }
}

void Editor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::ground);

    paintHeader (graphics, getLocalBounds().removeFromTop (Metrics::headerHeight));
    paintFooter (graphics, getLocalBounds().removeFromBottom (Metrics::footerHeight));
}

void Editor::resized()
{
    auto bounds = getLocalBounds();

    auto headerBounds = bounds.removeFromTop (Metrics::headerHeight).reduced (Metrics::margin, 7);
    bypassButton.setBounds (headerBounds.removeFromRight (bypassWidth));
    headerBounds.removeFromRight (Metrics::gap);
    voiceButton.setBounds (headerBounds.removeFromRight (
        juce::jmin (voiceButtonWidth, headerBounds.getWidth())));

    auto footerBounds = bounds.removeFromBottom (Metrics::footerHeight).reduced (Metrics::margin, 5);
    clearCaptureButton.setBounds (footerBounds.removeFromRight (buttonWidth));
    footerBounds.removeFromRight (Metrics::gap);
    convertButton.setBounds (footerBounds.removeFromRight (buttonWidth));

    pitchCurveView.setBounds (bounds.reduced (Metrics::margin, Metrics::gap));
}
}
