#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rvcara
{
/** @brief The plug-in's visual house style: one palette, one type scale, flat surfaces. */
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
        inline static const juce::Colour ground { 0xff17100f };
        inline static const juce::Colour bar { 0xff211714 };
        inline static const juce::Colour well { 0xff0f0a09 };
        inline static const juce::Colour edge { 0xff402c28 };
        inline static const juce::Colour rule { 0xff281c19 };
        inline static const juce::Colour text { 0xfff4ebec };
        inline static const juce::Colour dimText { 0xff9c8781 };
        inline static const juce::Colour accent { 0xffff4d5e };
        inline static const juce::Colour accentDim { 0xffb03040 };
        inline static const juce::Colour noteBlock { 0xff4e2020 };
        inline static const juce::Colour silhouette { 0xff3c2622 };
        inline static const juce::Colour whiteKey { 0xffe4d6d2 };
        inline static const juce::Colour blackKey { 0xff211714 };
        inline static const juce::Colour blackKeyRow { 0xff0b0706 };
        inline static const juce::Colour alert { 0xffffb454 };

        /** @brief The take as it was sung, kept neutral so the edit is the coloured thing. */
        inline static const juce::Colour sungCurve { 0xff8d7b76 };
    };

    /** @brief The four type sizes, in points. */
    struct TypeScale
    {
        static constexpr float title = 16.0f;
        static constexpr float value = 12.5f;
        static constexpr float label = 10.0f;
        static constexpr float caption = 11.5f;
    };

    /** @brief Panel metrics, in logical pixels. */
    struct Metrics
    {
        static constexpr int headerHeight = 42;
        static constexpr int footerHeight = 26;
        static constexpr int margin = 10;
        static constexpr int gap = 8;
        static constexpr float hairline = 1.0f;
        static constexpr float tracking = 1.4f;
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
