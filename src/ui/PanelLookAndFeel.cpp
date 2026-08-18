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
} // namespace

PanelLookAndFeel::PanelLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::ground);
    setColour (juce::PopupMenu::backgroundColourId, Palette::bar);
    setColour (juce::PopupMenu::textColourId, Palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, Palette::text);
    setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::textColourOffId, Palette::text);
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

    auto x = bounds.getX();

    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x = bounds.getCentreX() - extent.getWidth() * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x = bounds.getRight() - extent.getWidth();

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

void PanelLookAndFeel::drawButtonBackground (juce::Graphics& graphics,
                                             juce::Button& button,
                                             const juce::Colour&,
                                             bool shouldDrawAsHighlighted,
                                             bool shouldDrawAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);

    graphics.setColour (shouldDrawAsDown        ? Palette::accent.withAlpha (0.22f)
                        : shouldDrawAsHighlighted ? Palette::edge
                                                  : Palette::well);
    graphics.fillRect (bounds);

    graphics.setColour (shouldDrawAsHighlighted || shouldDrawAsDown ? Palette::accent : Palette::edge);
    graphics.drawRect (bounds, Metrics::hairline);
}

void PanelLookAndFeel::drawButtonText (juce::Graphics& graphics,
                                       juce::TextButton& button,
                                       bool,
                                       bool)
{
    drawTrackedText (graphics,
                     button.getButtonText().toUpperCase(),
                     button.getLocalBounds().toFloat(),
                     juce::Justification::centred,
                     TypeScale::label,
                     Metrics::tracking,
                     button.isEnabled() ? Palette::text : Palette::dimText);
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

void PanelLookAndFeel::drawScrollbar (juce::Graphics& graphics,
                                      juce::ScrollBar&,
                                      int x,
                                      int y,
                                      int width,
                                      int height,
                                      bool isScrollbarVertical,
                                      int thumbStartPosition,
                                      int thumbSize,
                                      bool isMouseOver,
                                      bool isMouseDown)
{
    graphics.setColour (Palette::well);
    graphics.fillRect (x, y, width, height);

    if (thumbSize <= 0)
        return;

    const auto thumb = isScrollbarVertical
                           ? juce::Rectangle<int> { x + 2, thumbStartPosition, width - 4, thumbSize }
                           : juce::Rectangle<int> { thumbStartPosition, y + 2, thumbSize, height - 4 };

    graphics.setColour (isMouseDown ? Palette::accent
                        : isMouseOver ? Palette::edge.brighter (0.6f)
                                      : Palette::edge.brighter (0.3f));
    graphics.fillRect (thumb);
}

void PanelLookAndFeel::drawLinearSlider (juce::Graphics& graphics,
                                         int x,
                                         int y,
                                         int width,
                                         int height,
                                         float sliderPosition,
                                         float,
                                         float,
                                         juce::Slider::SliderStyle,
                                         juce::Slider& slider)
{
    const juce::Rectangle<float> bounds { static_cast<float> (x),
                                          static_cast<float> (y),
                                          static_cast<float> (width),
                                          static_cast<float> (height) };

    const auto track = bounds.withSizeKeepingCentre (bounds.getWidth(), 2.0f);

    graphics.setColour (Palette::edge);
    graphics.fillRect (track);

    graphics.setColour (Palette::accent.withAlpha (0.7f));
    graphics.fillRect (track.withRight (sliderPosition));

    const juce::Rectangle<float> thumb { sliderPosition - 3.0f, bounds.getY() + 2.0f, 6.0f, bounds.getHeight() - 4.0f };

    graphics.setColour (slider.isMouseOverOrDragging() ? Palette::accent : Palette::dimText);
    graphics.fillRect (thumb);
}

} // namespace rvcara
