#pragma once

#include <ara/ConversionModification.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace rvcara::gui
{

/** Draws the melody the model followed, over the loudness of what it produced.

    This is the most informative thing the interface can show, because it answers the
    question a user actually has when a render sounds wrong: did the model hear the right
    notes? A conversion that is off by an octave, or that has latched onto a harmonic
    during a breathy passage, is obvious here and inaudible-but-mysterious otherwise.

    Pitch is drawn on a logarithmic axis with semitone gridlines, because that is the axis
    on which musical intervals are constant distances — a linear frequency axis compresses
    the top octave into a sliver and makes vibrato at the top of a range look like noise.
*/
class PitchCurveView final : public juce::Component
{
public:
    PitchCurveView();

    /** Shows a conversion, or clears the display when given nullptr. */
    void setConversion (ara::ConversionPointer conversion);

    /** Shows progress while a render is running.

        @param state     What the modification is doing.
        @param progress  Fraction complete, for the rendering state.
        @param message   Text to show instead of a curve, for example an error.
    */
    void setRenderState (ara::ConversionModification::State state, float progress, const juce::String& message);

    void paint (juce::Graphics& graphics) override;

private:
    /** @returns The vertical position for a frequency, or nothing if out of range. */
    [[nodiscard]] std::optional<float> getYForFrequency (float frequencyHz, juce::Rectangle<float> bounds) const;

    /** Draws semitone and octave gridlines with note names. */
    void paintGrid (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    /** Draws the loudness envelope of the conversion as a filled silhouette. */
    void paintEnvelope (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    /** Draws the pitch track, broken at unvoiced frames. */
    void paintPitch (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    ara::ConversionPointer conversion;
    ara::ConversionModification::State renderState { ara::ConversionModification::State::idle };
    float renderProgress { 0.0f };
    juce::String statusMessage;

    /** Displayed pitch range, chosen to cover a singing voice with headroom.

        Fixed rather than fitted to the data: a range that moves as the user edits makes
        two renders impossible to compare by eye.
    */
    static constexpr float lowestDisplayedHz = 65.0f;    // C2
    static constexpr float highestDisplayedHz = 1050.0f; // ~C6

    /** Loudness envelope, one point per pixel column, recomputed when the conversion changes. */
    std::vector<float> envelope;

    void rebuildEnvelope();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchCurveView)
};

} // namespace rvcara::gui
