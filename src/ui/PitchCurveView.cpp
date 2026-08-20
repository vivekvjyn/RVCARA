#include "ui/PitchCurveView.h"

#include "ui/PanelLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace rvcara
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using TypeScale = PanelLookAndFeel::TypeScale;
    using Metrics = PanelLookAndFeel::Metrics;

    bool isBlackKey (int midiNote)
    {
        static const bool black[] = { false, true, false, true, false, false, true, false, true, false, true, false };
        return black[static_cast<std::size_t> (((midiNote % 12) + 12) % 12)];
    }

    juce::String describeOctave (int midiNote)
    {
        return "C" + juce::String (midiNote / 12 - 1);
    }

    constexpr int toolRadioGroup = 1;

    constexpr float zoomPerWheelNotch = 0.3f;

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

    double chooseRulerStep (float pixelsPerSecond)
    {
        for (const auto candidate : { 0.5, 1.0, 2.0, 5.0, 10.0, 30.0 })
            if (candidate * static_cast<double> (pixelsPerSecond) >= 56.0)
                return candidate;

        return 60.0;
    }
} // namespace

PitchCurveView::PitchCurveView()
{
    setOpaque (true);

    viewport.setViewedComponent (&track, false);
    viewport.setScrollBarsShown (true, true);
    viewport.setScrollBarThickness (scrollBarThickness);
    viewport.onScroll = [this] { repaint(); };
    addAndMakeVisible (viewport);

    track.onEditChanged = [this] (const PitchEdit& edit)
    {
        updateToolbar();

        if (onEditChanged != nullptr)
            onEditChanged (edit);
    };

    track.onSelectionChanged = [this] { updateToolbar(); };

    const auto addTool = [this] (juce::TextButton& button, PitchTrack::Tool tool)
    {
        button.setClickingTogglesState (true);
        button.setRadioGroupId (toolRadioGroup);
        button.onClick = [this, tool] { chooseTool (tool); };
        addAndMakeVisible (button);
    };

    addTool (selectButton, PitchTrack::Tool::select);
    addTool (splitButton, PitchTrack::Tool::split);
    addTool (glueButton, PitchTrack::Tool::glue);

    selectButton.setToggleState (true, juce::dontSendNotification);

    const auto addAction = [this] (juce::TextButton& button, std::function<void()> action)
    {
        button.onClick = std::move (action);
        addAndMakeVisible (button);
    };

    addAction (snapButton, [this] { track.snapSelection(); updateToolbar(); });
    addAction (resetButton, [this] { track.resetSelection(); updateToolbar(); });
    addAction (undoButton, [this] { track.undo(); updateToolbar(); });
    addAction (redoButton, [this] { track.redo(); updateToolbar(); });

    scaleButton.onClick = [this] { showScaleMenu(); };
    scaleButton.setTriggeredOnMouseDown (true);
    addAndMakeVisible (scaleButton);

    track.onZoomRequested = [this] (juce::Point<float> factor, juce::Point<float> anchor)
    {
        applyZoomAround (factor, anchor);
    };

    shapeSlider.setRange (0.0, 2.0);
    shapeSlider.setValue (1.0, juce::dontSendNotification);
    shapeSlider.setDoubleClickReturnValue (true, 1.0);
    shapeSlider.onDragEnd = [this] { applySelectionDepth(); };
    shapeSlider.onValueChange = [this]
    {
        if (! shapeSlider.isMouseButtonDown())
            applySelectionDepth();
    };
    addAndMakeVisible (shapeSlider);

    const auto addScaler = [this] (juce::Slider& scaler, double minimum, double maximum, double value)
    {
        scaler.setRange (minimum, maximum);
        scaler.setValue (value, juce::dontSendNotification);
        scaler.setDoubleClickReturnValue (true, value);
        scaler.onValueChange = [this] { applyZoom(); };
        addAndMakeVisible (scaler);
    };

    addScaler (horizontalScaler,
               PitchTrack::minimumPixelsPerSecond,
               PitchTrack::maximumPixelsPerSecond,
               track.getPixelsPerSecond());

    addScaler (verticalScaler,
               PitchTrack::minimumRowHeight,
               PitchTrack::maximumRowHeight,
               track.getRowHeight());

    updateToolbar();
}

void PitchCurveView::setPitchEdit (PitchEdit edit)
{
    track.setPitchEdit (std::move (edit));
    updateToolbar();
}

void PitchCurveView::setEditingEnabled (bool shouldBeEnabled)
{
    if (isEditingEnabled == shouldBeEnabled)
        return;

    isEditingEnabled = shouldBeEnabled;
    track.setInterceptsMouseClicks (shouldBeEnabled, shouldBeEnabled);
    updateToolbar();
    repaint();
}

void PitchCurveView::setNoteStatus (const juce::String& status)
{
    if (noteStatus == status)
        return;

    noteStatus = status;
    repaint();
}

void PitchCurveView::setPlayheadSeconds (double seconds)
{
    if (juce::approximatelyEqual (playheadSeconds, seconds))
        return;

    playheadSeconds = seconds;
    track.setPlayheadSeconds (seconds);
    repaint();
}

void PitchCurveView::showScaleMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    menu.addItem (1, scales[0].name, true, scaleMode == 0);

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
                            .withMinimumWidth (scaleButton.getWidth())
                            .withStandardItemHeight (24),
                        [this] (int chosen)
                        {
                            if (chosen == 1)
                                applyScale (0, 0);
                            else if (chosen > 1)
                                applyScale ((chosen - 2) % 12, (chosen - 2) / 12);
                        });
}

void PitchCurveView::applyScale (int root, int mode)
{
    scaleRoot = root;
    scaleMode = mode;

    const auto& scale = scales[static_cast<std::size_t> (mode)];
    track.setScale (root, scale.degreeMask);

    scaleButton.setButtonText (mode == 0 ? juce::String (scale.name)
                                         : juce::String (pitchClassNames[root]) + " " + scale.name);
}

void PitchCurveView::applyZoomAround (juce::Point<float> factor, juce::Point<float> anchor)
{
    const auto anchorSeconds = track.getSecondsForX (anchor.x);
    const auto anchorNote = track.getMidiNoteForY (anchor.y);

    const juce::Point<float> viewOffset { anchor.x - static_cast<float> (viewport.getViewPositionX()),
                                          anchor.y - static_cast<float> (viewport.getViewPositionY()) };

    horizontalScaler.setValue (static_cast<double> (track.getPixelsPerSecond() * factor.x),
                               juce::dontSendNotification);
    verticalScaler.setValue (static_cast<double> (track.getRowHeight() * factor.y),
                             juce::dontSendNotification);
    applyZoom();

    viewport.setViewPosition (
        factor.x != 1.0f ? juce::roundToInt (track.getXForSeconds (anchorSeconds) - viewOffset.x)
                         : viewport.getViewPositionX(),
        factor.y != 1.0f ? juce::roundToInt (track.getRowCentre (anchorNote) - viewOffset.y)
                         : viewport.getViewPositionY());
}

void PitchCurveView::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto factor = 1.0f + wheel.deltaY * zoomPerWheelNotch;
    const auto position = event.getPosition();

    if (keyboardBounds.contains (position))
    {
        applyZoomAround ({ 1.0f, factor },
                         { 0.0f, static_cast<float> (position.y - viewport.getY()
                                                     + viewport.getViewPositionY()) });
        return;
    }

    if (rulerBounds.contains (position) || waveformBounds.contains (position))
    {
        applyZoomAround ({ factor, 1.0f },
                         { static_cast<float> (position.x - viewport.getX()
                                               + viewport.getViewPositionX()), 0.0f });
    }
}

void PitchCurveView::chooseTool (PitchTrack::Tool tool)
{
    track.setTool (tool);
}

void PitchCurveView::applySelectionDepth()
{
    track.setSelectionDepth (static_cast<float> (shapeSlider.getValue()));
    updateToolbar();
}

void PitchCurveView::updateToolbar()
{
    const auto hasNotes = ! track.getPitchEdit().notes.empty();
    const auto canEdit = isEditingEnabled && hasNotes;

    for (auto* button : { &selectButton, &splitButton, &glueButton, &snapButton, &resetButton })
        button->setEnabled (canEdit);

    undoButton.setEnabled (canEdit && track.canUndo());
    redoButton.setEnabled (canEdit && track.canRedo());

    shapeSlider.setEnabled (canEdit);

    if (! shapeSlider.isMouseButtonDown())
        shapeSlider.setValue (static_cast<double> (track.getSelectionDepth()), juce::dontSendNotification);
}

void PitchCurveView::setConversion (ConversionPointer newConversion)
{
    if (conversion == newConversion)
        return;

    const auto isFirst = conversion == nullptr;

    conversion = newConversion;
    track.setConversion (std::move (newConversion));
    rebuildWaveform();
    applyZoom();

    if (isFirst)
        scrollToContent();
}

void PitchCurveView::scrollToContent()
{
    const auto centre = track.getRowCentre (track.getCentreNote());
    const auto visibleHeight = static_cast<float> (viewport.getMaximumVisibleHeight());

    viewport.setViewPosition (viewport.getViewPositionX(),
                              juce::roundToInt (centre - visibleHeight * 0.5f));
}

void PitchCurveView::setCaption (const juce::String& newCaption, bool isAlert)
{
    if (caption == newCaption && isCaptionAlert == isAlert)
        return;

    caption = newCaption;
    isCaptionAlert = isAlert;
    repaint();
}

void PitchCurveView::rebuildWaveform()
{
    waveform.clear();

    if (conversion == nullptr || conversion->samples.empty())
        return;

    const auto numSamples = static_cast<int> (conversion->samples.size());
    const auto numPoints = std::min (waveformResolution, numSamples);
    const auto samplesPerPoint = std::max (numSamples / numPoints, 1);

    waveform.resize (static_cast<std::size_t> (numPoints), 0.0f);

    for (int pointIndex = 0; pointIndex < numPoints; ++pointIndex)
    {
        const auto first = pointIndex * samplesPerPoint;
        const auto last = std::min (first + samplesPerPoint, numSamples);

        auto peak = 0.0f;

        for (int sampleIndex = first; sampleIndex < last; ++sampleIndex)
            peak = std::max (peak, std::abs (conversion->samples[static_cast<std::size_t> (sampleIndex)]));

        waveform[static_cast<std::size_t> (pointIndex)] = peak;
    }
}

void PitchCurveView::applyZoom()
{
    track.setZoom (static_cast<float> (horizontalScaler.getValue()),
                   static_cast<float> (verticalScaler.getValue()));

    const auto size = track.getPreferredSize (viewport.getMaximumVisibleWidth());
    track.setSize (size.x, std::max (size.y, viewport.getMaximumVisibleHeight()));

    repaint();
}

void PitchCurveView::resized()
{
    auto bounds = getLocalBounds().reduced (1);

    toolbarBounds = bounds.removeFromTop (toolbarHeight);
    auto toolbarRow = toolbarBounds.reduced (Metrics::gap, 3);

    const auto place = [&toolbarRow] (juce::Component& component, int width)
    {
        component.setBounds (toolbarRow.removeFromLeft (width).withTrimmedRight (3));
    };

    place (selectButton, toolButtonWidth);
    place (splitButton, toolButtonWidth);
    place (glueButton, toolButtonWidth);
    toolbarRow.removeFromLeft (Metrics::gap);
    place (snapButton, toolButtonWidth);
    place (resetButton, toolButtonWidth);
    toolbarRow.removeFromLeft (Metrics::gap);
    place (undoButton, toolButtonWidth);
    place (redoButton, toolButtonWidth);
    toolbarRow.removeFromLeft (Metrics::gap);
    shapeLabelBounds = toolbarRow.removeFromLeft (44);
    place (shapeSlider, 96);
    toolbarRow.removeFromLeft (Metrics::gap);
    place (scaleButton, scaleButtonWidth);

    noteStatusBounds = toolbarRow;

    auto scalerRow = bounds.removeFromBottom (scalerHeight);
    scalerRow.removeFromLeft (keyboardWidth);
    verticalScaler.setBounds (scalerRow.removeFromRight (88).reduced (4, 3));
    pitchLabelBounds = scalerRow.removeFromRight (42);
    horizontalScaler.setBounds (scalerRow.removeFromRight (88).reduced (4, 3));
    timeLabelBounds = scalerRow.removeFromRight (38);

    rulerBounds = bounds.removeFromTop (rulerHeight).withTrimmedLeft (keyboardWidth);
    waveformBounds = bounds.removeFromBottom (waveformHeight).withTrimmedLeft (keyboardWidth);
    keyboardBounds = bounds.removeFromLeft (keyboardWidth);

    viewport.setBounds (bounds);
    applyZoom();
}

void PitchCurveView::paintToolbar (juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour (Palette::bar);
    graphics.fillRect (bounds);

    graphics.setColour (Palette::edge);
    graphics.fillRect (static_cast<float> (bounds.getX()),
                       static_cast<float> (bounds.getBottom()) - Metrics::hairline,
                       static_cast<float> (bounds.getWidth()),
                       Metrics::hairline);

    PanelLookAndFeel::drawTrackedText (graphics, "SHAPE", shapeLabelBounds.toFloat(),
                                       juce::Justification::left, TypeScale::label,
                                       Metrics::tracking * 0.5f, Palette::dimText);

    PanelLookAndFeel::drawTrackedText (graphics, noteStatus.toUpperCase(), noteStatusBounds.toFloat(),
                                       juce::Justification::right, TypeScale::label,
                                       Metrics::tracking * 0.5f, Palette::dimText);
}

void PitchCurveView::paintKeyboard (juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour (Palette::bar);
    graphics.fillRect (bounds);

    const auto rowHeight = track.getRowHeight();
    const auto originY = static_cast<float> (bounds.getY() - viewport.getViewPositionY());

    for (int midiNote = PitchTrack::lowestNote; midiNote <= PitchTrack::highestNote; ++midiNote)
    {
        const auto centre = originY + track.getRowCentre (midiNote);
        const juce::Rectangle<float> key { static_cast<float> (bounds.getX()),
                                           centre - rowHeight * 0.5f,
                                           static_cast<float> (bounds.getWidth())
                                               * (isBlackKey (midiNote) ? 0.62f : 1.0f),
                                           rowHeight };

        if (key.getBottom() < static_cast<float> (bounds.getY())
            || key.getY() > static_cast<float> (bounds.getBottom()))
            continue;

        graphics.setColour (isBlackKey (midiNote) ? Palette::blackKey : Palette::whiteKey);
        graphics.fillRect (key.reduced (0.0f, 0.5f));

        if (! isBlackKey (midiNote) && midiNote % 12 == 0 && rowHeight > 7.0f)
            PanelLookAndFeel::drawTrackedText (graphics,
                                               describeOctave (midiNote),
                                               key.reduced (4.0f, 0.0f),
                                               juce::Justification::left,
                                               TypeScale::label,
                                               0.0f,
                                               Palette::ground);
    }

    graphics.setColour (Palette::edge);
    graphics.fillRect (static_cast<float> (bounds.getRight()) - Metrics::hairline,
                       static_cast<float> (bounds.getY()),
                       Metrics::hairline,
                       static_cast<float> (bounds.getHeight()));
}

void PitchCurveView::paintRuler (juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour (Palette::bar);
    graphics.fillRect (bounds);

    const auto pixelsPerSecond = track.getPixelsPerSecond();
    const auto originX = static_cast<float> (bounds.getX() - viewport.getViewPositionX());
    const auto step = chooseRulerStep (pixelsPerSecond);

    for (auto second = 0.0; second <= track.getSeconds(); second += step)
    {
        const auto x = originX + static_cast<float> (second * static_cast<double> (pixelsPerSecond));

        if (x < static_cast<float> (bounds.getX()) || x > static_cast<float> (bounds.getRight()))
            continue;

        graphics.setColour (Palette::dimText);
        graphics.fillRect (x, static_cast<float> (bounds.getBottom()) - 4.0f, Metrics::hairline, 4.0f);

        PanelLookAndFeel::drawTrackedText (graphics,
                                           juce::String (second, step < 1.0 ? 1 : 0) + "s",
                                           juce::Rectangle<float> { x + 4.0f,
                                                                    static_cast<float> (bounds.getY()),
                                                                    46.0f,
                                                                    static_cast<float> (bounds.getHeight()) },
                                           juce::Justification::left,
                                           TypeScale::label,
                                           Metrics::tracking * 0.5f,
                                           Palette::dimText);
    }

    graphics.setColour (Palette::edge);
    graphics.fillRect (static_cast<float> (bounds.getX()),
                       static_cast<float> (bounds.getBottom()) - Metrics::hairline,
                       static_cast<float> (bounds.getWidth()),
                       Metrics::hairline);

    if (playheadSeconds >= 0.0)
    {
        const auto x = originX + track.getXForSeconds (playheadSeconds);

        if (x >= static_cast<float> (bounds.getX()) && x <= static_cast<float> (bounds.getRight()))
        {
            graphics.setColour (Palette::accent);
            graphics.fillRect (x - 1.0f, static_cast<float> (bounds.getY()), 2.0f,
                               static_cast<float> (bounds.getHeight()));
        }
    }
}

void PitchCurveView::paintWaveform (juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour (Palette::well);
    graphics.fillRect (bounds);

    graphics.setColour (Palette::edge);
    graphics.fillRect (static_cast<float> (bounds.getX()),
                       static_cast<float> (bounds.getY()),
                       static_cast<float> (bounds.getWidth()),
                       Metrics::hairline);

    if (waveform.empty() || track.getWidth() <= 0)
        return;

    const auto centreY = static_cast<float> (bounds.getCentreY());
    const auto halfHeight = static_cast<float> (bounds.getHeight()) * 0.42f;
    const auto trackWidth = static_cast<float> (track.getWidth());
    const auto lastPoint = static_cast<float> (waveform.size() - 1);

    graphics.setColour (Palette::silhouette);

    for (int x = 0; x < bounds.getWidth(); ++x)
    {
        const auto position = static_cast<float> (x + viewport.getViewPositionX()) / trackWidth;

        if (position < 0.0f || position > 1.0f)
            continue;

        const auto peak = waveform[static_cast<std::size_t> (position * lastPoint)];

        graphics.fillRect (static_cast<float> (bounds.getX() + x),
                           centreY - peak * halfHeight,
                           1.0f,
                           std::max (peak * halfHeight * 2.0f, 1.0f));
    }
}

void PitchCurveView::paintCaption (juce::Graphics& graphics) const
{
    const auto bounds = getLocalBounds().toFloat();
    const auto plate = bounds.withSizeKeepingCentre (std::min (400.0f, bounds.getWidth() - 40.0f), 82.0f);

    graphics.setColour (Palette::bar);
    graphics.fillRect (plate);
    graphics.setColour (Palette::edge);
    graphics.drawRect (plate, Metrics::hairline);

    graphics.setColour (isCaptionAlert ? Palette::alert : Palette::dimText);
    graphics.setFont (juce::Font { juce::FontOptions { TypeScale::caption } });
    graphics.drawFittedText (caption, plate.reduced (16.0f, 10.0f).toNearestInt(), juce::Justification::centred, 4);
}

void PitchCurveView::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::well);

    paintToolbar (graphics, toolbarBounds);
    paintKeyboard (graphics, keyboardBounds);
    paintRuler (graphics, rulerBounds);
    paintWaveform (graphics, waveformBounds);
}

void PitchCurveView::paintOverChildren (juce::Graphics& graphics)
{
    graphics.setColour (Palette::edge);
    graphics.drawRect (getLocalBounds().toFloat(), Metrics::hairline);

    PanelLookAndFeel::drawTrackedText (graphics, "TIME", timeLabelBounds.toFloat(),
                                       juce::Justification::right, TypeScale::label,
                                       Metrics::tracking * 0.5f, Palette::dimText);

    PanelLookAndFeel::drawTrackedText (graphics, "PITCH", pitchLabelBounds.toFloat(),
                                       juce::Justification::right, TypeScale::label,
                                       Metrics::tracking * 0.5f, Palette::dimText);

    if (caption.isNotEmpty())
        paintCaption (graphics);
}

} // namespace rvcara
