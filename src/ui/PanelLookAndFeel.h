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

        The neutrals are warm rather than blue, so the accent reads as the one saturated thing
        on the panel. Alert is amber rather than red precisely because the accent is red: a
        failure has to be distinguishable from a selection at a glance.
    */
    struct Palette
    {
        inline static const juce::Colour ground { 0xff141010 };
        inline static const juce::Colour bar { 0xff1c1616 };
        inline static const juce::Colour card { 0xff231b1b };
        inline static const juce::Colour well { 0xff100c0c };
        inline static const juce::Colour edge { 0xff3a2b2b };
        inline static const juce::Colour rule { 0xff241b1b };
        inline static const juce::Colour text { 0xfff6eeed };
        inline static const juce::Colour dimText { 0xff9c8a88 };
        inline static const juce::Colour accent { 0xffff4d5e };
        inline static const juce::Colour accentDim { 0xff8f2b38 };
        inline static const juce::Colour silhouette { 0xff33262a };
        inline static const juce::Colour alert { 0xffffb454 };

        /** @brief The take as it was sung, kept neutral so the edit is the coloured thing. */
        inline static const juce::Colour sungCurve { 0xff8d7b76 };

        /** @brief The melody the voice actually sang, drawn over everything. */
        inline static const juce::Colour editedCurve { 0xfffff2f0 };

        /** @brief A note's body, from its middle out, as it drifts off the nearest degree. */
        inline static const juce::Colour inTuneCentre { 0xffff5f6d };
        inline static const juce::Colour inTuneSide { 0xffb02434 };
        inline static const juce::Colour offCentre { 0xffffb45c };
        inline static const juce::Colour offSide { 0xffc06a18 };
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
