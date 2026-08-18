#pragma once

#include "ui/PitchTrack.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace rvcara
{
/** @brief The pitch editor: a piano keyboard and a time ruler pinned around a scrolling note
           grid, with a waveform lane beneath it and a zoom scaler for each axis.

    The keyboard, ruler and waveform are painted rather than made components of their own,
    offset by the viewport's scroll position: the keyboard follows the vertical scroll, the
    ruler and waveform the horizontal one, so each stays pinned on the axis it labels.
*/
class PitchCurveView final : public juce::Component
{
public:
    PitchCurveView();

    /** @brief Shows a conversion, or clears the editor when given nullptr. */
    void setConversion (ConversionPointer conversion);

    /** @brief Shows a line or two of text over the editor.
        @param caption  What to say, or empty to show the editor alone.
        @param isAlert  True when the caption is a failure, which colours it.
    */
    void setCaption (const juce::String& caption, bool isAlert);

    void paint (juce::Graphics& graphics) override;
    void paintOverChildren (juce::Graphics& graphics) override;
    void resized() override;

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

    static constexpr int keyboardWidth = 38;
    static constexpr int rulerHeight = 18;
    static constexpr int waveformHeight = 42;
    static constexpr int scalerHeight = 16;
    static constexpr int scrollBarThickness = 10;
    static constexpr int waveformResolution = 4096;

    void applyZoom();
    void rebuildWaveform();
    void scrollToContent();

    void paintKeyboard (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintRuler (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintWaveform (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintCaption (juce::Graphics& graphics) const;

    ScrollReportingViewport viewport;
    PitchTrack track;

    juce::Slider horizontalScaler { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider verticalScaler { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    ConversionPointer conversion;
    std::vector<float> waveform;

    juce::Rectangle<int> timeLabelBounds;
    juce::Rectangle<int> pitchLabelBounds;

    juce::String caption;
    bool isCaptionAlert { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchCurveView)
};

} // namespace rvcara
