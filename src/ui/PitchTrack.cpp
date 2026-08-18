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

    constexpr float voiceThickness = 2.6f;

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
    if (conversion == nullptr)
        return (lowestNote + highestNote) / 2;

    auto lowest = highestNote;
    auto highest = lowestNote;
    auto isVoiced = false;

    for (const auto frequencyHz : conversion->fundamentalFrequencyHz)
    {
        if (frequencyHz <= 0.0f)
            continue;

        const auto midiNote = juce::jlimit (lowestNote, highestNote,
                                            juce::roundToInt (toMidiNote (frequencyHz)));
        lowest = std::min (lowest, midiNote);
        highest = std::max (highest, midiNote);
        isVoiced = true;
    }

    return isVoiced ? (lowest + highest) / 2 : (lowestNote + highestNote) / 2;
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
    amplitude.clear();

    if (conversion == nullptr || conversion->samples.empty() || conversion->pitchFrameRate <= 0.0)
        return;

    const auto numFrames = static_cast<int> (conversion->fundamentalFrequencyHz.size());
    const auto samplesPerFrame = conversion->sampleRate > 0.0
                               ? conversion->sampleRate / conversion->pitchFrameRate
                               : static_cast<double> (conversion->samples.size()) / std::max (numFrames, 1);

    amplitude.resize (static_cast<std::size_t> (numFrames), 0.0f);

    const auto numSamples = static_cast<int> (conversion->samples.size());
    auto loudest = 0.0f;

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        const auto first = static_cast<int> (static_cast<double> (frameIndex) * samplesPerFrame);
        const auto last = std::min (static_cast<int> (static_cast<double> (frameIndex + 1) * samplesPerFrame),
                                    numSamples);

        auto peak = 0.0f;

        for (int sampleIndex = std::max (first, 0); sampleIndex < last; ++sampleIndex)
            peak = std::max (peak, std::abs (conversion->samples[static_cast<std::size_t> (sampleIndex)]));

        amplitude[static_cast<std::size_t> (frameIndex)] = peak;
        loudest = std::max (loudest, peak);
    }

    if (loudest <= 0.0f)
        return;

    for (auto& peak : amplitude)
        peak /= loudest;
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

void PitchTrack::paintVoice (juce::Graphics& graphics) const
{
    if (conversion == nullptr || amplitude.empty())
        return;

    const auto& track = conversion->fundamentalFrequencyHz;
    const auto numFrames = std::min (static_cast<int> (track.size()), static_cast<int> (amplitude.size()));
    const auto halfHeight = rowHeight * voiceThickness * 0.5f;

    juce::Path ribbon;
    std::vector<juce::Point<float>> upper;
    std::vector<juce::Point<float>> lower;

    const auto flush = [&]
    {
        if (upper.size() < 2)
        {
            upper.clear();
            lower.clear();
            return;
        }

        ribbon.clear();
        ribbon.startNewSubPath (upper.front());

        for (std::size_t index = 1; index < upper.size(); ++index)
            ribbon.lineTo (upper[index]);

        for (auto point = lower.rbegin(); point != lower.rend(); ++point)
            ribbon.lineTo (*point);

        ribbon.closeSubPath();

        graphics.setColour (Palette::noteBlock);
        graphics.fillPath (ribbon);

        upper.clear();
        lower.clear();
    };

    for (int frameIndex = 0; frameIndex < numFrames; ++frameIndex)
    {
        const auto y = getYForFrequency (track[static_cast<std::size_t> (frameIndex)]);

        if (! y.has_value())
        {
            flush();
            continue;
        }

        const auto x = getXForFrame (frameIndex);
        const auto reach = std::max (amplitude[static_cast<std::size_t> (frameIndex)] * halfHeight, 0.5f);

        upper.push_back ({ x, *y - reach });
        lower.push_back ({ x, *y + reach });
    }

    flush();
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
    paintVoice (graphics);
    paintCurve (graphics);
}

} // namespace rvcara
