#pragma once

#include "ara/ConversionModification.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

namespace rvcara
{
/** @brief Draws the melody the model followed over the loudness it produced. */
class PitchCurveView final : public juce::Component
{
public:
    PitchCurveView();

    void setConversion (ConversionPointer conversion);

    void setCaption (const juce::String& caption, bool isAlert);

    void paint (juce::Graphics& graphics) override;

private:
    [[nodiscard]] std::optional<float> getYForFrequency (float frequencyHz, juce::Rectangle<float> bounds) const;

    [[nodiscard]] double getConversionSeconds() const;

    [[nodiscard]] static int getGridStepInSemitones (juce::Rectangle<float> bounds);

    void paintPitchGrid (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    void paintOctaveNames (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    void paintTimeRuler (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    void paintEnvelope (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    void paintPitch (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    void paintCaption (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    void rebuildEnvelope();

    ConversionPointer conversion;

    juce::String caption;
    bool isCaptionAlert { false };

    static constexpr float lowestDisplayedHz = 65.0f;
    static constexpr float highestDisplayedHz = 1050.0f;

    std::vector<float> envelope;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchCurveView)
};
}
