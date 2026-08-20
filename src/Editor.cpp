#include "Editor.h"

#include "model/VoiceModelLibrary.h"

namespace rvcara
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using TypeScale = PanelLookAndFeel::TypeScale;
    using Metrics = PanelLookAndFeel::Metrics;

    constexpr int defaultWidth = 1180;
    constexpr int defaultHeight = 720;
    constexpr int minimumWidth = 880;
    constexpr int minimumHeight = 560;
    constexpr int maximumWidth = 2600;
    constexpr int maximumHeight = 1600;

    constexpr int refreshHz = 15;
    /** @brief The toolbar's geometry, taken from PitchNet's so the panel reads the same. */
    constexpr int transportSlotSize = 30;
    constexpr int transportSlotStride = 28;
    constexpr int transportPad = 9;
    constexpr int transportCapsuleHeight = 38;
    constexpr int logoRight = 120;

    constexpr int toolSlotSize = 22;
    constexpr int toolGap = 6;
    constexpr int toolPad = 15;
    constexpr int toolGroupHeight = 38;
    constexpr int rightButtonSize = 30;
    constexpr int rightSectionWidth = 250;
    constexpr int scaleButtonWidth = 96;
    constexpr int toolRadioGroup = 1;
    constexpr int rescanItemId = 10000;

    const char* qualifierGap = "   ";

    const char* bypassCaption = "The Bypass parameter is on, so RVCARA is passing the source "
                                "through unchanged.\nTurn it off in the host's parameter list.";

    /** @brief The scales snapping can land on: everything, then the two the ear expects. */
    struct Scale
    {
        const char* name;
        int degreeMask;
    };

    const Scale scales[] {
        { "Chromatic", 0xfff },
        { "Major", 0b101010110101 },
        { "Minor", 0b010101101101 },
    };

    const char* pitchClassNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

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

    const auto addTool = [this] (IconButton& button, PitchTrack::Tool tool)
    {
        button.setClickingTogglesState (true);
        button.setRadioGroupId (toolRadioGroup);
        button.onClick = [this, tool] { chooseTool (tool); };
        addAndMakeVisible (button);
    };

    addTool (selectButton, PitchTrack::Tool::select);
    addTool (splitButton, PitchTrack::Tool::split);
    addTool (anchorButton, PitchTrack::Tool::anchor);
    addTool (timingButton, PitchTrack::Tool::timing);

    selectButton.setToggleState (true, juce::dontSendNotification);

    stopButton.onClick = [this]
    {
        processorReference.setTransportPlaying (false);
        processorReference.rewindTransport();
    };

    playButton.onClick = [this]
    {
        processorReference.setTransportPlaying (! processorReference.isTransportPlaying());
    };

    loopButton.setClickingTogglesState (true);
    loopButton.onClick = [this] { isLooping = loopButton.getToggleState(); };

    for (auto* button : { &stopButton, &playButton, &loopButton })
        addAndMakeVisible (*button);

    auditionButton.setClickingTogglesState (true);
    auditionButton.setToggleState (true, juce::dontSendNotification);
    auditionButton.onClick = [this]
    {
        isAuditioning = auditionButton.getToggleState();

        if (isAuditioning)
            applyPitchEdit (pitchCurveView.getTrack().getPitchEdit());
    };
    addAndMakeVisible (auditionButton);

    quantizeButton.onClick = [this] { pitchCurveView.getTrack().snapSelection(); refresh(); };
    undoButton.onClick = [this] { pitchCurveView.getTrack().undo(); refresh(); };
    redoButton.onClick = [this] { pitchCurveView.getTrack().redo(); refresh(); };

    panelButton.setClickingTogglesState (true);
    panelButton.setToggleState (true, juce::dontSendNotification);
    panelButton.onClick = [this] { setPanelShown (panelButton.getToggleState()); };

    for (auto* button : { &quantizeButton, &undoButton, &redoButton, &panelButton })
        addAndMakeVisible (*button);

    scaleButton.setTriggeredOnMouseDown (true);
    scaleButton.onClick = [this] { showScaleMenu(); };
    addAndMakeVisible (scaleButton);

    pitchCurveView.onEditChanged = [this] (const PitchEdit& edit) { applyPitchEdit (edit); };
    pitchCurveView.onSelectionChanged = [this] { refresh(); };
    pitchCurveView.onOverviewToggled = [this] (bool isShown) { setOverviewShown (isShown); };
    addAndMakeVisible (pitchCurveView);

    propertyPanel.onVoiceClicked = [this] { showVoiceMenu(); };
    propertyPanel.onScaleClicked = [this] { showScaleMenu(); };
    propertyPanel.onChromaticChanged = [this] (bool chromatic) { applyChromatic (chromatic); };
    propertyPanel.onSnapWhileDraggingChanged = [this] (bool snap)
    {
        snapWhileDragging = snap;
        pitchCurveView.getTrack().setSnapWhileDragging (snap);
    };

    propertyPanel.onShowBeatsChanged = [this] (bool beats) { showBeats = beats; applyTimeline(); };
    propertyPanel.onBeatClicked = [this] { showBeatMenu(); };
    propertyPanel.onGridClicked = [this] { showGridMenu(); };
    propertyPanel.onTempoChanged = [this] (double bpm) { tempo = bpm; applyTimeline(); };
    propertyPanel.onSnapCycleChanged = [this] (bool snap) { snapCycle = snap; applyTimeline(); };
    propertyPanel.onBrightnessChanged = [this] (double value) { brightness = value; repaint(); };
    addAndMakeVisible (propertyPanel);

    overviewStrip.onScrubbed = [this] (double) {};
    addAndMakeVisible (overviewStrip);

    if (auto* documentController = processorReference.getConversionDocumentController())
        documentController->addListener (this);

    refresh();

    setResizeLimits (minimumWidth, minimumHeight, maximumWidth, maximumHeight);
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

void Editor::chooseTool (PitchTrack::Tool tool)
{
    pitchCurveView.getTrack().setTool (tool);
}

juce::String Editor::describeLoadedVoice() const
{
    auto& loader = processorReference.getVoiceLoader();

    if (loader.isLoading())
        return "Loading " + loader.getRequestedName();

    if (auto voice = loader.getVoice())
        return voice->getName();

    return "No model";
}

juce::String Editor::describeVoiceDetail() const
{
    auto& loader = processorReference.getVoiceLoader();

    if (auto voice = loader.getVoice())
    {
        const auto rate = voice->getManifest().modelSampleRate;
        return (rate > 0 ? juce::String (rate / 1000) + " kHz" : juce::String())
             + (isRenderingThroughARA() ? juce::String ("   ARA") : juce::String ("   INSERT"));
    }

    return VoiceModelLibrary::getUserModelDirectory().getFullPathName();
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
                            .withTargetComponent (propertyPanel)
                            .withMinimumWidth (Metrics::panelWidth - Metrics::gap * 2)
                            .withStandardItemHeight (30),
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

void Editor::showScaleMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);

    menu.addItem (1, scales[0].name, true, isChromatic);

    for (int mode = 1; mode < static_cast<int> (std::size (scales)); ++mode)
    {
        juce::PopupMenu roots;

        for (int root = 0; root < 12; ++root)
            roots.addItem (mode * 12 + root + 2,
                           juce::String (pitchClassNames[root]),
                           true,
                           scaleMode == mode && scaleRoot == root);

        menu.addSubMenu (scales[static_cast<std::size_t> (mode)].name, roots);
    }

    menu.showMenuAsync (juce::PopupMenu::Options {}
                            .withTargetComponent (scaleButton)
                            .withStandardItemHeight (28),
                        [this] (int chosen)
                        {
                            if (chosen == 1)
                                applyChromatic (true);
                            else if (chosen > 1)
                                applyScale ((chosen - 2) % 12, (chosen - 2) / 12);
                        });
}

void Editor::applyScale (int root, int mode)
{
    scaleRoot = root;
    scaleMode = mode;
    isChromatic = false;

    pitchCurveView.getTrack().setScale (root, scales[static_cast<std::size_t> (mode)].degreeMask);
    refresh();
}

void Editor::applyChromatic (bool shouldBeChromatic)
{
    isChromatic = shouldBeChromatic;

    pitchCurveView.getTrack().setScale (
        scaleRoot,
        shouldBeChromatic ? 0xfff : scales[static_cast<std::size_t> (scaleMode)].degreeMask);

    refresh();
}

void Editor::applyTimeline()
{
    pitchCurveView.setTimeline (showBeats, tempo, beatsPerBar);

    const auto secondsPerBeat = 60.0 / (tempo > 0.0 ? tempo : 120.0);
    const auto division = showBeats && snapCycle
                            ? secondsPerBeat * 4.0 / static_cast<double> (gridDivision)
                            : 0.0;

    pitchCurveView.getTrack().setTimeGrid (division);
    refresh();
}

void Editor::showBeatMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);

    const std::pair<int, int> signatures[] { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 } };

    for (int index = 0; index < static_cast<int> (std::size (signatures)); ++index)
        menu.addItem (index + 1,
                      juce::String (signatures[index].first) + "/" + juce::String (signatures[index].second),
                      true,
                      beatsPerBar == signatures[index].first && beatUnit == signatures[index].second);

    menu.showMenuAsync (juce::PopupMenu::Options {}.withTargetComponent (propertyPanel)
                                                   .withStandardItemHeight (26),
                        [this, signatures] (int chosen)
                        {
                            if (chosen <= 0)
                                return;

                            beatsPerBar = signatures[chosen - 1].first;
                            beatUnit = signatures[chosen - 1].second;
                            applyTimeline();
                        });
}

void Editor::showGridMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);

    const int divisions[] { 1, 2, 4, 8, 16 };

    for (int index = 0; index < static_cast<int> (std::size (divisions)); ++index)
        menu.addItem (index + 1,
                      "1/" + juce::String (divisions[index]),
                      true,
                      gridDivision == divisions[index]);

    menu.showMenuAsync (juce::PopupMenu::Options {}.withTargetComponent (propertyPanel)
                                                   .withStandardItemHeight (26),
                        [this, divisions] (int chosen)
                        {
                            if (chosen <= 0)
                                return;

                            gridDivision = divisions[chosen - 1];
                            applyTimeline();
                        });
}

void Editor::setPanelShown (bool isShown)
{
    isPanelShown = isShown;
    propertyPanel.setVisible (isShown);
    resized();
}

void Editor::setOverviewShown (bool isShown)
{
    isOverviewShown = isShown;
    overviewStrip.setVisible (isShown);
    resized();
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
    using State = ConversionModification::State;

    const auto modifications = processorReference.getEditableModifications();

    if (modifications.empty())
        return nullptr;

    for (auto* modification : modifications)
        if (const auto state = modification->getState();
            state == State::rendering || state == State::queued)
            return modification;

    return modifications.front();
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
        result.status = "Ready";
        return result;
    }

    if (documentController->getVoiceModel() == nullptr)
    {
        result.status = "No model";
        result.caption = "No model installed.\nPut an exported voice in\n"
                       + VoiceModelLibrary::getUserModelDirectory().getFullPathName();
        return result;
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

            if (! modification->isConversionCurrent (documentController->getSessionSampleRate()))
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

bool Editor::isRenderingThroughARA() const
{
    return processorReference.isUsingARA();
}

Editor::Report Editor::describeState() const
{
    return isRenderingThroughARA() ? describeModification() : describeCapture();
}

juce::String Editor::describeNotes (const ConversionModification& modification)
{
    switch (modification.getNoteState())
    {
        case ConversionModification::NoteState::finding:
            return "Finding notes";

        case ConversionModification::NoteState::found:
            return juce::String (static_cast<int> (modification.getPitchEdit().notes.size())) + " notes";

        case ConversionModification::NoteState::failed:
            return "No notes";

        case ConversionModification::NoteState::none:
            break;
    }

    return {};
}

void Editor::applyPitchEdit (const PitchEdit& edit)
{
    auto* documentController = processorReference.getConversionDocumentController();

    // With audition off an edit is kept but not sung, so a long take is not re-rendered under
    // every gesture.
    if (! isAuditioning)
    {
        refresh();
        return;
    }

    if (documentController != nullptr && shownModification != nullptr)
        documentController->applyPitchEdit (*shownModification, edit);

    refresh();
}

void Editor::refresh()
{
    report = describeState();

    if (const auto hostTempo = processorReference.getHostTempo(); hostTempo > 0.0 && ! showBeats)
        tempo = hostTempo;

    if (const auto hostBeats = processorReference.getHostBeatsPerBar(); hostBeats > 0 && ! showBeats)
        beatsPerBar = hostBeats;

    shownModification = isRenderingThroughARA() ? getFocusedModification() : nullptr;

    const auto conversion = shownModification != nullptr
                              ? shownModification->getConversion()
                              : (isRenderingThroughARA()
                                     ? nullptr
                                     : processorReference.getInsertConverter().getConversion());

    pitchCurveView.setConversion (conversion);
    pitchCurveView.setEditingEnabled (shownModification != nullptr);
    pitchCurveView.setPitchEdit (shownModification != nullptr ? shownModification->getPitchEdit()
                                                              : PitchEdit {});
    pitchCurveView.setPlayheadSeconds (
        shownModification != nullptr ? processorReference.getPlayheadInModification (*shownModification)
                                     : -1.0);

    overviewStrip.setConversion (conversion);
    overviewStrip.setPitchEdit (pitchCurveView.getTrack().getPitchEdit());

    if (shownModification != nullptr
        && report.caption.isEmpty()
        && shownModification->getNoteState() == ConversionModification::NoteState::failed)
    {
        report.caption = shownModification->getNoteError();
    }

    pitchCurveView.setCaption (report.caption, report.isAlert);

    auto& editorTrack = pitchCurveView.getTrack();
    const auto hasNotes = ! editorTrack.getPitchEdit().notes.empty();
    const auto canEdit = shownModification != nullptr && hasNotes;

    for (auto* button : { &selectButton, &splitButton, &anchorButton, &timingButton,
                          &quantizeButton, &auditionButton })
        button->setEnabled (canEdit);

    const auto canDrive = processorReference.canControlTransport();

    for (auto* button : { &stopButton, &playButton, &loopButton })
        button->setEnabled (canDrive);

    playButton.setToggleState (processorReference.isTransportPlaying(), juce::dontSendNotification);

    undoButton.setEnabled (canEdit && editorTrack.canUndo());
    redoButton.setEnabled (canEdit && editorTrack.canRedo());

    const auto scaleName = isChromatic
                             ? juce::String (scales[0].name)
                             : juce::String (pitchClassNames[scaleRoot]) + " "
                                   + scales[static_cast<std::size_t> (scaleMode)].name;

    scaleButton.setButtonText (scaleName);
    scaleButton.setEnabled (canEdit);

    PropertyPanel::State state;
    state.isChromatic = isChromatic;
    state.scaleRoot = scaleRoot;
    state.scaleMode = scaleMode;
    state.snapWhileDragging = snapWhileDragging;
    state.showBeats = showBeats;
    state.beatSignature = juce::String (beatsPerBar) + "/" + juce::String (beatUnit);
    state.tempo = tempo;
    state.gridDivision = "1/" + juce::String (gridDivision);
    state.snapCycle = snapCycle;
    state.brightness = brightness;
    state.voiceName = describeLoadedVoice();
    state.voiceDetail = describeVoiceDetail();
    state.status = report.status;
    state.statusDetail = report.detail;
    state.noteStatus = shownModification != nullptr ? describeNotes (*shownModification)
                                                    : juce::String ("Editing needs ARA");
    state.progress = report.progress;
    state.isBusy = report.isBusy;
    state.isAlert = report.isAlert;
    state.canEdit = canEdit;

    propertyPanel.setState (state);
    repaint();
}

void Editor::conversionStateChanged() { refresh(); }
void Editor::timerCallback()          { refresh(); }

void Editor::paintHeader (juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour (Palette::bar);
    graphics.fillRect (bounds);

    graphics.setColour (Palette::edge);
    graphics.fillRect (bounds.getX(), bounds.getBottom() - 1, bounds.getWidth(), 1);

    auto textBounds = bounds.reduced (Metrics::margin, 0).toFloat();

    const juce::Rectangle<float> mark { textBounds.getX(), textBounds.getCentreY() - 5.0f, 10.0f, 10.0f };

    graphics.setColour (Palette::accent);
    graphics.fillRoundedRectangle (mark, 2.5f);

    textBounds.removeFromLeft (mark.getWidth() + 10.0f);

    PanelLookAndFeel::drawTrackedText (graphics,
                                       "RVCARA",
                                       textBounds,
                                       juce::Justification::left,
                                       TypeScale::title,
                                       Metrics::tracking + 1.5f,
                                       Palette::text);

    for (const auto& capsule : { toolCapsule, transportCapsule })
        if (! capsule.isEmpty())
        {
            graphics.setColour (Palette::well);
            graphics.fillRoundedRectangle (capsule.toFloat(), Metrics::corner + 2.0f);
        }
}

void Editor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::ground);

    paintHeader (graphics, headerBounds);
}

void Editor::paintOverChildren (juce::Graphics& graphics)
{
    if (juce::approximatelyEqual (brightness, 100.0))
        return;

    const auto amount = static_cast<float> (std::abs (brightness - 100.0) / 100.0) * 0.55f;

    graphics.setColour ((brightness > 100.0 ? juce::Colours::white : juce::Colours::black)
                            .withAlpha (amount));
    graphics.fillRect (getLocalBounds());
}

void Editor::resized()
{
    auto bounds = getLocalBounds();

    headerBounds = bounds.removeFromTop (Metrics::headerHeight);

    {
        auto row = headerBounds.reduced (12, 0);
        const auto contentHeight = row.getHeight() - 2;

        auto rightSection = row.removeFromRight (rightSectionWidth);

        const auto placeRight = [&rightSection, contentHeight] (juce::Component& button, int spacing)
        {
            auto area = rightSection.removeFromRight (rightButtonSize + spacing);
            button.setBounds (area.getRight() - rightButtonSize,
                              1 + (contentHeight - rightButtonSize) / 2,
                              rightButtonSize, rightButtonSize);
        };

        placeRight (panelButton, 6);
        placeRight (redoButton, 2);
        placeRight (undoButton, 2);
        placeRight (auditionButton, 2);
        placeRight (quantizeButton, 2);

        scaleButton.setBounds (quantizeButton.getX() - 11 - scaleButtonWidth,
                               1 + (contentHeight - 26) / 2,
                               scaleButtonWidth, 26);

        const auto groupWidth = toolSlotSize * 4 + toolGap * 3 + toolPad * 2;

        toolCapsule = { headerBounds.getCentreX() - groupWidth / 2,
                        1 + (contentHeight - toolGroupHeight) / 2,
                        groupWidth,
                        toolGroupHeight };

        auto tools = toolCapsule.reduced (toolPad, 0);

        for (auto* button : { &selectButton, &splitButton, &anchorButton, &timingButton })
        {
            button->setBounds (tools.removeFromLeft (toolSlotSize)
                                    .withSizeKeepingCentre (toolSlotSize, toolSlotSize));
            tools.removeFromLeft (toolGap);
        }

        // The transport sits beside the wordmark, where PitchNet keeps it.
        const auto transportWidth = transportSlotSize + transportSlotStride * 2 + transportPad * 2;

        transportCapsule = { logoRight + 24,
                             1 + (contentHeight - transportCapsuleHeight) / 2,
                             transportWidth,
                             transportCapsuleHeight };

        auto slotX = transportCapsule.getX() + transportPad;

        for (auto* button : { &stopButton, &playButton, &loopButton })
        {
            button->setBounds (slotX, transportCapsule.getCentreY() - transportSlotSize / 2,
                               transportSlotSize, transportSlotSize);
            slotX += transportSlotStride;
        }
    }

    if (isPanelShown)
        propertyPanel.setBounds (bounds.removeFromRight (Metrics::panelWidth));

    if (isOverviewShown)
        overviewStrip.setBounds (bounds.removeFromBottom (Metrics::overviewHeight));

    pitchCurveView.setBounds (bounds);
}

} // namespace rvcara
