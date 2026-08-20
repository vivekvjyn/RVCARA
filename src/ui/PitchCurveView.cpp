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

    constexpr float zoomPerWheelNotch = 0.3f;

    const char* pitchClassNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    juce::String describeNote (int midiNote)
    {
        return juce::String (pitchClassNames[static_cast<std::size_t> (((midiNote % 12) + 12) % 12)])
             + juce::String (midiNote / 12 - 1);
    }

    juce::String describeTime (double seconds, double step)
    {
        const auto whole = static_cast<int> (seconds);
        auto text = juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2);

        if (step < 1.0)
            text += juce::String (seconds - std::floor (seconds), 1).substring (1);

        return text;
    }

    bool isNatural (int midiNote)
    {
        static const bool natural[] = { true, false, true, false, true, true, false, true, false, true, false, true };
        return natural[static_cast<std::size_t> (((midiNote % 12) + 12) % 12)];
    }

    double chooseRulerStep (float pixelsPerSecond)
    {
        for (const auto candidate : { 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0 })
            if (candidate * static_cast<double> (pixelsPerSecond) >= 78.0)
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
        if (onEditChanged != nullptr)
            onEditChanged (edit);
    };

    track.onSelectionChanged = [this]
    {
        if (onSelectionChanged != nullptr)
            onSelectionChanged();
    };

    track.onZoomRequested = [this] (juce::Point<float> factor, juce::Point<float> anchor)
    {
        applyZoomAround (factor, anchor);
    };

    const auto addZoom = [this] (juce::Slider& slider, double minimum, double maximum, double value)
    {
        slider.setRange (minimum, maximum);
        slider.setValue (value, juce::dontSendNotification);
        slider.setDoubleClickReturnValue (true, value);
        slider.onValueChange = [this] { applyZoom(); };
        addAndMakeVisible (slider);
    };

    addZoom (verticalZoom, PitchTrack::minimumRowHeight, PitchTrack::maximumRowHeight,
             track.getRowHeight());

    addZoom (horizontalZoom, PitchTrack::minimumPixelsPerSecond, PitchTrack::maximumPixelsPerSecond,
             track.getPixelsPerSecond());

    fitButton.onClick = [this] { zoomToFit(); };
    addAndMakeVisible (fitButton);

    overviewButton.setClickingTogglesState (true);
    overviewButton.setToggleState (true, juce::dontSendNotification);
    overviewButton.onClick = [this]
    {
        if (onOverviewToggled != nullptr)
            onOverviewToggled (overviewButton.getToggleState());
    };
    addAndMakeVisible (overviewButton);
}

void PitchCurveView::setConversion (ConversionPointer newConversion)
{
    if (conversion == newConversion)
        return;

    conversion = newConversion;
    track.setConversion (std::move (newConversion));
    applyZoom();

    if (! hasScrolledToContent && conversion != nullptr)
    {
        hasScrolledToContent = true;
        zoomToFit();
    }
}

void PitchCurveView::setPitchEdit (PitchEdit edit)
{
    track.setPitchEdit (std::move (edit));
}

void PitchCurveView::setEditingEnabled (bool shouldBeEnabled)
{
    if (isEditingEnabled == shouldBeEnabled)
        return;

    isEditingEnabled = shouldBeEnabled;
    track.setInterceptsMouseClicks (shouldBeEnabled, shouldBeEnabled);
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

void PitchCurveView::setOverviewShown (bool isShown)
{
    overviewButton.setToggleState (isShown, juce::dontSendNotification);
}

void PitchCurveView::setTimeline (bool shouldShowBeats, double newTempo, int newBeatsPerBar)
{
    if (showBeats == shouldShowBeats
        && juce::approximatelyEqual (tempo, newTempo)
        && beatsPerBar == newBeatsPerBar)
        return;

    showBeats = shouldShowBeats;
    tempo = newTempo > 0.0 ? newTempo : 120.0;
    beatsPerBar = newBeatsPerBar > 0 ? newBeatsPerBar : 4;
    repaint();
}

void PitchCurveView::setCaption (const juce::String& newCaption, bool isAlert)
{
    if (caption == newCaption && isCaptionAlert == isAlert)
        return;

    caption = newCaption;
    isCaptionAlert = isAlert;
    repaint();
}

void PitchCurveView::zoomToFit()
{
    if (const auto seconds = track.getSeconds(); seconds > 0.0)
    {
        const auto wanted = static_cast<double> (viewport.getMaximumVisibleWidth()) / seconds;
        horizontalZoom.setValue (wanted, juce::dontSendNotification);
    }

    applyZoom();
    scrollToContent();
}

void PitchCurveView::scrollToContent()
{
    const auto centre = track.getRowCentre (static_cast<float> (track.getCentreNote()));
    const auto visibleHeight = static_cast<float> (viewport.getMaximumVisibleHeight());

    viewport.setViewPosition (0, juce::roundToInt (centre - visibleHeight * 0.5f));
}

void PitchCurveView::applyZoom()
{
    track.setZoom (static_cast<float> (horizontalZoom.getValue()),
                   static_cast<float> (verticalZoom.getValue()));

    const auto size = track.getPreferredSize (viewport.getMaximumVisibleWidth());
    track.setSize (size.x, std::max (size.y, viewport.getMaximumVisibleHeight()));

    repaint();
}

void PitchCurveView::applyZoomAround (juce::Point<float> factor, juce::Point<float> anchor)
{
    const auto anchorSeconds = track.getSecondsForX (anchor.x);
    const auto anchorNote = track.getMidiNoteForY (anchor.y);

    const juce::Point<float> viewOffset { anchor.x - static_cast<float> (viewport.getViewPositionX()),
                                          anchor.y - static_cast<float> (viewport.getViewPositionY()) };

    horizontalZoom.setValue (static_cast<double> (track.getPixelsPerSecond() * factor.x),
                             juce::dontSendNotification);
    verticalZoom.setValue (static_cast<double> (track.getRowHeight() * factor.y),
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

    if (gutterBounds.contains (position))
    {
        applyZoomAround ({ 1.0f, factor },
                         { 0.0f, static_cast<float> (position.y - viewport.getY()
                                                     + viewport.getViewPositionY()) });
        return;
    }

    if (rulerBounds.contains (position))
        applyZoomAround ({ factor, 1.0f },
                         { static_cast<float> (position.x - viewport.getX()
                                               + viewport.getViewPositionX()), 0.0f });
}

void PitchCurveView::resized()
{
    auto bounds = getLocalBounds();

    rulerBounds = bounds.removeFromTop (rulerHeight).withTrimmedLeft (gutterWidth);
    gutterBounds = bounds.removeFromLeft (gutterWidth);

    viewport.setBounds (bounds);

    // The zoom controls float over the grid's bottom right rather than taking a lane from it.
    auto corner = bounds.reduced (floatingInset);
    corner.removeFromBottom (scrollBarThickness);

    auto row = corner.removeFromBottom (floatingButtonSize)
                     .removeFromRight (floatingButtonSize * 2 + zoomSliderLength + floatingInset * 2
                                       + scrollBarThickness);
    row.removeFromRight (scrollBarThickness);

    fitButton.setBounds (row.removeFromLeft (floatingButtonSize));
    row.removeFromLeft (floatingInset);
    horizontalZoom.setBounds (row.removeFromLeft (zoomSliderLength));
    row.removeFromLeft (floatingInset);
    overviewButton.setBounds (row.removeFromLeft (floatingButtonSize));

    verticalZoom.setBounds (bounds.getRight() - floatingInset - zoomSliderThickness
                                - scrollBarThickness,
                            bounds.getY() + floatingInset,
                            zoomSliderThickness,
                            zoomSliderLength);

    applyZoom();
}

void PitchCurveView::paintGutter (juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour (Palette::bar);
    graphics.fillRect (bounds);

    const auto rowHeight = track.getRowHeight();
    const auto originY = static_cast<float> (bounds.getY() - viewport.getViewPositionY());

    for (int midiNote = PitchTrack::lowestNote; midiNote <= PitchTrack::highestNote; ++midiNote)
    {
        const auto centre = originY + track.getRowCentre (static_cast<float> (midiNote));

        if (centre + rowHeight < static_cast<float> (bounds.getY())
            || centre - rowHeight > static_cast<float> (bounds.getBottom()))
            continue;

        graphics.setColour (Palette::rule);
        graphics.fillRect (static_cast<float> (bounds.getX()),
                           centre + rowHeight * 0.5f - Metrics::hairline,
                           static_cast<float> (bounds.getWidth()),
                           Metrics::hairline);

        const auto isShown = rowHeight >= 15.0f
                          || (rowHeight >= 10.0f && isNatural (midiNote))
                          || midiNote % 12 == 0;

        if (! isShown)
            continue;

        PanelLookAndFeel::drawTrackedText (graphics,
                                           describeNote (midiNote),
                                           juce::Rectangle<float> { static_cast<float> (bounds.getX()),
                                                                    centre - rowHeight * 0.5f,
                                                                    static_cast<float> (bounds.getWidth()) - 10.0f,
                                                                    rowHeight },
                                           juce::Justification::right,
                                           TypeScale::label,
                                           0.0f,
                                           midiNote % 12 == 0 ? Palette::text : Palette::dimText);
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

    const auto secondsPerBeat = 60.0 / tempo;
    const auto step = showBeats ? secondsPerBeat * beatsPerBar : chooseRulerStep (pixelsPerSecond);

    for (auto second = 0.0; second <= track.getSeconds() + step; second += step)
    {
        const auto barNumber = 1 + juce::roundToInt (second / step);

        const auto x = originX + static_cast<float> (second * static_cast<double> (pixelsPerSecond));

        if (x < static_cast<float> (bounds.getX()) - 1.0f || x > static_cast<float> (bounds.getRight()))
            continue;

        graphics.setColour (Palette::edge);
        graphics.fillRect (x, static_cast<float> (bounds.getY()), Metrics::hairline,
                           static_cast<float> (bounds.getHeight()));

        PanelLookAndFeel::drawTrackedText (graphics,
                                           showBeats ? juce::String (barNumber)
                                                     : describeTime (second, step),
                                           juce::Rectangle<float> { x + 7.0f,
                                                                    static_cast<float> (bounds.getY()),
                                                                    60.0f,
                                                                    static_cast<float> (bounds.getHeight()) },
                                           juce::Justification::left,
                                           TypeScale::label,
                                           0.0f,
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

void PitchCurveView::paintCaption (juce::Graphics& graphics) const
{
    const auto bounds = getLocalBounds().toFloat();
    const auto plate = bounds.withSizeKeepingCentre (std::min (440.0f, bounds.getWidth() - 60.0f), 96.0f);

    graphics.setColour (Palette::card);
    graphics.fillRoundedRectangle (plate, Metrics::corner);
    graphics.setColour (isCaptionAlert ? Palette::alert : Palette::edge);
    graphics.drawRoundedRectangle (plate.reduced (0.5f), Metrics::corner, Metrics::hairline);

    graphics.setColour (isCaptionAlert ? Palette::alert : Palette::dimText);
    graphics.setFont (juce::Font { juce::FontOptions { TypeScale::caption } });
    graphics.drawFittedText (caption, plate.reduced (18.0f, 12.0f).toNearestInt(),
                             juce::Justification::centred, 4);
}

void PitchCurveView::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::grid);

    paintGutter (graphics, gutterBounds);
    paintRuler (graphics, rulerBounds);
}

void PitchCurveView::paintOverChildren (juce::Graphics& graphics)
{
    if (caption.isNotEmpty())
        paintCaption (graphics);
}

} // namespace rvcara
