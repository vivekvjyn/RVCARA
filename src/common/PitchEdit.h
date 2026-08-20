#pragma once

#include <cstdint>
#include <vector>

namespace rvcara
{
/** @brief One note of a performance: where the segmenter heard it, and what the user did to it.

    The offset moves a note relative to what was actually sung rather than onto an absolute
    pitch, so a note nobody has touched leaves the take exactly as it was — which is what
    makes an untouched edit free.
*/
struct EditedNote
{
    double startSeconds { 0.0 };
    double endSeconds { 0.0 };

    /** @brief The note the segmenter named, in MIDI semitones. Drawn, never rendered. */
    float sungMidiNote { 60.0f };

    /** @brief How far the note moves from where it was sung, in semitones. */
    float offsetSemitones { 0.0f };

    /** @brief How much of the sung contour survives: one keeps all of it, zero is dead flat,
               and more than one exaggerates what was already there.
    */
    float depth { 1.0f };

    /** @brief Semitones added at the note's start, fading to nothing by its middle. */
    float tiltLeft { 0.0f };

    /** @brief Semitones added at the note's end, fading to nothing back to its middle. */
    float tiltRight { 0.0f };

    /** @brief True where the segmenter heard no note, which the editor leaves alone. */
    bool isRest { false };

    /** @brief Whether this note would change the take at all. */
    [[nodiscard]] bool isNeutral() const noexcept
    {
        return isRest
            || (offsetSemitones == 0.0f && depth == 1.0f && tiltLeft == 0.0f && tiltRight == 0.0f);
    }

    /** @brief Where the note is drawn, which is where the singer will land. */
    [[nodiscard]] float getEditedMidiNote() const noexcept { return sungMidiNote + offsetSemitones; }

    [[nodiscard]] bool operator== (const EditedNote& other) const noexcept
    {
        return startSeconds == other.startSeconds
            && endSeconds == other.endSeconds
            && sungMidiNote == other.sungMidiNote
            && offsetSemitones == other.offsetSemitones
            && depth == other.depth
            && tiltLeft == other.tiltLeft
            && tiltRight == other.tiltRight
            && isRest == other.isRest;
    }

    [[nodiscard]] bool operator!= (const EditedNote& other) const noexcept { return ! (*this == other); }
};

/** @brief A whole performance as notes, which is what the pitch editor edits. */
struct PitchEdit
{
    std::vector<EditedNote> notes;

    /** @brief Whether the edit would change the take at all, so a render can skip it. */
    [[nodiscard]] bool isNeutral() const noexcept;

    [[nodiscard]] bool operator== (const PitchEdit& other) const noexcept { return notes == other.notes; }
    [[nodiscard]] bool operator!= (const PitchEdit& other) const noexcept { return ! (*this == other); }

    [[nodiscard]] std::uint64_t getHash() const noexcept;
};

/** @brief The MIDI note a stretch of a sung track centres on.

    Taking the median rather than the mean keeps a slide into or out of the note from
    dragging the answer with it.

    @param fundamentalFrequencyHz  The track to measure, one entry per frame.
    @param frameRate               Frames per second of that track.
    @param firstFrame              The entry that time zero falls on.
    @param startSeconds            Where the stretch begins, relative to time zero.
    @param endSeconds              Where the stretch ends.
    @return The median note over the voiced frames, or zero when the stretch has none.
*/
[[nodiscard]] float getSungMidiNote (const std::vector<float>& fundamentalFrequencyHz,
                                     double frameRate,
                                     int firstFrame,
                                     double startSeconds,
                                     double endSeconds);

/** @brief Rewrites a sung track so every edited note lands where the edit puts it.

    Each note is moved by its offset, squeezed toward its own centre by its depth, and tilted
    from each edge toward the middle, so an untouched note returns the track unchanged.

    @param fundamentalFrequencyHz  The track to rewrite in place, one entry per frame.
    @param edit                    The notes to apply.
    @param frameRate               Frames per second of the track.
    @param firstFrame              The entry that time zero falls on.
*/
void applyPitchEdit (std::vector<float>& fundamentalFrequencyHz,
                     const PitchEdit& edit,
                     double frameRate,
                     int firstFrame);
}
