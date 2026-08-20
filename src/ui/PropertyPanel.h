#pragma once

#include "common/ConversionResult.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace rvcara
{
/** @brief The column of cards down the right: the voice, the selected notes, and what the
           plug-in is doing.

    Everything on it is set from outside; the panel owns its controls but none of its state.
*/
class PropertyPanel final : public juce::Component
{
public:
    PropertyPanel();

    /** @brief Everything the panel reports, gathered in one place so it updates in one call. */
    struct State
    {
        juce::String voiceName { "No model" };
        juce::String voiceDetail;

        juce::String status;
        juce::String statusDetail;
        juce::String noteStatus;

        float progress { 0.0f };
        bool isBusy { false };
        bool isAlert { false };

        bool canEdit { false };
        int numSelected { 0 };
        float shape { 1.0f };
        juce::String scaleName { "Chromatic" };
    };

    void setState (const State& state);

    std::function<void()> onVoiceClicked;
    std::function<void()> onScaleClicked;
    std::function<void (float)> onShapeChanged;
    std::function<void()> onSnapClicked;
    std::function<void()> onResetClicked;

    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    static constexpr int voiceCardHeight = 92;
    static constexpr int noteCardHeight = 150;
    static constexpr int statusCardHeight = 116;

    void applyShape();

    juce::TextButton voiceButton { "No model" };
    juce::TextButton scaleButton { "Chromatic" };
    juce::TextButton snapButton { "Snap" };
    juce::TextButton resetButton { "Reset" };
    juce::Slider shapeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    juce::Rectangle<int> voiceCard;
    juce::Rectangle<int> noteCard;
    juce::Rectangle<int> statusCard;

    State current;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PropertyPanel)
};

} // namespace rvcara
