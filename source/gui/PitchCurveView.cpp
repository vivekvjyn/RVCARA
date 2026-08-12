#include "PitchCurveView.h"

#include <dsp/PitchConversions.h>

#include <algorithm>
#include <cmath>

namespace rvcara::gui
{

namespace
{
    /** How many envelope points to compute per pixel of width. */
    constexpr int envelopeResolution = 512;

    const juce::Colour backgroundColour { 0xff14161a };
    const juce::Colour gridColour { 0xff23262c };
    const juce::Colour octaveGridColour { 0xff313640 };
    const juce::Colour envelopeColour { 0xff2c3a4a };
    const juce::Colour pitchColour { 0xff5fc9a0 };
    const juce::Colour textColour { 0xff7d8590 };

    /** @returns A note name like "A3" for a frequency, using A4 = 440 Hz. */
    juce::String describeNote (double frequencyHz)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        const auto midiNote = static_cast<int> (std::lround (69.0 + 12.0 * std::log2 (frequencyHz / 440.0)));
        const auto octave = midiNote / 12 - 1;

        return juce::String (names[static_cast<std::size_t> (midiNote % 12)]) + juce::String (octave);
    }
} // namespace

PitchCurveView::PitchCurveView()
{
    setOpaque (true);
}

void PitchCurveView::setConversion (ara::ConversionPointer newConversion)
{
    conversion = std::move (newConversion);
    rebuildEnvelope();
    repaint();
}

void PitchCurveView::setRenderState (ara::ConversionModification::State state,
                                     float progress,
                                     const juce::String& message)
{
    if (renderState == state && juce::approximatelyEqual (renderProgress, progress) && statusMessage == message)
        return;

    renderState = state;
    renderProgress = progress;
    statusMessage = message;
    repaint();
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

    // Logarithmic, so a semitone is the same distance everywhere on the axis.
    const auto span = std::log2 (highestDisplayedHz / lowestDisplayedHz);
    const auto position = std::log2 (frequencyHz / lowestDisplayedHz) / span;

    return bounds.getBottom() - position * bounds.getHeight();
}

void PitchCurveView::paintGrid (juce::Graphics& graphics, juce::Rectangle<float> bounds) const
{
    graphics.setFont (juce::FontOptions { 11.0f });

    // One line per semitone from C2 up, with octaves picked out and named.
    for (int midiNote = 36; midiNote <= 84; ++midiNote)
    {
        const auto frequencyHz = static_cast<float> (440.0 * std::exp2 ((midiNote - 69) / 12.0));
        const auto y = getYForFrequency (frequencyHz, bounds);

        if (! y.has_value())
            continue;

        const auto isOctave = midiNote % 12 == 0;

        graphics.setColour (isOctave ? octaveGridColour : gridColour);
        graphics.drawHorizontalLine (juce::roundToInt (*y), bounds.getX(), bounds.getRight());

        if (isOctave)
        {
            graphics.setColour (textColour);
            graphics.drawText (describeNote (frequencyHz),
                               juce::Rectangle<float> { bounds.getX() + 4.0f, *y - 14.0f, 32.0f, 13.0f },
                               juce::Justification::left);
        }
    }
}

void PitchCurveView::paintEnvelope (juce::Graphics& graphics, juce::Rectangle<float> bounds) const
{
    if (envelope.empty())
        return;

    // A silhouette rather than a waveform: the pitch curve is the subject here, and a
    // full waveform would compete with it for attention.
    juce::Path silhouette;
    const auto centreY = bounds.getCentreY();
    const auto halfHeight = bounds.getHeight() * 0.5f;

    silhouette.startNewSubPath (bounds.getX(), centreY);

    for (std::size_t pointIndex = 0; pointIndex < envelope.size(); ++pointIndex)
    {
        const auto position = static_cast<float> (pointIndex) / static_cast<float> (envelope.size() - 1);
        const auto x = bounds.getX() + position * bounds.getWidth();
        silhouette.lineTo (x, centreY - envelope[pointIndex] * halfHeight);
    }

    for (auto pointIndex = static_cast<int> (envelope.size()) - 1; pointIndex >= 0; --pointIndex)
    {
        const auto position = static_cast<float> (pointIndex) / static_cast<float> (envelope.size() - 1);
        const auto x = bounds.getX() + position * bounds.getWidth();
        silhouette.lineTo (x, centreY + envelope[static_cast<std::size_t> (pointIndex)] * halfHeight);
    }

    silhouette.closeSubPath();

    graphics.setColour (envelopeColour);
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
            // Break the line rather than joining across a gap, so an unvoiced passage does
            // not read as a glissando.
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

    graphics.setColour (pitchColour);
    graphics.strokePath (curve, juce::PathStrokeType { 1.6f });
}

void PitchCurveView::paint (juce::Graphics& graphics)
{
    graphics.fillAll (backgroundColour);

    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    paintGrid (graphics, bounds);
    paintEnvelope (graphics, bounds);
    paintPitch (graphics, bounds);

    graphics.setColour (gridColour);
    graphics.drawRect (getLocalBounds(), 1);

    using State = ara::ConversionModification::State;

    if (renderState == State::rendering || renderState == State::queued)
    {
        // A progress bar drawn over the previous render, which stays visible: the user can
        // see what they are currently hearing while the replacement is prepared.
        const auto barBounds = bounds.removeFromBottom (4.0f);

        graphics.setColour (gridColour);
        graphics.fillRect (barBounds);

        graphics.setColour (pitchColour);
        graphics.fillRect (barBounds.withWidth (barBounds.getWidth() * renderProgress));
    }

    if (statusMessage.isNotEmpty())
    {
        graphics.setColour (backgroundColour.withAlpha (0.85f));
        const auto textBounds = bounds.withSizeKeepingCentre (bounds.getWidth() - 20.0f, 44.0f);
        graphics.fillRoundedRectangle (textBounds, 4.0f);

        graphics.setColour (textColour);
        graphics.setFont (juce::FontOptions { 13.0f });
        graphics.drawFittedText (statusMessage, textBounds.toNearestInt().reduced (8, 4),
                                 juce::Justification::centred, 2);
    }
}

} // namespace rvcara::gui
