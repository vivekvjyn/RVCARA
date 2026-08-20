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

juce::Rectangle<int> PanelLookAndFeel::drawCard (juce::Graphics& graphics,
                                                 juce::Rectangle<int> bounds,
                                                 const juce::String& heading)
{
    graphics.setColour (Palette::card);
    graphics.fillRoundedRectangle (bounds.toFloat(), Metrics::corner);

    graphics.setColour (Palette::edge);
    graphics.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), Metrics::corner, Metrics::hairline);

    auto inside = bounds.reduced (Metrics::gap + 2, Metrics::gap);

    if (heading.isNotEmpty())
        drawTrackedText (graphics,
                         heading.toUpperCase(),
                         inside.removeFromTop (16).toFloat(),
                         juce::Justification::left,
                         TypeScale::label,
                         Metrics::tracking + 0.6f,
                         Palette::dimText);

    return inside;
}

void PanelLookAndFeel::drawButtonBackground (juce::Graphics& graphics,
                                             juce::Button& button,
                                             const juce::Colour&,
                                             bool shouldDrawAsHighlighted,
                                             bool shouldDrawAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto isLatched = button.getToggleState();

    const auto fill = isLatched          ? (button.isEnabled() ? Palette::accent : Palette::accentDim)
                    : shouldDrawAsDown   ? Palette::accent.withAlpha (0.35f)
                    : shouldDrawAsHighlighted ? Palette::edge
                                              : Palette::bar;

    graphics.setColour (fill);
    graphics.fillRoundedRectangle (bounds, Metrics::corner - 1.0f);

    if (! isLatched)
    {
        graphics.setColour (shouldDrawAsHighlighted || shouldDrawAsDown ? Palette::accent : Palette::edge);
        graphics.drawRoundedRectangle (bounds, Metrics::corner - 1.0f, Metrics::hairline);
    }
}

void PanelLookAndFeel::drawButtonText (juce::Graphics& graphics,
                                       juce::TextButton& button,
                                       bool,
                                       bool)
{
    const auto colour = button.getToggleState() ? Palette::ground
                      : button.isEnabled()      ? Palette::text
                                                : Palette::dimText.withAlpha (0.55f);

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

    graphics.setColour (Palette::card);
    graphics.fillRoundedRectangle (bounds, Metrics::corner);
    graphics.setColour (Palette::edge);
    graphics.drawRoundedRectangle (bounds.reduced (0.5f), Metrics::corner, Metrics::hairline);
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
                           ? juce::Rectangle<float> { static_cast<float> (x + 3),
                                                      static_cast<float> (thumbStartPosition),
                                                      static_cast<float> (width - 6),
                                                      static_cast<float> (thumbSize) }
                           : juce::Rectangle<float> { static_cast<float> (thumbStartPosition),
                                                      static_cast<float> (y + 3),
                                                      static_cast<float> (thumbSize),
                                                      static_cast<float> (height - 6) };

    graphics.setColour (isMouseDown ? Palette::accent
                        : isMouseOver ? Palette::edge.brighter (0.6f)
                                      : Palette::edge.brighter (0.3f));
    graphics.fillRoundedRectangle (thumb, thumb.getWidth() * 0.5f);
}

void PanelLookAndFeel::drawLinearSlider (juce::Graphics& graphics,
                                         int x,
                                         int y,
                                         int width,
                                         int height,
                                         float sliderPosition,
                                         float,
                                         float,
                                         juce::Slider::SliderStyle style,
                                         juce::Slider& slider)
{
    const juce::Rectangle<float> bounds { static_cast<float> (x),
                                          static_cast<float> (y),
                                          static_cast<float> (width),
                                          static_cast<float> (height) };

    const auto isVertical = style == juce::Slider::LinearVertical
                         || style == juce::Slider::LinearBarVertical;

    const auto track = isVertical ? bounds.withSizeKeepingCentre (4.0f, bounds.getHeight())
                                  : bounds.withSizeKeepingCentre (bounds.getWidth(), 4.0f);

    graphics.setColour (Palette::well);
    graphics.fillRoundedRectangle (track, 2.0f);

    graphics.setColour (slider.isEnabled() ? Palette::accent : Palette::edge);
    graphics.fillRoundedRectangle (isVertical ? track.withTop (sliderPosition)
                                              : track.withRight (sliderPosition),
                                   2.0f);

    const auto radius = slider.isMouseOverOrDragging() ? 7.0f : 6.0f;
    const auto centre = isVertical ? juce::Point<float> { bounds.getCentreX(), sliderPosition }
                                   : juce::Point<float> { sliderPosition, bounds.getCentreY() };

    graphics.setColour (slider.isEnabled() ? Palette::text : Palette::dimText);
    graphics.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
}

} // namespace rvcara
