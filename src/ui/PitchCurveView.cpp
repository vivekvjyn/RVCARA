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

    constexpr int envelopeResolution = 512;

    constexpr float timeRulerHeight = 14.0f;

    constexpr int lowestGridNote = 36;
    constexpr int highestGridNote = 84;

    juce::String describeNote (double frequencyHz)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        const auto midiNote = static_cast<int> (std::lround (69.0 + 12.0 * std::log2 (frequencyHz / 440.0)));
        const auto octave = midiNote / 12 - 1;

        return juce::String (names[static_cast<std::size_t> (midiNote % 12)]) + juce::String (octave);
    }

    double chooseTickSeconds (double totalSeconds, float widthInPixels)
    {
        constexpr double candidates[] = { 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 };
        const auto minimumSpacingInPixels = 56.0;

        for (const auto candidate : candidates)
            if (candidate / totalSeconds * static_cast<double> (widthInPixels) >= minimumSpacingInPixels)
                return candidate;

        return candidates[std::size (candidates) - 1];
    }
}

int PitchCurveView::getGridStepInSemitones (juce::Rectangle<float> bounds)
{
    constexpr float smallestUsefulSemitoneSpacing = 9.0f;

    const auto semitoneSpacing = bounds.getHeight() / static_cast<float> (highestGridNote - lowestGridNote);
    return semitoneSpacing >= smallestUsefulSemitoneSpacing ? 1 : 12;
}

PitchCurveView::PitchCurveView()
{
    setOpaque (true);
}

void PitchCurveView::setConversion (ConversionPointer newConversion)
{
    if (conversion == newConversion)
        return;

    conversion = std::move (newConversion);
    rebuildEnvelope();
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

double PitchCurveView::getConversionSeconds() const
{
    if (conversion == nullptr || conversion->pitchFrameRate <= 0.0)
        return 0.0;

    return static_cast<double> (conversion->fundamentalFrequencyHz.size()) / conversion->pitchFrameRate;
}

void PitchCurveView::rebuildEnvelope()
{
    envelope.clear();

    if (conversion == nullptr || conversion->samples.empty())
        return;

    const auto numSamples = static_cast<int> (conversion->samples.size());
    const auto numPoints = std::min (envelopeResolution, numSamples);
    const auto samplesPerPoint = std::max (numSamples / numPoints, 1);

    envelope.resize (static_cast<std::size_t> (numPoints), 0.0f);

    for (int pointIndex = 0; pointIndex < numPoints; ++pointIndex)
    {
        const auto first = pointIndex * samplesPerPoint;
        const auto last = std::min (first + samplesPerPoint, numSamples);

        auto peak = 0.0f;

        for (int sampleIndex = first; sampleIndex < last; ++sampleIndex)
            peak = std::max (peak, std::abs (conversion->samples[static_cast<std::size_t> (sampleIndex)]));

        envelope[static_cast<std::size_t> (pointIndex)] = peak;
    }
}

std::optional<float> PitchCurveView::getYForFrequency (float frequencyHz, juce::Rectangle<float> bounds) const
{
    if (frequencyHz < lowestDisplayedHz || frequencyHz > highestDisplayedHz)
        return std::nullopt;

    const auto span = std::log2 (highestDisplayedHz / lowestDisplayedHz);
    const auto position = std::log2 (frequencyHz / lowestDisplayedHz) / span;

    return bounds.getBottom() - position * bounds.getHeight();
}

void PitchCurveView::paintPitchGrid (juce::Graphics& graphics, juce::Rectangle<float> bounds) const
{
    for (int midiNote = lowestGridNote; midiNote <= highestGridNote; midiNote += getGridStepInSemitones (bounds))
    {
        const auto frequencyHz = static_cast<float> (440.0 * std::exp2 ((midiNote - 69) / 12.0));
        const auto y = getYForFrequency (frequencyHz, bounds);

        if (! y.has_value())
            continue;

        graphics.setColour (midiNote % 12 == 0 ? Palette::edge : Palette::rule);
        graphics.fillRect (bounds.getX(), *y, bounds.getWidth(), Metrics::hairline);
    }
}

void PitchCurveView::paintOctaveNames (juce::Graphics& graphics, juce::Rectangle<float> bounds) const
{
    constexpr float labelHeight = 12.0f;

    for (int midiNote = lowestGridNote; midiNote <= highestGridNote; midiNote += 12)
    {
        const auto frequencyHz = static_cast<float> (440.0 * std::exp2 ((midiNote - 69) / 12.0));
        const auto y = getYForFrequency (frequencyHz, bounds);

        if (! y.has_value())
            continue;

        const auto labelBounds = juce::Rectangle<float> { bounds.getX() + 5.0f,
                                                          juce::jlimit (bounds.getY(),
                                                                        bounds.getBottom() - labelHeight,
                                                                        *y - labelHeight * 0.5f),
                                                          40.0f,
                                                          labelHeight };

        PanelLookAndFeel::drawTrackedText (graphics,
                                           describeNote (frequencyHz),
                                           labelBounds,
                                           juce::Justification::left,
                                           TypeScale::label,
                                           Metrics::tracking * 0.5f,
                                           Palette::dimText);
    }
}

void PitchCurveView::paintTimeRuler (juce::Graphics& graphics, juce::Rectangle<float> bounds) const
{
    const auto totalSeconds = getConversionSeconds();

    if (totalSeconds <= 0.0)
        return;

    const auto tickSeconds = chooseTickSeconds (totalSeconds, bounds.getWidth());

    for (auto seconds = tickSeconds; seconds < totalSeconds; seconds += tickSeconds)
    {
        const auto x = bounds.getX()
                     + static_cast<float> (seconds / totalSeconds) * bounds.getWidth();

        graphics.setColour (Palette::rule);
        graphics.fillRect (x, bounds.getY(), Metrics::hairline, bounds.getHeight() - timeRulerHeight);

        PanelLookAndFeel::drawTrackedText (graphics,
                                           juce::String (seconds, tickSeconds < 1.0 ? 1 : 0) + "s",
                                           juce::Rectangle<float> { x + 4.0f,
                                                                    bounds.getBottom() - timeRulerHeight,
                                                                    46.0f,
                                                                    timeRulerHeight },
                                           juce::Justification::left,
                                           TypeScale::label,
                                           Metrics::tracking * 0.5f,
                                           Palette::dimText);
    }
}

void PitchCurveView::paintEnvelope (juce::Graphics& graphics, juce::Rectangle<float> bounds) const
{
    if (envelope.size() < 2)
        return;

    juce::Path silhouette;
    const auto centreY = bounds.getCentreY();
    const auto halfHeight = bounds.getHeight() * 0.5f;
    const auto lastIndex = static_cast<float> (envelope.size() - 1);

    silhouette.startNewSubPath (bounds.getX(), centreY);

    for (std::size_t pointIndex = 0; pointIndex < envelope.size(); ++pointIndex)
    {
        const auto position = static_cast<float> (pointIndex) / lastIndex;
        const auto x = bounds.getX() + position * bounds.getWidth();
        silhouette.lineTo (x, centreY - envelope[pointIndex] * halfHeight);
    }

    for (auto pointIndex = static_cast<int> (envelope.size()) - 1; pointIndex >= 0; --pointIndex)
    {
        const auto position = static_cast<float> (pointIndex) / lastIndex;
        const auto x = bounds.getX() + position * bounds.getWidth();
        silhouette.lineTo (x, centreY + envelope[static_cast<std::size_t> (pointIndex)] * halfHeight);
    }

    silhouette.closeSubPath();

    graphics.setColour (Palette::silhouette);
    graphics.fillPath (silhouette);
}

void PitchCurveView::paintPitch (juce::Graphics& graphics, juce::Rectangle<float> bounds) const
{
    if (conversion == nullptr || conversion->fundamentalFrequencyHz.empty())
        return;

    const auto& track = conversion->fundamentalFrequencyHz;
    const auto numFrames = static_cast<int> (track.size());

    juce::Path curve;
    auto isDrawing = false;

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        const auto frequencyHz = track[static_cast<std::size_t> (frameIndex)];
        const auto y = frequencyHz > 0.0f ? getYForFrequency (frequencyHz, bounds) : std::nullopt;

        if (! y.has_value())
        {
            isDrawing = false;
            continue;
        }

        const auto position = numFrames > 1 ? static_cast<float> (frameIndex) / static_cast<float> (numFrames - 1)
                                            : 0.0f;
        const auto x = bounds.getX() + position * bounds.getWidth();

        if (isDrawing)
        {
            curve.lineTo (x, *y);
        }
        else
        {
            curve.startNewSubPath (x, *y);
            isDrawing = true;
        }
    }

    graphics.setColour (Palette::accent);
    graphics.strokePath (curve, juce::PathStrokeType { 1.5f, juce::PathStrokeType::curved });
}

void PitchCurveView::paintCaption (juce::Graphics& graphics, juce::Rectangle<float> bounds) const
{
    constexpr float captionWidth = 380.0f;
    constexpr float captionHeight = 76.0f;

    const auto captionBounds = bounds.withSizeKeepingCentre (juce::jmin (captionWidth, bounds.getWidth() - 24.0f),
                                                             captionHeight);

    graphics.setColour (Palette::ground);
    graphics.fillRoundedRectangle (captionBounds, Metrics::corner);
    graphics.setColour (Palette::rule);
    graphics.drawRoundedRectangle (captionBounds, Metrics::corner, Metrics::hairline);

    graphics.setColour (isCaptionAlert ? Palette::alert : Palette::dimText);
    graphics.setFont (juce::Font { juce::FontOptions { TypeScale::caption } });
    graphics.drawFittedText (caption,
                             captionBounds.reduced (14.0f, 8.0f).toNearestInt(),
                             juce::Justification::centred,
                             4);
}

void PitchCurveView::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::well);

    const auto bounds = getLocalBounds().toFloat().reduced (Metrics::hairline);
    const auto plotBounds = bounds.withTrimmedBottom (timeRulerHeight);

    paintPitchGrid (graphics, plotBounds);
    paintTimeRuler (graphics, bounds);
    paintEnvelope (graphics, plotBounds);
    paintPitch (graphics, plotBounds);
    paintOctaveNames (graphics, plotBounds);

    graphics.setColour (Palette::edge);
    graphics.drawRect (bounds, Metrics::hairline);

    if (caption.isNotEmpty())
        paintCaption (graphics, bounds);
}
}
