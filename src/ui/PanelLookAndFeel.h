#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rvcara
{
/** @brief The plug-in's visual house style: one palette, one type scale, flat controls. */
class PanelLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PanelLookAndFeel();

    /** @brief Colours named for the role they play, never for the colour they are. */
    struct Palette
    {
        inline static const juce::Colour ground { 0xff0e1013 };

        inline static const juce::Colour bar { 0xff181b21 };

        inline static const juce::Colour well { 0xff0a0c0f };

        inline static const juce::Colour edge { 0xff272c34 };

        inline static const juce::Colour rule { 0xff1e222a };

        inline static const juce::Colour text { 0xffe3e6ec };

        inline static const juce::Colour dimText { 0xff79808d };

        inline static const juce::Colour accent { 0xffd8a24a };

        inline static const juce::Colour silhouette { 0xff222c37 };

        inline static const juce::Colour alert { 0xffc6564a };
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
        static constexpr int headerHeight = 40;
        static constexpr int footerHeight = 30;
        static constexpr int margin = 10;
        static constexpr int gap = 8;
        static constexpr float hairline = 1.0f;
        static constexpr float corner = 2.0f;
        static constexpr float tracking = 1.4f;
    };

    static void drawTrackedText (juce::Graphics& graphics,
                                 const juce::String& text,
                                 juce::Rectangle<float> bounds,
                                 juce::Justification justification,
                                 float fontHeight,
                                 float tracking,
                                 juce::Colour colour);

    [[nodiscard]] static float getTrackedTextWidth (const juce::String& text,
                                                    float fontHeight,
                                                    float tracking);

    static void drawRuleUnder (juce::Graphics& graphics, juce::Rectangle<int> bounds, juce::Colour colour);

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
};
}
