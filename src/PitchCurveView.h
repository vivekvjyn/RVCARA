#pragma once

#include "ConversionModification.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

namespace rvcara
{

/** Draws the melody the model followed, over the loudness of what it produced.

    This is the most informative thing the interface can show, because it answers the question
    a user actually has when a render sounds wrong: did the model hear the right notes? A
    conversion that is off by an octave, or that has latched onto a harmonic during a breathy
    passage, is obvious here and inaudible-but-mysterious otherwise.

    Pitch is drawn on a logarithmic axis with semitone gridlines, because that is the axis on
    which musical intervals are constant distances — a linear frequency axis compresses the top
    octave into a sliver and makes vibrato at the top of a range look like noise.

    The view knows nothing about rendering state. It shows a conversion, or a caption when
    there is nothing to show; the editor decides which.
*/
class PitchCurveView final : public juce::Component
{
public:
    PitchCurveView();

    /** Shows a conversion, or clears the display when given nullptr. */
    void setConversion (ConversionPointer conversion);

    /** Shows a line or two of text instead of a curve.

        @param caption  What to say, or empty to show the curve alone.
        @param isAlert  True when the caption is a failure, which colours it.
    */
    void setCaption (const juce::String& caption, bool isAlert);

    void paint (juce::Graphics& graphics) override;

private:
    /** @returns The vertical position for a frequency, or nothing if out of range. */
    [[nodiscard]] std::optional<float> getYForFrequency (float frequencyHz, juce::Rectangle<float> bounds) const;

    /** @returns The conversion's length in seconds, or zero when there is none. */
    [[nodiscard]] double getConversionSeconds() const;

    /** @returns 1 when there is room for a line per semitone, 12 when only octaves fit. */
    [[nodiscard]] static int getGridStepInSemitones (juce::Rectangle<float> bounds);

    /** Draws the gridlines, under everything else. */
    void paintPitchGrid (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    /** Draws the octave note names, over everything else. */
    void paintOctaveNames (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    /** Draws second markers along the bottom. */
    void paintTimeRuler (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    /** Draws the loudness envelope of the conversion as a filled silhouette. */
    void paintEnvelope (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    /** Draws the pitch track, broken at unvoiced frames. */
    void paintPitch (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    /** Draws the caption, centred, over everything else. */
    void paintCaption (juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    void rebuildEnvelope();

    ConversionPointer conversion;

    juce::String caption;
    bool isCaptionAlert { false };

    /** Displayed pitch range, chosen to cover a singing voice with headroom.

        Fixed rather than fitted to the data: a range that moves as the user edits makes two
        renders impossible to compare by eye.
    */
    static constexpr float lowestDisplayedHz = 65.0f;    // C2
    static constexpr float highestDisplayedHz = 1050.0f; // ~C6

    /** Loudness envelope, one point per column, recomputed when the conversion changes. */
    std::vector<float> envelope;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchCurveView)
};

} // namespace rvcara
