#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rvcara
{
/** @brief The plug-in's visual house style: one palette, one type scale, rounded surfaces. */
class PanelLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PanelLookAndFeel();

    /** @brief Colours named for the role they play, never for the colour they are.

        The values are PitchNet's, from its Theme.cpp, so the two read as the same instrument.
    */
    struct Palette
    {
        inline static const juce::Colour ground { 0xff1c1c1c };
        inline static const juce::Colour bar { 0xff161922 };
        inline static const juce::Colour card { 0xff222737 };
        inline static const juce::Colour well { 0xff0d0b0b };
        inline static const juce::Colour edge { 0xff282e40 };
        inline static const juce::Colour rule { 0xff1e2333 };
        inline static const juce::Colour text { 0xffeceff5 };
        inline static const juce::Colour dimText { 0xff8891a6 };
        inline static const juce::Colour accent { 0xff7c6aff };
        inline static const juce::Colour accentDim { 0xff3d4562 };
        inline static const juce::Colour silhouette { 0xff232940 };
        inline static const juce::Colour alert { 0xffffb626 };

        /** @brief The grid the notes sit on, and the lines ruled across it. */
        inline static const juce::Colour grid { 0xff161922 };
        inline static const juce::Colour gridRow { 0xff131620 };
        inline static const juce::Colour gridLine { 0xff0d0b0b };

        /** @brief The take as it was sung, kept quiet so the edit is the loud thing. */
        inline static const juce::Colour sungCurve { 0xff7e8899 };

        /** @brief The melody the voice actually sang, drawn over everything. */
        inline static const juce::Colour editedCurve { 0xffe6e6e6 };

        /** @brief A note's body, from its middle out, across the four steps it takes from in
                   tune to a quarter tone out: blue, purple, pink, red.
        */
        inline static const juce::Colour inTuneCentre { 0xff1983e0 };
        inline static const juce::Colour inTuneSide { 0xff0021e2 };
        inline static const juce::Colour driftingCentre { 0xffd66cff };
        inline static const juce::Colour driftingSide { 0xff971cff };
        inline static const juce::Colour offCentre { 0xffff90ed };
        inline static const juce::Colour offSide { 0xfff624b7 };
        inline static const juce::Colour wayOffCentre { 0xfff95d5d };
        inline static const juce::Colour wayOffSide { 0xffff0000 };
    };

    /** @brief The type sizes, in points. */
    struct TypeScale
    {
        static constexpr float title = 19.0f;
        static constexpr float heading = 12.0f;
        static constexpr float value = 13.5f;
        static constexpr float label = 11.0f;
        static constexpr float caption = 13.0f;
    };

    /** @brief Panel metrics, in logical pixels. */
    struct Metrics
    {
        static constexpr int headerHeight = 56;
        static constexpr int panelWidth = 236;
        static constexpr int overviewHeight = 74;
        static constexpr int controlHeight = 34;
        static constexpr int margin = 14;
        static constexpr int gap = 10;
        static constexpr float corner = 7.0f;
        static constexpr float hairline = 1.0f;
        static constexpr float tracking = 1.0f;
    };

    /** @brief Draws letter-spaced text, which JUCE has no direct support for.
        @param graphics       Where to draw.
        @param text           Drawn as given; capitalise at the call site.
        @param bounds         The text is positioned within these bounds and vertically centred.
        @param justification  Horizontal only.
        @param fontHeight     From TypeScale.
        @param tracking       Extra advance per character, in pixels.
        @param colour         Text colour.
    */
    static void drawTrackedText (juce::Graphics& graphics,
                                 const juce::String& text,
                                 juce::Rectangle<float> bounds,
                                 juce::Justification justification,
                                 float fontHeight,
                                 float tracking,
                                 juce::Colour colour);

    /** @brief Returns the width @c text would occupy at this height and tracking. */
    [[nodiscard]] static float getTrackedTextWidth (const juce::String& text,
                                                    float fontHeight,
                                                    float tracking);

    /** @brief Draws one of the panel's cards, which is what groups the controls.
        @param graphics  Where to draw.
        @param bounds    The whole card, heading included.
        @param heading   Drawn along the card's top edge, or empty for a plain card.
        @return What is left inside the card for its contents.
    */
    static juce::Rectangle<int> drawCard (juce::Graphics& graphics,
                                          juce::Rectangle<int> bounds,
                                          const juce::String& heading);

    void drawButtonBackground (juce::Graphics& graphics,
                               juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawAsHighlighted,
                               bool shouldDrawAsDown) override;

    void drawButtonText (juce::Graphics& graphics,
                         juce::TextButton& button,
                         bool shouldDrawAsHighlighted,
                         bool shouldDrawAsDown) override;

    void drawPopupMenuBackgroundWithOptions (juce::Graphics& graphics,
                                             int width,
                                             int height,
                                             const juce::PopupMenu::Options& options) override;

    juce::Font getPopupMenuFont() override;

    void drawScrollbar (juce::Graphics& graphics,
                        juce::ScrollBar& scrollBar,
                        int x,
                        int y,
                        int width,
                        int height,
                        bool isScrollbarVertical,
                        int thumbStartPosition,
                        int thumbSize,
                        bool isMouseOver,
                        bool isMouseDown) override;

    void drawLinearSlider (juce::Graphics& graphics,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPosition,
                           float minSliderPosition,
                           float maxSliderPosition,
                           juce::Slider::SliderStyle style,
                           juce::Slider& slider) override;
};

} // namespace rvcara
