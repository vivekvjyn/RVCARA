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
    constexpr int timelineRadioGroup = 3;
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

    for (auto* button : { &beatsButton, &timeButton })
    {
        button->setClickingTogglesState (true);
        button->setRadioGroupId (timelineRadioGroup);
        addAndMakeVisible (*button);
    }

    timeButton.setToggleState (true, juce::dontSendNotification);
    beatsButton.onClick = [this] { if (onShowBeatsChanged != nullptr) onShowBeatsChanged (true); };
    timeButton.onClick = [this] { if (onShowBeatsChanged != nullptr) onShowBeatsChanged (false); };

    beatButton.setTriggeredOnMouseDown (true);
    beatButton.onClick = [this] { if (onBeatClicked != nullptr) onBeatClicked(); };
    addAndMakeVisible (beatButton);

    gridButton.setTriggeredOnMouseDown (true);
    gridButton.onClick = [this] { if (onGridClicked != nullptr) onGridClicked(); };
    addAndMakeVisible (gridButton);

    tempoSlider.setRange (20.0, 300.0, 0.01);
    tempoSlider.setValue (120.0, juce::dontSendNotification);
    tempoSlider.onValueChange = [this]
    {
        if (onTempoChanged != nullptr)
            onTempoChanged (tempoSlider.getValue());
    };
    addAndMakeVisible (tempoSlider);

    snapCycleButton.onClick = [this]
    {
        if (onSnapCycleChanged != nullptr)
            onSnapCycleChanged (snapCycleButton.getToggleState());
    };
    addAndMakeVisible (snapCycleButton);

    brightnessSlider.setRange (55.0, 145.0, 1.0);
    brightnessSlider.setValue (100.0, juce::dontSendNotification);
    brightnessSlider.setDoubleClickReturnValue (true, 100.0);
    brightnessSlider.onValueChange = [this]
    {
        if (onBrightnessChanged != nullptr)
            onBrightnessChanged (brightnessSlider.getValue());
    };
    addAndMakeVisible (brightnessSlider);
}

void PropertyPanel::setState (const State& state)
{
    current = state;

    chromaticButton.setToggleState (state.isChromatic, juce::dontSendNotification);
    scaleButton.setToggleState (! state.isChromatic, juce::dontSendNotification);
    dragSnapButton.setToggleState (state.snapWhileDragging, juce::dontSendNotification);

    voiceButton.setButtonText (state.voiceName);

    beatsButton.setToggleState (state.showBeats, juce::dontSendNotification);
    timeButton.setToggleState (! state.showBeats, juce::dontSendNotification);
    beatButton.setButtonText (state.beatSignature);
    gridButton.setButtonText (state.gridDivision);
    snapCycleButton.setToggleState (state.snapCycle, juce::dontSendNotification);

    if (! tempoSlider.isMouseButtonDown())
        tempoSlider.setValue (state.tempo, juce::dontSendNotification);

    if (! brightnessSlider.isMouseButtonDown())
        brightnessSlider.setValue (state.brightness, juce::dontSendNotification);

    beatButton.setEnabled (state.showBeats);
    gridButton.setEnabled (state.showBeats);
    tempoSlider.setEnabled (state.showBeats);
    snapCycleButton.setEnabled (state.showBeats);

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
    timeCard = bounds.removeFromTop (timeCardHeight);
    bounds.removeFromTop (Metrics::gap);
    brightnessCard = bounds.removeFromTop (brightnessCardHeight);
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

    auto timeInside = timeCard.reduced (Metrics::gap + 2, Metrics::gap);
    timeInside.removeFromTop (16 + 8);

    auto timelineModes = timeInside.removeFromTop (Metrics::controlHeight);
    beatsButton.setBounds (timelineModes.removeFromLeft (timelineModes.getWidth() / 2).withTrimmedRight (3));
    timeButton.setBounds (timelineModes.withTrimmedLeft (3));

    timeInside.removeFromTop (Metrics::gap);

    auto signatureRow = timeInside.removeFromTop (26);
    signatureRow.removeFromLeft (44);
    beatButton.setBounds (signatureRow.removeFromLeft (54));
    signatureRow.removeFromLeft (Metrics::gap);
    signatureRow.removeFromLeft (34);
    gridButton.setBounds (signatureRow.removeFromLeft (54));

    timeInside.removeFromTop (Metrics::gap);
    tempoSlider.setBounds (timeInside.removeFromTop (22).withTrimmedLeft (44));
    timeInside.removeFromTop (Metrics::gap);
    snapCycleButton.setBounds (timeInside.removeFromTop (22));

    auto brightnessInside = brightnessCard.reduced (Metrics::gap + 2, Metrics::gap);
    brightnessInside.removeFromTop (16 + 8);
    brightnessSlider.setBounds (brightnessInside.removeFromTop (22));

    auto voiceInside = voiceCard.reduced (Metrics::gap + 2, Metrics::gap);
    voiceInside.removeFromTop (16 + 8);
    voiceButton.setBounds (voiceInside.removeFromTop (Metrics::controlHeight));
}

void PropertyPanel::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Palette::ground);

    PanelLookAndFeel::drawCard (graphics, pitchCard, "Pitch");
    PanelLookAndFeel::drawCard (graphics, timeCard, "Time");
    PanelLookAndFeel::drawCard (graphics, brightnessCard, "UI Brightness");
    PanelLookAndFeel::drawCard (graphics, voiceCard, "Voice");

    const auto label = [&graphics] (const juce::String& text, juce::Rectangle<int> bounds)
    {
        PanelLookAndFeel::drawTrackedText (graphics, text, bounds.toFloat(),
                                           juce::Justification::left, TypeScale::label,
                                           Metrics::tracking * 0.5f, Palette::dimText);
    };

    label ("BEAT", beatButton.getBounds().withX (timeCard.getX() + Metrics::gap + 2).withWidth (44));
    label ("GRID", gridButton.getBounds().withX (gridButton.getX() - 34).withWidth (34));
    label ("TEMPO", tempoSlider.getBounds().withX (timeCard.getX() + Metrics::gap + 2).withWidth (44));

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
