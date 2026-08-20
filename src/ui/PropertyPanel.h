#pragma once

#include "common/ConversionResult.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace rvcara
{
/** @brief The column of cards down the right: how pitch is snapped, and which voice is singing.

    Everything on it is set from outside; the panel owns its controls but none of its state.
*/
class PropertyPanel final : public juce::Component
{
public:
    PropertyPanel();

    /** @brief Everything the panel reports, gathered so it updates in one call. */
    struct State
    {
        bool isChromatic { true };
        int scaleRoot { 0 };
        int scaleMode { 1 };
        bool snapWhileDragging { true };

        bool showBeats { false };
        juce::String beatSignature { "4/4" };
        double tempo { 120.0 };
        juce::String gridDivision { "1/4" };
        bool snapCycle { false };

        double brightness { 100.0 };

        juce::String voiceName { "No model" };
        juce::String voiceDetail;

        juce::String status;
        juce::String statusDetail;
        juce::String noteStatus;

        float progress { 0.0f };
        bool isBusy { false };
        bool isAlert { false };
        bool canEdit { false };
    };

    void setState (const State& state);

    std::function<void()> onVoiceClicked;
    std::function<void (bool)> onChromaticChanged;
    std::function<void()> onScaleClicked;
    std::function<void (bool)> onSnapWhileDraggingChanged;
    std::function<void (bool)> onShowBeatsChanged;
    std::function<void()> onBeatClicked;
    std::function<void (double)> onTempoChanged;
    std::function<void()> onGridClicked;
    std::function<void (bool)> onSnapCycleChanged;
    std::function<void (double)> onBrightnessChanged;

    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    static constexpr int pitchCardHeight = 150;
    static constexpr int timeCardHeight = 176;
    static constexpr int brightnessCardHeight = 76;
    static constexpr int voiceCardHeight = 160;

    juce::TextButton chromaticButton { "Chromatic" };
    juce::TextButton scaleButton { "Scale" };
    juce::TextButton rootButton { "C Major" };
    juce::ToggleButton dragSnapButton { "Snap while dragging" };
    juce::TextButton voiceButton { "No model" };

    juce::TextButton beatsButton { "Beats" };
    juce::TextButton timeButton { "Time" };
    juce::TextButton beatButton { "4/4" };
    juce::Slider tempoSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextButton gridButton { "1/4" };
    juce::ToggleButton snapCycleButton { "Snap cycle" };

    juce::Slider brightnessSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    juce::Rectangle<int> pitchCard;
    juce::Rectangle<int> timeCard;
    juce::Rectangle<int> brightnessCard;
    juce::Rectangle<int> voiceCard;

    State current;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PropertyPanel)
};

} // namespace rvcara
