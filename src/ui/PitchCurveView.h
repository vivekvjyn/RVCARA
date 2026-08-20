#pragma once

#include "ui/PitchTrack.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace rvcara
{
/** @brief The piano roll: a column of note names and a time ruler pinned around a scrolling
           grid, with a zoom rail down the right and along the bottom.

    The gutter and the ruler are painted rather than made components of their own, offset by the
    viewport's scroll position, so each stays pinned to the axis it labels.
*/
class PitchCurveView final : public juce::Component
{
public:
    PitchCurveView();

    /** @brief Shows a conversion, or clears the roll when given nullptr. */
    void setConversion (ConversionPointer conversion);

    /** @brief Shows a note list, unless the user is in the middle of changing it. */
    void setPitchEdit (PitchEdit edit);

    /** @brief Turns the editing tools on, which only an ARA host can support. */
    void setEditingEnabled (bool isEnabled);

    /** @brief Shows where the host's transport is, or hides the line when given a time before zero. */
    void setPlayheadSeconds (double seconds);

    /** @brief Shows a line or two of text over the roll.
        @param caption  What to say, or empty to show the roll alone.
        @param isAlert  True when the caption is a failure, which colours it.
    */
    void setCaption (const juce::String& caption, bool isAlert);

    /** @brief The grid itself, so the panel's controls can drive it. */
    [[nodiscard]] PitchTrack& getTrack() noexcept { return track; }

    /** @brief Called with the new note list whenever the user finishes changing it. */
    std::function<void (const PitchEdit&)> onEditChanged;

    /** @brief Called when the selection changes, so the panel can follow it. */
    std::function<void()> onSelectionChanged;

    /** @brief Fits the whole take across the roll and centres it on the sung range. */
    void zoomToFit();

    void paint (juce::Graphics& graphics) override;
    void paintOverChildren (juce::Graphics& graphics) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    /** @brief A viewport that reports scrolling, so the pinned lanes can follow it. */
    struct ScrollReportingViewport final : public juce::Viewport
    {
        std::function<void()> onScroll;

        void visibleAreaChanged (const juce::Rectangle<int>&) override
        {
            if (onScroll != nullptr)
                onScroll();
        }
    };

    static constexpr int gutterWidth = 54;
    static constexpr int rulerHeight = 26;
    static constexpr int zoomRailWidth = 30;
    static constexpr int zoomRailHeight = 26;
    static constexpr int scrollBarThickness = 12;

    void applyZoom();

    /** @brief Zooms while keeping one point of the grid under the mouse.
        @param factor  What to multiply each scale by, time in x and pitch in y.
        @param anchor  The point to keep still, in the grid's coordinates.
    */
    void applyZoomAround (juce::Point<float> factor, juce::Point<float> anchor);

    void scrollToContent();

    void paintGutter (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintRuler (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintCaption (juce::Graphics& graphics) const;

    ScrollReportingViewport viewport;
    PitchTrack track;

    juce::Slider verticalZoom { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Slider horizontalZoom { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextButton fitButton { "Fit" };

    ConversionPointer conversion;

    juce::Rectangle<int> gutterBounds;
    juce::Rectangle<int> rulerBounds;

    double playheadSeconds { -1.0 };

    juce::String caption;
    bool isCaptionAlert { false };
    bool isEditingEnabled { true };
    bool hasScrolledToContent { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchCurveView)
};

} // namespace rvcara
