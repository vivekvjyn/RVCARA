#include "ui/PanelLookAndFeel.h"

namespace rvcara
{
namespace
{
    juce::GlyphArrangement layOutTrackedText (const juce::String& text, float fontHeight, float tracking)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (juce::Font { juce::FontOptions { fontHeight } }, text, 0.0f, 0.0f);

        for (int glyphIndex = 0; glyphIndex < glyphs.getNumGlyphs(); ++glyphIndex)
            glyphs.moveRangeOfGlyphs (glyphIndex, 1, static_cast<float> (glyphIndex) * tracking, 0.0f);

        return glyphs;
    }
}

PanelLookAndFeel::PanelLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::ground);
    setColour (juce::PopupMenu::backgroundColourId, Palette::bar);
    setColour (juce::PopupMenu::textColourId, Palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::edge);
    setColour (juce::PopupMenu::highlightedTextColourId, Palette::text);
    setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId, Palette::dimText);
    setColour (juce::TextButton::textColourOnId, Palette::ground);
}

void PanelLookAndFeel::drawTrackedText (juce::Graphics& graphics,
                                        const juce::String& text,
                                        juce::Rectangle<float> bounds,
                                        juce::Justification justification,
                                        float fontHeight,
                                        float tracking,
                                        juce::Colour colour)
{
    if (text.isEmpty())
        return;

    auto glyphs = layOutTrackedText (text, fontHeight, tracking);
    const auto extent = glyphs.getBoundingBox (0, glyphs.getNumGlyphs(), true);

    const auto width = extent.getWidth();

    auto x = bounds.getX();

    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x = bounds.getCentreX() - width * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x = bounds.getRight() - width;

    graphics.setColour (colour);
    glyphs.draw (graphics, juce::AffineTransform::translation (x - extent.getX(), bounds.getCentreY()));
}

float PanelLookAndFeel::getTrackedTextWidth (const juce::String& text, float fontHeight, float tracking)
{
    if (text.isEmpty())
        return 0.0f;

    const auto glyphs = layOutTrackedText (text, fontHeight, tracking);
    return glyphs.getBoundingBox (0, glyphs.getNumGlyphs(), true).getWidth();
}

void PanelLookAndFeel::drawRuleUnder (juce::Graphics& graphics, juce::Rectangle<int> bounds, juce::Colour colour)
{
    graphics.setColour (colour);
    graphics.fillRect (bounds.getX(),
                       bounds.getBottom() - 1,
                       bounds.getWidth(),
                       juce::roundToInt (Metrics::hairline));
}

void PanelLookAndFeel::drawButtonBackground (juce::Graphics& graphics,
                                             juce::Button& button,
                                             const juce::Colour&,
                                             bool shouldDrawAsHighlighted,
                                             bool shouldDrawAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto isEngaged = button.getToggleState() || shouldDrawAsDown;

    if (isEngaged)
    {
        graphics.setColour (shouldDrawAsDown ? Palette::accent.darker (0.2f) : Palette::accent);
        graphics.fillRoundedRectangle (bounds, Metrics::corner);
    }
    else if (shouldDrawAsHighlighted)
    {
        graphics.setColour (Palette::edge);
        graphics.fillRoundedRectangle (bounds, Metrics::corner);
    }

    if (! isEngaged)
    {
        graphics.setColour (button.isEnabled() ? Palette::edge : Palette::rule);
        graphics.drawRoundedRectangle (bounds, Metrics::corner, Metrics::hairline);
    }
}

void PanelLookAndFeel::drawButtonText (juce::Graphics& graphics,
                                       juce::TextButton& button,
                                       bool,
                                       bool shouldDrawAsDown)
{
    const auto isEngaged = button.getToggleState() || shouldDrawAsDown;

    const auto colour = ! button.isEnabled() ? Palette::dimText.withAlpha (0.4f)
                      : isEngaged            ? Palette::ground
                                             : Palette::text;

    drawTrackedText (graphics,
                     button.getButtonText().toUpperCase(),
                     button.getLocalBounds().toFloat(),
                     juce::Justification::centred,
                     TypeScale::label,
                     Metrics::tracking,
                     colour);
}

void PanelLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& graphics,
                                                           int width,
                                                           int height,
                                                           const juce::PopupMenu::Options&)
{
    const juce::Rectangle<float> bounds { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };

    graphics.setColour (Palette::bar);
    graphics.fillRect (bounds);
    graphics.setColour (Palette::edge);
    graphics.drawRect (bounds, Metrics::hairline);
}

juce::Font PanelLookAndFeel::getPopupMenuFont()
{
    return juce::Font { juce::FontOptions { TypeScale::value } };
}
}
