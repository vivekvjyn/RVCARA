#include "ui/OverviewStrip.h"

#include "dsp/PitchConversions.h"
#include "ui/PanelLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace rvcara
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using Metrics = PanelLookAndFeel::Metrics;

    constexpr float noteMarkHeight = 3.0f;
} // namespace

OverviewStrip::OverviewStrip()
{
    setOpaque (true);
}

void OverviewStrip::setConversion (ConversionPointer newConversion)
{
    if (conversion == newConversion)
        return;

    conversion = std::move (newConversion);
    rebuild();
    repaint();
}

void OverviewStrip::setPitchEdit (PitchEdit newEdit)
{
    if (edit == newEdit)
        return;

    edit = std::move (newEdit);
    repaint();
}

void OverviewStrip::setVisibleRange (double startSeconds, double endSeconds)
{
    if (juce::approximatelyEqual (visibleStart, startSeconds)
        && juce::approximatelyEqual (visibleEnd, endSeconds))
        return;

    visibleStart = startSeconds;
    visibleEnd = endSeconds;
    repaint();
}

double OverviewStrip::getSeconds() const
{
    auto seconds = 0.0;

    if (conversion != nullptr && conversion->pitchFrameRate > 0.0)
        seconds = static_cast<double> (conversion->fundamentalFrequencyHz.size())
                / conversion->pitchFrameRate;

    for (const auto& note : edit.notes)
        seconds = std::max (seconds, note.endSeconds);

    return seconds;
}

float OverviewStrip::getXForSeconds (double seconds) const
{
    const auto total = getSeconds();
    const auto lane = getLocalBounds().reduced (Metrics::gap, Metrics::gap / 2).toFloat();

    if (total <= 0.0)
        return lane.getX();

    return lane.getX() + static_cast<float> (seconds / total) * lane.getWidth();
}

void OverviewStrip::rebuild()
{
    envelope.clear();

    if (conversion == nullptr || conversion->samples.empty())
        return;

    const auto numSamples = static_cast<int> (conversion->samples.size());
    const auto numPoints = std::min (resolution, numSamples);
    const auto samplesPerPoint = std::max (numSamples / numPoints, 1);

    envelope.resize (static_cast<std::size_t> (numPoints), 0.0f);
    auto loudest = 0.0f;

    for (int pointIndex = 0; pointIndex < numPoints; ++pointIndex)
    {
        const auto first = pointIndex * samplesPerPoint;
        const auto last = std::min (first + samplesPerPoint, numSamples);

        auto peak = 0.0f;

        for (int sampleIndex = first; sampleIndex < last; ++sampleIndex)
            peak = std::max (peak, std::abs (conversion->samples[static_cast<std::size_t> (sampleIndex)]));

        envelope[static_cast<std::size_t> (pointIndex)] = peak;
        loudest = std::max (loudest, peak);
    }

    if (loudest <= 0.0f)
        return;

    for (auto& peak : envelope)
        peak /= loudest;
}

void OverviewStrip::scrubTo (float x)
{
    const auto total = getSeconds();

    if (total <= 0.0 || onScrubbed == nullptr || getWidth() <= 0)
        return;

    const auto lane = getLocalBounds().reduced (Metrics::gap, Metrics::gap / 2).toFloat();

    onScrubbed (juce::jlimit (0.0, total,
                              static_cast<double> ((x - lane.getX()) / lane.getWidth()) * total));
}

void OverviewStrip::mouseDown (const juce::MouseEvent& event) { scrubTo (event.position.x); }
void OverviewStrip::mouseDrag (const juce::MouseEvent& event) { scrubTo (event.position.x); }

void OverviewStrip::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::ground);

    const auto lane = getLocalBounds().reduced (Metrics::gap, Metrics::gap / 2).toFloat();

    graphics.setColour (Palette::well);
    graphics.fillRoundedRectangle (lane, Metrics::corner);

    graphics.setColour (Palette::edge);
    graphics.drawRoundedRectangle (lane.reduced (0.5f), Metrics::corner, Metrics::hairline);

    const auto bounds = lane;
    const auto centreY = bounds.getCentreY();
    const auto halfHeight = bounds.getHeight() * 0.36f;

    if (! envelope.empty())
    {
        const auto lastPoint = static_cast<float> (envelope.size() - 1);
        const auto laneWidth = juce::roundToInt (lane.getWidth());

        graphics.setColour (Palette::silhouette);

        for (int x = 0; x < laneWidth; ++x)
        {
            const auto position = static_cast<float> (x) / static_cast<float> (std::max (laneWidth - 1, 1));
            const auto peak = envelope[static_cast<std::size_t> (position * lastPoint)];

            graphics.fillRect (lane.getX() + static_cast<float> (x), centreY - peak * halfHeight,
                               1.0f, std::max (peak * halfHeight * 2.0f, 1.0f));
        }
    }

    if (! edit.notes.empty())
    {
        auto lowest = 127.0f;
        auto highest = 0.0f;

        for (const auto& note : edit.notes)
        {
            if (note.isRest)
                continue;

            lowest = std::min (lowest, note.getEditedMidiNote());
            highest = std::max (highest, note.getEditedMidiNote());
        }

        const auto span = std::max (highest - lowest, 1.0f);

        for (const auto& note : edit.notes)
        {
            if (note.isRest)
                continue;

            const auto left = getXForSeconds (note.startSeconds);
            const auto right = getXForSeconds (note.endSeconds);
            const auto position = (note.getEditedMidiNote() - lowest) / span;

            graphics.setColour (note.isNeutral() ? Palette::accentDim : Palette::accent);
            graphics.fillRect (left,
                               centreY + halfHeight - position * halfHeight * 2.0f - noteMarkHeight * 0.5f,
                               std::max (right - left, 1.5f),
                               noteMarkHeight);
        }
    }

    if (visibleEnd > visibleStart)
    {
        const auto left = getXForSeconds (visibleStart);
        const auto right = getXForSeconds (visibleEnd);

        graphics.setColour (Palette::ground.withAlpha (0.6f));
        graphics.fillRect (bounds.getX(), bounds.getY(), left - bounds.getX(), bounds.getHeight());
        graphics.fillRect (right, bounds.getY(), bounds.getRight() - right, bounds.getHeight());

        graphics.setColour (Palette::edge.brighter (0.5f));
        graphics.drawRoundedRectangle ({ left, bounds.getY() + 1.0f,
                                         std::max (right - left, 2.0f), bounds.getHeight() - 2.0f },
                                       4.0f, 1.4f);
    }

}

} // namespace rvcara
