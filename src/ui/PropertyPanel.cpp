#include "ui/PropertyPanel.h"

#include "ui/PanelLookAndFeel.h"

namespace rvcara
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using TypeScale = PanelLookAndFeel::TypeScale;
    using Metrics = PanelLookAndFeel::Metrics;
} // namespace

PropertyPanel::PropertyPanel()
{
    setOpaque (true);

    voiceButton.setTriggeredOnMouseDown (true);
    voiceButton.onClick = [this] { if (onVoiceClicked != nullptr) onVoiceClicked(); };
    addAndMakeVisible (voiceButton);

    scaleButton.setTriggeredOnMouseDown (true);
    scaleButton.onClick = [this] { if (onScaleClicked != nullptr) onScaleClicked(); };
    addAndMakeVisible (scaleButton);

    snapButton.onClick = [this] { if (onSnapClicked != nullptr) onSnapClicked(); };
    addAndMakeVisible (snapButton);

    resetButton.onClick = [this] { if (onResetClicked != nullptr) onResetClicked(); };
    addAndMakeVisible (resetButton);

    shapeSlider.setRange (0.0, 2.0);
    shapeSlider.setValue (1.0, juce::dontSendNotification);
    shapeSlider.setDoubleClickReturnValue (true, 1.0);
    shapeSlider.onDragEnd = [this] { applyShape(); };
    shapeSlider.onValueChange = [this]
    {
        if (! shapeSlider.isMouseButtonDown())
            applyShape();
    };
    addAndMakeVisible (shapeSlider);
}

void PropertyPanel::applyShape()
{
    if (onShapeChanged != nullptr)
        onShapeChanged (static_cast<float> (shapeSlider.getValue()));
}

void PropertyPanel::setState (const State& state)
{
    const auto wasEditable = current.canEdit;
    current = state;

    voiceButton.setButtonText (state.voiceName);
    scaleButton.setButtonText (state.scaleName);

    for (auto* button : { &scaleButton, &snapButton, &resetButton })
        button->setEnabled (state.canEdit);

    shapeSlider.setEnabled (state.canEdit);

    if (! shapeSlider.isMouseButtonDown())
        shapeSlider.setValue (static_cast<double> (state.shape), juce::dontSendNotification);

    juce::ignoreUnused (wasEditable);
    repaint();
}

void PropertyPanel::resized()
{
    auto bounds = getLocalBounds().reduced (Metrics::gap, Metrics::gap);

    voiceCard = bounds.removeFromTop (voiceCardHeight);
    bounds.removeFromTop (Metrics::gap);
    noteCard = bounds.removeFromTop (noteCardHeight);
    bounds.removeFromTop (Metrics::gap);
    statusCard = bounds.removeFromTop (statusCardHeight);

    auto voiceInside = voiceCard.reduced (Metrics::gap + 2, Metrics::gap);
    voiceInside.removeFromTop (16 + 6);
    voiceButton.setBounds (voiceInside.removeFromTop (Metrics::controlHeight));

    auto noteInside = noteCard.reduced (Metrics::gap + 2, Metrics::gap);
    noteInside.removeFromTop (16 + 6);
    noteInside.removeFromTop (14);
    shapeSlider.setBounds (noteInside.removeFromTop (22));
    noteInside.removeFromTop (Metrics::gap);
    scaleButton.setBounds (noteInside.removeFromTop (Metrics::controlHeight));
    noteInside.removeFromTop (Metrics::gap);

    auto actions = noteInside.removeFromTop (Metrics::controlHeight);
    snapButton.setBounds (actions.removeFromLeft (actions.getWidth() / 2).withTrimmedRight (Metrics::gap / 2));
    resetButton.setBounds (actions.withTrimmedLeft (Metrics::gap / 2));
}

void PropertyPanel::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::ground);

    PanelLookAndFeel::drawCard (graphics, voiceCard, "Voice");

    {
        auto detail = voiceCard.reduced (Metrics::gap + 2, Metrics::gap);
        detail.removeFromTop (16 + 6 + Metrics::controlHeight + 4);

        PanelLookAndFeel::drawTrackedText (graphics, current.voiceDetail, detail.toFloat(),
                                           juce::Justification::left, TypeScale::label,
                                           Metrics::tracking * 0.5f, Palette::dimText);
    }

    PanelLookAndFeel::drawCard (graphics, noteCard, "Note");

    {
        auto label = noteCard.reduced (Metrics::gap + 2, Metrics::gap);
        label.removeFromTop (16 + 6);

        const auto shaped = current.numSelected > 0
                              ? juce::String (current.numSelected) + " selected"
                              : juce::String ("All notes");

        PanelLookAndFeel::drawTrackedText (graphics, "SHAPE", label.removeFromTop (14).toFloat(),
                                           juce::Justification::left, TypeScale::label,
                                           Metrics::tracking * 0.5f, Palette::dimText);

        PanelLookAndFeel::drawTrackedText (graphics, shaped.toUpperCase(),
                                           label.withY (label.getY() - 14).withHeight (14).toFloat(),
                                           juce::Justification::right, TypeScale::label,
                                           Metrics::tracking * 0.5f, Palette::dimText);
    }

    auto statusInside = PanelLookAndFeel::drawCard (graphics, statusCard, "Status");

    graphics.setColour (current.isAlert ? Palette::alert
                        : current.isBusy ? Palette::accent
                                         : Palette::text);
    graphics.setFont (juce::Font { juce::FontOptions { TypeScale::value } });
    graphics.drawText (current.status, statusInside.removeFromTop (22), juce::Justification::left, false);

    if (current.statusDetail.isNotEmpty())
        PanelLookAndFeel::drawTrackedText (graphics, current.statusDetail.toUpperCase(),
                                           statusInside.removeFromTop (16).toFloat(),
                                           juce::Justification::left, TypeScale::label,
                                           Metrics::tracking * 0.5f, Palette::dimText);

    statusInside.removeFromTop (6);

    {
        const auto track = statusInside.removeFromTop (4).toFloat();

        graphics.setColour (Palette::well);
        graphics.fillRoundedRectangle (track, 2.0f);

        if (current.progress > 0.0f)
        {
            graphics.setColour (Palette::accent);
            graphics.fillRoundedRectangle (track.withWidth (track.getWidth() * current.progress), 2.0f);
        }
    }

    statusInside.removeFromTop (8);

    PanelLookAndFeel::drawTrackedText (graphics, current.noteStatus.toUpperCase(),
                                       statusInside.removeFromTop (16).toFloat(),
                                       juce::Justification::left, TypeScale::label,
                                       Metrics::tracking * 0.5f, Palette::dimText);
}

} // namespace rvcara
