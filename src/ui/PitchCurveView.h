#pragma once

#include "ui/PitchTrack.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace rvcara
{
/** @brief The pitch editor: a toolbar over a piano keyboard and a time ruler pinned around a
           scrolling note grid, with a waveform lane beneath it and a zoom scaler for each axis.

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

    /** @brief Shows a note list, unless the user is in the middle of changing it. */
    void setPitchEdit (PitchEdit edit);

    /** @brief Turns the editing tools on, which only an ARA host can support. */
    void setEditingEnabled (bool isEnabled);

    /** @brief Says how note detection is getting on, shown at the end of the toolbar. */
    void setNoteStatus (const juce::String& status);

    /** @brief Shows where the host's transport is, or hides the line when given a time before zero. */
    void setPlayheadSeconds (double seconds);

    /** @brief Called with the new note list whenever the user finishes changing it. */
    std::function<void (const PitchEdit&)> onEditChanged;

    /** @brief Shows a line or two of text over the editor.
        @param caption  What to say, or empty to show the editor alone.
        @param isAlert  True when the caption is a failure, which colours it.
    */
    void setCaption (const juce::String& caption, bool isAlert);

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

    static constexpr int keyboardWidth = 38;
    static constexpr int toolbarHeight = 26;
    static constexpr int rulerHeight = 18;
    static constexpr int waveformHeight = 42;
    static constexpr int scalerHeight = 16;
    static constexpr int scrollBarThickness = 10;
    static constexpr int waveformResolution = 4096;

    static constexpr int toolButtonWidth = 54;
    static constexpr int scaleButtonWidth = 96;

    void applyZoom();

    /** @brief Zooms while keeping one point of the grid under the mouse.
        @param factor  What to multiply each scale by, time in x and pitch in y.
        @param anchor  The point to keep still, in the grid's coordinates.
    */
    void applyZoomAround (juce::Point<float> factor, juce::Point<float> anchor);

    void rebuildWaveform();
    void scrollToContent();

    void showScaleMenu();
    void applyScale (int root, int mode);

    void chooseTool (PitchTrack::Tool tool);
    void applySelectionDepth();
    void updateToolbar();

    void paintToolbar (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintKeyboard (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintRuler (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintWaveform (juce::Graphics& graphics, juce::Rectangle<int> bounds) const;
    void paintCaption (juce::Graphics& graphics) const;

    ScrollReportingViewport viewport;
    PitchTrack track;

    juce::TextButton selectButton { "Select" };
    juce::TextButton splitButton { "Split" };
    juce::TextButton glueButton { "Glue" };
    juce::TextButton snapButton { "Snap" };
    juce::TextButton resetButton { "Reset" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };

    juce::TextButton scaleButton { "Chromatic" };

    juce::Slider shapeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    juce::Slider horizontalScaler { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider verticalScaler { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    ConversionPointer conversion;
    std::vector<float> waveform;

    juce::Rectangle<int> keyboardBounds;
    juce::Rectangle<int> rulerBounds;
    juce::Rectangle<int> waveformBounds;
    juce::Rectangle<int> toolbarBounds;
    juce::Rectangle<int> shapeLabelBounds;
    juce::Rectangle<int> noteStatusBounds;
    juce::Rectangle<int> timeLabelBounds;
    juce::Rectangle<int> pitchLabelBounds;

    juce::String noteStatus;
    double playheadSeconds { -1.0 };
    int scaleRoot { 0 };
    int scaleMode { 0 };
    juce::String caption;
    bool isCaptionAlert { false };
    bool isEditingEnabled { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchCurveView)
};

} // namespace rvcara
