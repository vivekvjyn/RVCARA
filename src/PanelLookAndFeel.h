#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rvcara
{

/** The plug-in's visual house style: one palette, one type scale, flat controls.

    Gathered into a look and feel rather than scattered through the editor's paint methods
    because a house style has to be the same everywhere to read as deliberate, and because a
    colour used in three files will eventually be three colours.

    The idiom is the one every serious instrument plug-in has converged on, and which
    hardware got to first: a dark neutral ground so that the display is the brightest thing on
    the panel, a single accent used only for the signal and for the control that is engaged,
    hairline rules instead of bevels, and letter-spaced capitals for anything that labels
    rather than states. Nothing here is decorative. A gradient, a shadow or a second accent
    would each be one more thing competing with the pitch curve, which is the only part of
    this interface carrying information the user cannot get by listening.
*/
class PanelLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PanelLookAndFeel();

    /** Colours named for the role they play, never for the colour they are.

        A role survives a change of palette; `darkGrey` becomes a lie the first time the
        panel is lightened. This is the same reason a stylesheet names `--surface` rather
        than `--grey-800`.
    */
    struct Palette
    {
        /** Behind everything, and the darkest tone on the panel. */
        inline static const juce::Colour ground { 0xff0e1013 };

        /** The header and footer strips, one step up from the ground. */
        inline static const juce::Colour bar { 0xff181b21 };

        /** The display well, one step *down*, so the curve inside it reads as lit. */
        inline static const juce::Colour well { 0xff0a0c0f };

        /** Hairline rules and control outlines. */
        inline static const juce::Colour edge { 0xff272c34 };

        /** A rule that separates two things of equal weight, fainter than an edge. */
        inline static const juce::Colour rule { 0xff1e222a };

        /** Anything the user reads for content. */
        inline static const juce::Colour text { 0xffe3e6ec };

        /** Labels, units, and values that are context rather than content. */
        inline static const juce::Colour dimText { 0xff79808d };

        /** The signal, and any control that is currently engaged. Used sparingly: if two
            things on the panel are accented, neither is.
        */
        inline static const juce::Colour accent { 0xffd8a24a };

        /** The conversion's loudness silhouette, behind the pitch curve. */
        inline static const juce::Colour silhouette { 0xff222c37 };

        /** A failed render, and nothing else. */
        inline static const juce::Colour alert { 0xffc6564a };
    };

    /** The type scale. Four sizes, because a fifth would stop being a hierarchy. */
    struct TypeScale
    {
        static constexpr float title = 16.0f;    ///< The product name, once
        static constexpr float value = 12.5f;    ///< Voice name, status, readouts
        static constexpr float label = 10.0f;    ///< Letter-spaced capitals
        static constexpr float caption = 11.5f;  ///< Explanatory text in the display
    };

    /** Panel metrics, in logical pixels. */
    struct Metrics
    {
        static constexpr int headerHeight = 40;
        static constexpr int footerHeight = 30;
        static constexpr int margin = 10;
        static constexpr int gap = 8;
        static constexpr float hairline = 1.0f;
        static constexpr float corner = 2.0f;   ///< Barely rounded; square reads as cheap, round as toy
        static constexpr float tracking = 1.4f; ///< Letter spacing for capitals
    };

    /** Draws letter-spaced text, which JUCE has no direct support for.

        Tracking is what makes a short uppercase label read as an engraved legend rather than
        as shouting, and it is the one typographic detail that cannot be faked with size and
        colour alone.

        @param graphics       Where to draw.
        @param text           Drawn as given; capitalise at the call site so the caller can
                              see what will appear.
        @param bounds         The text is positioned within these bounds.
        @param justification  Horizontal only; the text is always vertically centred.
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

    /** @returns The width @c text would occupy at this height and tracking. */
    [[nodiscard]] static float getTrackedTextWidth (const juce::String& text,
                                                    float fontHeight,
                                                    float tracking);

    /** Draws a hairline rule across the bottom of @c bounds. */
    static void drawRuleUnder (juce::Graphics& graphics, juce::Rectangle<int> bounds, juce::Colour colour);

    // ==============================================================================
    // LookAndFeel_V4

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

} // namespace rvcara
