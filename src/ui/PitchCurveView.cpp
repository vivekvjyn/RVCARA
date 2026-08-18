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

    auto scalerRow = bounds.removeFromBottom (scalerHeight);
    scalerRow.removeFromLeft (keyboardWidth);
    verticalScaler.setBounds (scalerRow.removeFromRight (88).reduced (4, 3));
    pitchLabelBounds = scalerRow.removeFromRight (42);
    horizontalScaler.setBounds (scalerRow.removeFromRight (88).reduced (4, 3));
    timeLabelBounds = scalerRow.removeFromRight (38);

    bounds.removeFromTop (rulerHeight);
    bounds.removeFromBottom (waveformHeight);
    bounds.removeFromLeft (keyboardWidth);

    viewport.setBounds (bounds);
    applyZoom();
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

    auto bounds = getLocalBounds().reduced (1);
    bounds.removeFromBottom (scalerHeight);

    const auto rulerBounds = bounds.removeFromTop (rulerHeight);
    const auto waveformBounds = bounds.removeFromBottom (waveformHeight);

    paintKeyboard (graphics, bounds.removeFromLeft (keyboardWidth));
    paintRuler (graphics, rulerBounds.withTrimmedLeft (keyboardWidth));
    paintWaveform (graphics, waveformBounds.withTrimmedLeft (keyboardWidth));
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
