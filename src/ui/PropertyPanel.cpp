#include "ui/PropertyPanel.h"

#include "ui/PanelLookAndFeel.h"

namespace rvcara
{
namespace
{
    using Palette = PanelLookAndFeel::Palette;
    using TypeScale = PanelLookAndFeel::TypeScale;
    using Metrics = PanelLookAndFeel::Metrics;

    constexpr int modeRadioGroup = 2;
} // namespace

PropertyPanel::PropertyPanel()
{
    setOpaque (true);

    for (auto* button : { &chromaticButton, &scaleButton })
    {
        button->setClickingTogglesState (true);
        button->setRadioGroupId (modeRadioGroup);
        addAndMakeVisible (*button);
    }

    chromaticButton.setToggleState (true, juce::dontSendNotification);
    chromaticButton.onClick = [this] { if (onChromaticChanged != nullptr) onChromaticChanged (true); };
    scaleButton.onClick = [this] { if (onChromaticChanged != nullptr) onChromaticChanged (false); };

    rootButton.setTriggeredOnMouseDown (true);
    rootButton.onClick = [this] { if (onScaleClicked != nullptr) onScaleClicked(); };
    addAndMakeVisible (rootButton);

    dragSnapButton.setToggleState (true, juce::dontSendNotification);
    dragSnapButton.onClick = [this]
    {
        if (onSnapWhileDraggingChanged != nullptr)
            onSnapWhileDraggingChanged (dragSnapButton.getToggleState());
    };
    addAndMakeVisible (dragSnapButton);

    voiceButton.setTriggeredOnMouseDown (true);
    voiceButton.onClick = [this] { if (onVoiceClicked != nullptr) onVoiceClicked(); };
    addAndMakeVisible (voiceButton);
}

void PropertyPanel::setState (const State& state)
{
    current = state;

    chromaticButton.setToggleState (state.isChromatic, juce::dontSendNotification);
    scaleButton.setToggleState (! state.isChromatic, juce::dontSendNotification);
    dragSnapButton.setToggleState (state.snapWhileDragging, juce::dontSendNotification);

    voiceButton.setButtonText (state.voiceName);

    for (auto* button : { &chromaticButton, &scaleButton })
        button->setEnabled (state.canEdit);

    rootButton.setEnabled (state.canEdit && ! state.isChromatic);
    dragSnapButton.setEnabled (state.canEdit);

    repaint();
}

void PropertyPanel::resized()
{
    auto bounds = getLocalBounds().reduced (Metrics::gap, Metrics::gap);

    pitchCard = bounds.removeFromTop (pitchCardHeight);
    bounds.removeFromTop (Metrics::gap);
    voiceCard = bounds.removeFromTop (voiceCardHeight);

    auto pitchInside = pitchCard.reduced (Metrics::gap + 2, Metrics::gap);
    pitchInside.removeFromTop (16 + 8);

    auto modes = pitchInside.removeFromTop (Metrics::controlHeight);
    chromaticButton.setBounds (modes.removeFromLeft (modes.getWidth() / 2).withTrimmedRight (3));
    scaleButton.setBounds (modes.withTrimmedLeft (3));

    pitchInside.removeFromTop (Metrics::gap);
    rootButton.setBounds (pitchInside.removeFromTop (Metrics::controlHeight));
    pitchInside.removeFromTop (Metrics::gap);
    dragSnapButton.setBounds (pitchInside.removeFromTop (22));

    auto voiceInside = voiceCard.reduced (Metrics::gap + 2, Metrics::gap);
    voiceInside.removeFromTop (16 + 8);
    voiceButton.setBounds (voiceInside.removeFromTop (Metrics::controlHeight));
}

void PropertyPanel::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::ground);

    PanelLookAndFeel::drawCard (graphics, pitchCard, "Pitch");
    PanelLookAndFeel::drawCard (graphics, voiceCard, "Voice");

    auto inside = voiceCard.reduced (Metrics::gap + 2, Metrics::gap);
    inside.removeFromTop (16 + 8 + Metrics::controlHeight + 6);

    PanelLookAndFeel::drawTrackedText (graphics, current.voiceDetail.toUpperCase(),
                                       inside.removeFromTop (15).toFloat(),
                                       juce::Justification::left, TypeScale::label,
                                       Metrics::tracking * 0.5f, Palette::dimText);

    inside.removeFromTop (Metrics::gap);

    graphics.setColour (current.isAlert ? Palette::alert
                        : current.isBusy ? Palette::accent
                                         : Palette::text);
    graphics.setFont (juce::Font { juce::FontOptions { TypeScale::value } });
    graphics.drawText (current.status, inside.removeFromTop (20), juce::Justification::left, false);

    {
        const auto track = inside.removeFromTop (4).toFloat();

        graphics.setColour (Palette::well);
        graphics.fillRoundedRectangle (track, 2.0f);

        if (current.progress > 0.0f)
        {
            graphics.setColour (Palette::accent);
            graphics.fillRoundedRectangle (track.withWidth (track.getWidth() * current.progress), 2.0f);
        }
    }

    inside.removeFromTop (7);

    const auto detail = current.statusDetail.isNotEmpty()
                          ? current.statusDetail + "   " + current.noteStatus
                          : current.noteStatus;

    PanelLookAndFeel::drawTrackedText (graphics, detail.toUpperCase(),
                                       inside.removeFromTop (15).toFloat(),
                                       juce::Justification::left, TypeScale::label,
                                       Metrics::tracking * 0.5f, Palette::dimText);
}

} // namespace rvcara
