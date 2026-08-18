#include "ui/PitchTrack.h"

#include "ui/PanelLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace rvcara
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using Metrics = PanelLookAndFeel::Metrics;

    constexpr int shortestSegmentInFrames = 3;

    bool isBlackKey (int midiNote)
    {
        static const bool black[] = { false, true, false, true, false, false, true, false, true, false, true, false };
        return black[static_cast<std::size_t> (((midiNote % 12) + 12) % 12)];
    }

    float toMidiNote (float frequencyHz)
    {
        return 69.0f + 12.0f * std::log2 (frequencyHz / 440.0f);
    }
} // namespace

PitchTrack::PitchTrack()
{
    setOpaque (true);
}

void PitchTrack::setConversion (ConversionPointer newConversion)
{
    if (conversion == newConversion)
        return;

    conversion = std::move (newConversion);
    rebuild();
    repaint();
}

void PitchTrack::setZoom (float newPixelsPerSecond, float newRowHeight)
{
    pixelsPerSecond = juce::jlimit (minimumPixelsPerSecond, maximumPixelsPerSecond, newPixelsPerSecond);
    rowHeight = juce::jlimit (minimumRowHeight, maximumRowHeight, newRowHeight);
    repaint();
}

double PitchTrack::getSeconds() const
{
    if (conversion == nullptr || conversion->pitchFrameRate <= 0.0)
        return 0.0;

    return static_cast<double> (conversion->fundamentalFrequencyHz.size()) / conversion->pitchFrameRate;
}

int PitchTrack::getCentreNote() const
{
    if (segments.empty())
        return (lowestNote + highestNote) / 2;

    auto lowest = highestNote;
    auto highest = lowestNote;

    for (const auto& segment : segments)
    {
        lowest = std::min (lowest, segment.midiNote);
        highest = std::max (highest, segment.midiNote);
    }

    return (lowest + highest) / 2;
}

juce::Point<int> PitchTrack::getPreferredSize (int minimumWidth) const
{
    return { std::max (juce::roundToInt (getSeconds() * static_cast<double> (pixelsPerSecond)), minimumWidth),
             juce::roundToInt (rowHeight * static_cast<float> (numRows)) };
}

float PitchTrack::getRowCentre (int midiNote) const
{
    return static_cast<float> (getHeight()) - (static_cast<float> (midiNote - lowestNote) + 0.5f) * rowHeight;
}

std::optional<float> PitchTrack::getYForFrequency (float frequencyHz) const
{
    if (frequencyHz <= 0.0f)
        return std::nullopt;

    const auto midiNote = toMidiNote (frequencyHz);

    if (midiNote < static_cast<float> (lowestNote) - 0.5f || midiNote > static_cast<float> (highestNote) + 0.5f)
        return std::nullopt;

    return static_cast<float> (getHeight()) - (midiNote - static_cast<float> (lowestNote) + 0.5f) * rowHeight;
}

float PitchTrack::getXForFrame (int frameIndex) const
{
    if (conversion == nullptr || conversion->pitchFrameRate <= 0.0)
        return 0.0f;

    return static_cast<float> (static_cast<double> (frameIndex) / conversion->pitchFrameRate
                               * static_cast<double> (pixelsPerSecond));
}

void PitchTrack::rebuild()
{
    segments.clear();

    if (conversion == nullptr)
        return;

    const auto& track = conversion->fundamentalFrequencyHz;
    auto firstFrame = -1;
    auto sumOfNotes = 0.0f;

    for (int frameIndex = 0; frameIndex <= static_cast<int> (track.size()); ++frameIndex)
    {
        const auto isVoiced = frameIndex < static_cast<int> (track.size())
                           && track[static_cast<std::size_t> (frameIndex)] > 0.0f;

        if (isVoiced)
        {
            if (firstFrame < 0)
            {
                firstFrame = frameIndex;
                sumOfNotes = 0.0f;
            }

            sumOfNotes += toMidiNote (track[static_cast<std::size_t> (frameIndex)]);
            continue;
        }

        if (firstFrame >= 0)
        {
            const auto numFrames = frameIndex - firstFrame;

            if (numFrames >= shortestSegmentInFrames)
                segments.push_back ({ firstFrame,
                                      frameIndex - 1,
                                      juce::roundToInt (sumOfNotes / static_cast<float> (numFrames)) });

            firstFrame = -1;
        }
    }
}

void PitchTrack::paintRows (juce::Graphics& graphics) const
{
    const auto width = static_cast<float> (getWidth());

    for (int midiNote = lowestNote; midiNote <= highestNote; ++midiNote)
    {
        const auto top = getRowCentre (midiNote) - rowHeight * 0.5f;

        if (isBlackKey (midiNote))
        {
            graphics.setColour (Palette::blackKeyRow);
            graphics.fillRect (0.0f, top, width, rowHeight);
        }

        graphics.setColour (midiNote % 12 == 0 ? Palette::edge : Palette::rule);
        graphics.fillRect (0.0f, top + rowHeight - Metrics::hairline, width, Metrics::hairline);
    }
}

void PitchTrack::paintTimeGrid (juce::Graphics& graphics) const
{
    const auto totalSeconds = getSeconds();
    const auto height = static_cast<float> (getHeight());
    const auto secondsPerLine = pixelsPerSecond < 40.0f ? 5.0 : 1.0;

    for (auto second = 0.0; second <= totalSeconds; second += secondsPerLine)
    {
        const auto x = static_cast<float> (second * static_cast<double> (pixelsPerSecond));

        graphics.setColour (std::fmod (second, 5.0) < 0.5 ? Palette::edge : Palette::rule);
        graphics.fillRect (x, 0.0f, Metrics::hairline, height);
    }
}

void PitchTrack::paintSegments (juce::Graphics& graphics) const
{
    for (const auto& segment : segments)
    {
        const auto left = getXForFrame (segment.firstFrame);
        const auto right = getXForFrame (segment.lastFrame + 1);
        const auto centre = getRowCentre (segment.midiNote);

        const juce::Rectangle<float> block { left,
                                             centre - rowHeight * 0.42f,
                                             std::max (right - left, 2.0f),
                                             rowHeight * 0.84f };

        graphics.setColour (Palette::noteBlock);
        graphics.fillRect (block);

        graphics.setColour (Palette::accent.withAlpha (0.75f));
        graphics.drawRect (block, Metrics::hairline);
    }
}

void PitchTrack::paintCurve (juce::Graphics& graphics) const
{
    if (conversion == nullptr || conversion->fundamentalFrequencyHz.empty())
        return;

    const auto& track = conversion->fundamentalFrequencyHz;
    juce::Path curve;
    auto isDrawing = false;

    for (int frameIndex = 0; frameIndex < static_cast<int> (track.size()); ++frameIndex)
    {
        const auto y = getYForFrequency (track[static_cast<std::size_t> (frameIndex)]);

        if (! y.has_value())
        {
            isDrawing = false;
            continue;
        }

        const auto x = getXForFrame (frameIndex);

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
    graphics.strokePath (curve, juce::PathStrokeType { 1.6f, juce::PathStrokeType::curved });
}

void PitchTrack::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::well);

    paintRows (graphics);
    paintTimeGrid (graphics);
    paintSegments (graphics);
    paintCurve (graphics);
}

} // namespace rvcara
