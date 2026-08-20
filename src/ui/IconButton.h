#pragma once

#include "ui/PanelLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace rvcara
{
/** @brief A square button carrying a drawn icon rather than a word.

    The icons are paths rather than images so they stay sharp at any scale and carry no assets.
*/
class IconButton final : public juce::Button
{
public:
    /** @brief Which icon the button carries. */
    enum class Icon
    {
        select,
        split,
        anchor,
        timing,
        play,
        pause,
        stop,
        loop,
        audition,
        magnet,
        undo,
        redo,
        panel,
        fit,
        overview
    };

    IconButton (Icon iconToDraw, const juce::String& name)
        : juce::Button (name),
          icon (iconToDraw)
    {
        setTooltip (name);
    }

    void paintButton (juce::Graphics& graphics, bool isOver, bool isDown) override
    {
        using Palette = PanelLookAndFeel::Palette;

        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const auto isLatched = getToggleState();

        if (isLatched || isDown || isOver)
        {
            graphics.setColour (isLatched ? (isEnabled() ? Palette::accent : Palette::accentDim)
                                          : Palette::edge);
            graphics.fillRoundedRectangle (bounds, PanelLookAndFeel::Metrics::corner - 1.0f);
        }

        const auto colour = isLatched      ? Palette::ground
                          : ! isEnabled()  ? Palette::dimText.withAlpha (0.45f)
                          : isOver         ? Palette::text
                                           : Palette::dimText;

        graphics.setColour (colour);
        drawIcon (graphics, bounds.reduced (bounds.getWidth() * 0.22f));
    }

private:
    void drawIcon (juce::Graphics& graphics, juce::Rectangle<float> area) const
    {
        const auto stroke = juce::PathStrokeType { juce::jmax (1.6f, area.getWidth() * 0.15f),
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded };

        juce::Path path;

        switch (icon)
        {
            case Icon::select:
                path.startNewSubPath (area.getX() + area.getWidth() * 0.18f, area.getY());
                path.lineTo (area.getX() + area.getWidth() * 0.18f, area.getBottom());
                path.lineTo (area.getX() + area.getWidth() * 0.5f, area.getBottom() - area.getHeight() * 0.3f);
                path.lineTo (area.getRight(), area.getBottom() - area.getHeight() * 0.36f);
                path.closeSubPath();
                graphics.fillPath (path);
                return;

            case Icon::split:
                path.startNewSubPath (area.getCentreX(), area.getY());
                path.lineTo (area.getCentreX(), area.getBottom());
                graphics.strokePath (path, stroke);
                graphics.fillEllipse (area.getX(), area.getBottom() - area.getHeight() * 0.26f,
                                      area.getWidth() * 0.26f, area.getHeight() * 0.26f);
                graphics.fillEllipse (area.getRight() - area.getWidth() * 0.26f,
                                      area.getBottom() - area.getHeight() * 0.26f,
                                      area.getWidth() * 0.26f, area.getHeight() * 0.26f);
                return;

            case Icon::anchor:
                path.startNewSubPath (area.getX(), area.getBottom());
                path.quadraticTo (area.getCentreX(), area.getY() - area.getHeight() * 0.2f,
                                  area.getRight(), area.getCentreY());
                graphics.strokePath (path, stroke);
                graphics.fillEllipse (area.getCentreX() - area.getWidth() * 0.16f,
                                      area.getCentreY() - area.getHeight() * 0.34f,
                                      area.getWidth() * 0.32f, area.getHeight() * 0.32f);
                return;

            case Icon::timing:
                path.startNewSubPath (area.getX() + area.getWidth() * 0.2f, area.getY());
                path.lineTo (area.getX() + area.getWidth() * 0.2f, area.getBottom());
                path.startNewSubPath (area.getRight() - area.getWidth() * 0.2f, area.getY());
                path.lineTo (area.getRight() - area.getWidth() * 0.2f, area.getBottom());
                path.startNewSubPath (area.getX() + area.getWidth() * 0.2f, area.getCentreY());
                path.lineTo (area.getRight() - area.getWidth() * 0.2f, area.getCentreY());
                graphics.strokePath (path, stroke);
                return;

            case Icon::play:
                path.addTriangle (area.getX() + area.getWidth() * 0.1f, area.getY(),
                                  area.getX() + area.getWidth() * 0.1f, area.getBottom(),
                                  area.getRight(), area.getCentreY());
                graphics.fillPath (path);
                return;

            case Icon::pause:
                graphics.fillRect (area.getX() + area.getWidth() * 0.08f, area.getY(),
                                   area.getWidth() * 0.26f, area.getHeight());
                graphics.fillRect (area.getRight() - area.getWidth() * 0.34f, area.getY(),
                                   area.getWidth() * 0.26f, area.getHeight());
                return;

            case Icon::stop:
                graphics.fillRoundedRectangle (area.reduced (area.getWidth() * 0.08f), 1.5f);
                return;

            case Icon::loop:
                path.addArc (area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                             juce::MathConstants<float>::pi * 0.15f,
                             juce::MathConstants<float>::pi * 1.75f, true);
                graphics.strokePath (path, stroke);
                graphics.fillPath (makeArrow (area.withTrimmedBottom (area.getHeight() * 0.55f), true));
                return;

            case Icon::audition:
                path.startNewSubPath (area.getX(), area.getCentreY());
                path.lineTo (area.getX() + area.getWidth() * 0.3f, area.getCentreY());
                path.lineTo (area.getX() + area.getWidth() * 0.45f, area.getY());
                path.lineTo (area.getX() + area.getWidth() * 0.6f, area.getBottom());
                path.lineTo (area.getX() + area.getWidth() * 0.75f, area.getCentreY());
                path.lineTo (area.getRight(), area.getCentreY());
                graphics.strokePath (path, stroke);
                return;

            case Icon::magnet:
                path.startNewSubPath (area.getX(), area.getBottom());
                path.lineTo (area.getX(), area.getCentreY());
                path.addArc (area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                             juce::MathConstants<float>::pi * 1.5f,
                             juce::MathConstants<float>::pi * 2.5f, false);
                path.lineTo (area.getRight(), area.getBottom());
                graphics.strokePath (path, stroke);
                return;

            case Icon::undo:
            case Icon::redo:
            {
                const auto flip = icon == Icon::redo;

                path.addArc (area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                             juce::MathConstants<float>::pi * 0.35f,
                             juce::MathConstants<float>::pi * 1.85f, true);
                graphics.strokePath (path, stroke, flip
                    ? juce::AffineTransform::scale (-1.0f, 1.0f, area.getCentreX(), area.getCentreY())
                    : juce::AffineTransform());

                juce::Path head;
                head.addTriangle (area.getCentreX() - area.getWidth() * 0.42f, area.getY(),
                                  area.getCentreX() - area.getWidth() * 0.06f, area.getY(),
                                  area.getCentreX() - area.getWidth() * 0.42f,
                                  area.getY() + area.getHeight() * 0.36f);
                graphics.fillPath (head, flip
                    ? juce::AffineTransform::scale (-1.0f, 1.0f, area.getCentreX(), area.getCentreY())
                    : juce::AffineTransform());
                return;
            }

            case Icon::panel:
                graphics.drawRoundedRectangle (area, 2.0f, stroke.getStrokeThickness());
                graphics.fillRect (area.getRight() - area.getWidth() * 0.38f, area.getY(),
                                   area.getWidth() * 0.38f, area.getHeight());
                return;

            case Icon::fit:
                path.startNewSubPath (area.getCentreX(), area.getY());
                path.lineTo (area.getCentreX(), area.getBottom());
                graphics.strokePath (path, stroke);
                graphics.fillPath (makeArrow (area, true));
                graphics.fillPath (makeArrow (area, false));
                return;

            case Icon::overview:
                graphics.drawRoundedRectangle (area, 2.0f, stroke.getStrokeThickness());
                graphics.fillRect (area.getX() + area.getWidth() * 0.15f, area.getCentreY() - 1.0f,
                                   area.getWidth() * 0.4f, 2.0f);
                return;
        }
    }

    [[nodiscard]] static juce::Path makeArrow (juce::Rectangle<float> area, bool pointsUp)
    {
        juce::Path head;
        const auto tip = pointsUp ? area.getY() : area.getBottom();
        const auto base = pointsUp ? area.getY() + area.getHeight() * 0.3f
                                   : area.getBottom() - area.getHeight() * 0.3f;

        head.addTriangle (area.getCentreX(), tip,
                          area.getCentreX() - area.getWidth() * 0.26f, base,
                          area.getCentreX() + area.getWidth() * 0.26f, base);
        return head;
    }

    Icon icon;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};

} // namespace rvcara
