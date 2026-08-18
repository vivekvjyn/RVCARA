#pragma once

#include "ara/ConversionModification.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

namespace rvcara
{
/** @brief The scrolling note grid: one row per semitone, a block per sung note, and the pitch
           curve the model followed. Its size follows the zoom, so the viewport scrolls it.
*/
class PitchTrack final : public juce::Component
{
public:
    PitchTrack();

    static constexpr int lowestNote = 36;
    static constexpr int highestNote = 84;
    static constexpr int numRows = highestNote - lowestNote + 1;

    static constexpr float minimumPixelsPerSecond = 16.0f;
    static constexpr float maximumPixelsPerSecond = 320.0f;
    static constexpr float minimumRowHeight = 3.0f;
    static constexpr float maximumRowHeight = 26.0f;

    /** @brief Shows a conversion, or clears the grid when given nullptr. */
    void setConversion (ConversionPointer conversion);

    /** @brief Sets the zoom on both axes and resizes to match.
        @param pixelsPerSecond  Horizontal scale.
        @param rowHeight        Height of one semitone row.
    */
    void setZoom (float pixelsPerSecond, float rowHeight);

    [[nodiscard]] float getPixelsPerSecond() const noexcept { return pixelsPerSecond; }
    [[nodiscard]] float getRowHeight() const noexcept { return rowHeight; }

    /** @brief Returns the size the grid wants at the current zoom, given the space available. */
    [[nodiscard]] juce::Point<int> getPreferredSize (int minimumWidth) const;

    /** @brief Returns the vertical centre of a note's row, in this component's coordinates. */
    [[nodiscard]] float getRowCentre (int midiNote) const;

    /** @brief Returns the conversion's length in seconds, or zero when there is none. */
    [[nodiscard]] double getSeconds() const;

    /** @brief Returns the note the sung range centres on, for scrolling the view to it. */
    [[nodiscard]] int getCentreNote() const;

    void paint (juce::Graphics& graphics) override;

private:
    struct Segment
    {
        int firstFrame;
        int lastFrame;
        int midiNote;
    };

    [[nodiscard]] std::optional<float> getYForFrequency (float frequencyHz) const;
    [[nodiscard]] float getXForFrame (int frameIndex) const;

    void paintRows (juce::Graphics& graphics) const;
    void paintTimeGrid (juce::Graphics& graphics) const;
    void paintSegments (juce::Graphics& graphics) const;
    void paintCurve (juce::Graphics& graphics) const;

    void rebuild();

    ConversionPointer conversion;
    std::vector<Segment> segments;

    float pixelsPerSecond { 78.0f };
    float rowHeight { 9.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchTrack)
};

} // namespace rvcara
