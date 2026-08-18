#include "Editor.h"

#include "model/VoiceModelLibrary.h"

namespace rvcara
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using TypeScale = PanelLookAndFeel::TypeScale;
    using Metrics = PanelLookAndFeel::Metrics;

    constexpr int defaultWidth = 980;
    constexpr int defaultHeight = 572;
    constexpr int minimumWidth = 700;
    constexpr int minimumHeight = 409;
    constexpr int maximumWidth = 1960;
    constexpr int maximumHeight = 1144;

    constexpr int refreshHz = 15;
    constexpr int voiceButtonWidth = 220;
    constexpr int rescanItemId = 10000;

    const char* qualifierGap = "   ";

    const char* bypassCaption = "The Bypass parameter is on, so RVCARA is passing the source "
                                "through unchanged.\nTurn it off in the host's parameter list.";

    juce::String describeRate (double sampleRate)
    {
        return juce::String (sampleRate / 1000.0, 1) + " kHz";
    }

    juce::String describeEntry (const VoiceModelLibrary::Entry& entry)
    {
        auto description = entry.name;

        if (entry.modelSampleRate > 0)
            description += qualifierGap + juce::String (entry.modelSampleRate / 1000) + " kHz";

        if (! entry.hasRetrieval)
            description += qualifierGap + juce::String ("no timbre index");

        return description;
    }
} // namespace

Editor::Editor (Processor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      juce::AudioProcessorEditorARAExtension (&processorToUse),
      processorReference (processorToUse)
{
    setLookAndFeel (&lookAndFeel);

    voiceButton.onClick = [this] { showVoiceMenu(); };
    voiceButton.setTriggeredOnMouseDown (true);
    addAndMakeVisible (voiceButton);

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

    return "No model";
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
        menu.addItem (static_cast<int> (entryIndex) + 1,
                      describeEntry (entries[entryIndex]),
                      true,
                      entries[entryIndex].name == loadedName);

    if (entries.empty())
        menu.addItem (-1,
                      "No models in " + VoiceModelLibrary::getUserModelDirectory().getFullPathName(),
                      false,
                      false);

    menu.addSeparator();
    menu.addItem (rescanItemId, "Rescan");

    menu.showMenuAsync (juce::PopupMenu::Options {}
                            .withTargetComponent (voiceButton)
                            .withMinimumWidth (voiceButton.getWidth())
                            .withStandardItemHeight (26),
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

    if (! processorReference.hasAssignedRegions())
    {
        result.status = "Waiting for host";
        result.caption = "The clip has not been handed to this instance yet, so the track plays "
                         "unchanged for now.\n"
                         "If it stays this way, switch ARA on for the clip in your host.";
        return result;
    }

    auto* modification = getFocusedModification();

    if (modification == nullptr)
    {
        result.status = "Waiting for host";
        result.caption = "The clip has not been handed to this instance yet, so the track plays "
                         "unchanged for now.";
        return result;
    }

    if (documentController->getVoiceModel() == nullptr)
    {
        result.status = "No model";
        result.caption = "No model installed.\nPut an exported voice in\n"
                       + VoiceModelLibrary::getUserModelDirectory().getFullPathName();
        return result;
    }

    if (auto* audioSource = modification->getAudioSource())
    {
        const auto sourceRate = audioSource->getSampleRate();
        const auto sessionRate = processorReference.getSampleRate();

        if (sourceRate > 0.0 && sessionRate > 0.0 && ! juce::approximatelyEqual (sourceRate, sessionRate))
        {
            result.status = "Rate mismatch";
            result.caption = "This region's audio is " + describeRate (sourceRate)
                           + " but the session runs at " + describeRate (sessionRate) + ".\n"
                             "RVCARA plays the source unchanged until they match.";
            result.isAlert = true;
            return result;
        }
    }

    if (modification->getSettings().isBypassed)
    {
        result.status = "Bypassed";
        result.caption = bypassCaption;
        result.isAlert = true;
        return result;
    }

    result.progress = modification->getProgress();

    switch (modification->getState())
    {
        case State::idle:
            result.status = "Ready";
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
        result.status = "No model";
        result.caption = "No model installed.\nPut an exported voice in\n"
                       + VoiceModelLibrary::getUserModelDirectory().getFullPathName();
        return result;
    }

    if (converter.hasReceivedAudio() && ! converter.hasHostTimeline())
    {
        result.status = "No timeline";
        result.caption = "This host gives RVCARA no transport position, so a take cannot be "
                         "captured and played back in place.\n"
                         "Load it in a DAW, on a track with audio on it.";
        result.isAlert = true;
        return result;
    }

    if (converter.getSettings().isBypassed)
    {
        result.status = "Bypassed";
        result.caption = bypassCaption;
        result.isAlert = true;
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
            result.detail = converter.isConversionStale() ? "out of date"
                                                          : juce::String (capturedSeconds, 1) + " s";
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

    auto* modification = processorReference.isUsingARA() ? getFocusedModification() : nullptr;

    pitchCurveView.setConversion (processorReference.isUsingARA()
                                      ? (modification != nullptr ? modification->getConversion() : nullptr)
                                      : processorReference.getInsertConverter().getConversion());

    pitchCurveView.setCaption (report.caption, report.isAlert);
    repaint();
}

void Editor::conversionStateChanged() { refresh(); }
void Editor::timerCallback()          { refresh(); }

void Editor::paintHeader (juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour (Palette::bar);
    graphics.fillRect (bounds);

    graphics.setColour (Palette::accent);
    graphics.fillRect (bounds.getX(), bounds.getBottom() - 2, bounds.getWidth(), 2);

    auto textBounds = bounds.reduced (Metrics::margin, 0).toFloat();

    PanelLookAndFeel::drawTrackedText (graphics,
                                       "RVCARA",
                                       textBounds,
                                       juce::Justification::left,
                                       TypeScale::title,
                                       Metrics::tracking + 1.0f,
                                       Palette::text);

    textBounds.removeFromLeft (PanelLookAndFeel::getTrackedTextWidth ("RVCARA",
                                                                     TypeScale::title,
                                                                     Metrics::tracking + 1.0f)
                               + 12.0f);

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

    textBounds.removeFromLeft (lamp.getWidth() + 9.0f);

    PanelLookAndFeel::drawTrackedText (graphics,
                                       report.status.toUpperCase(),
                                       textBounds,
                                       juce::Justification::left,
                                       TypeScale::label,
                                       Metrics::tracking,
                                       report.isAlert ? Palette::alert : Palette::text);

    if (report.detail.isNotEmpty())
        PanelLookAndFeel::drawTrackedText (graphics,
                                           report.detail.toUpperCase(),
                                           textBounds,
                                           juce::Justification::right,
                                           TypeScale::label,
                                           Metrics::tracking,
                                           Palette::dimText);

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

    auto headerBounds = bounds.removeFromTop (Metrics::headerHeight).reduced (Metrics::margin, 8);
    voiceButton.setBounds (headerBounds.removeFromRight (
        juce::jmin (voiceButtonWidth, headerBounds.getWidth())));

    bounds.removeFromBottom (Metrics::footerHeight);
    pitchCurveView.setBounds (bounds.reduced (Metrics::margin, Metrics::gap));
}

} // namespace rvcara
