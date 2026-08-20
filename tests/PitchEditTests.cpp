#include "common/PitchEdit.h"
#include "dsp/PitchConversions.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace rvcara;

namespace
{
constexpr double frameRate = 100.0;

std::vector<float> makeTrack (const std::vector<float>& midiNotes)
{
    std::vector<float> track;
    track.reserve (midiNotes.size());

    for (const auto midiNote : midiNotes)
        track.push_back (static_cast<float> (midiNoteToHz (static_cast<double> (midiNote))));

    return track;
}

float midiAt (const std::vector<float>& track, std::size_t frameIndex)
{
    return static_cast<float> (hzToMidiNote (static_cast<double> (track[frameIndex])));
}

EditedNote makeNote (double startSeconds, double endSeconds)
{
    EditedNote note;
    note.startSeconds = startSeconds;
    note.endSeconds = endSeconds;
    return note;
}
} // namespace

TEST (PitchEditTests, AnUntouchedEditLeavesTheTakeExactlyAsItWas)
{
    const auto original = makeTrack ({ 60.0f, 60.4f, 59.6f, 60.2f });
    auto track = original;

    PitchEdit edit;
    edit.notes.push_back (makeNote (0.0, 0.04));

    ASSERT_TRUE (edit.isNeutral());

    applyPitchEdit (track, edit, frameRate, 0);

    EXPECT_EQ (track, original);
}

TEST (PitchEditTests, AnOffsetMovesTheWholeNoteAndKeepsItsShape)
{
    const auto original = makeTrack ({ 60.0f, 60.5f, 59.5f, 60.0f });
    auto track = original;

    PitchEdit edit;
    auto note = makeNote (0.0, 0.04);
    note.offsetSemitones = 3.0f;
    edit.notes.push_back (note);

    applyPitchEdit (track, edit, frameRate, 0);

    for (std::size_t frameIndex = 0; frameIndex < track.size(); ++frameIndex)
        EXPECT_NEAR (midiAt (track, frameIndex), midiAt (original, frameIndex) + 3.0f, 1.0e-3f);
}

TEST (PitchEditTests, ZeroDepthFlattensTheNoteOntoItsOwnCentre)
{
    auto track = makeTrack ({ 60.0f, 61.0f, 59.0f, 62.0f, 58.0f });

    PitchEdit edit;
    auto note = makeNote (0.0, 0.05);
    note.depth = 0.0f;
    edit.notes.push_back (note);

    applyPitchEdit (track, edit, frameRate, 0);

    for (std::size_t frameIndex = 0; frameIndex < track.size(); ++frameIndex)
        EXPECT_NEAR (midiAt (track, frameIndex), 60.0f, 1.0e-3f);
}

TEST (PitchEditTests, ZeroDepthWithAnOffsetFlattensOntoTheMovedCentre)
{
    auto track = makeTrack ({ 60.0f, 61.0f, 59.0f });

    PitchEdit edit;
    auto note = makeNote (0.0, 0.03);
    note.depth = 0.0f;
    note.offsetSemitones = -2.5f;
    edit.notes.push_back (note);

    applyPitchEdit (track, edit, frameRate, 0);

    for (std::size_t frameIndex = 0; frameIndex < track.size(); ++frameIndex)
        EXPECT_NEAR (midiAt (track, frameIndex), 57.5f, 1.0e-3f);
}

TEST (PitchEditTests, ARestIsLeftAlone)
{
    const auto original = makeTrack ({ 60.0f, 61.0f });
    auto track = original;

    PitchEdit edit;
    auto note = makeNote (0.0, 0.02);
    note.isRest = true;
    note.offsetSemitones = 5.0f;
    edit.notes.push_back (note);

    applyPitchEdit (track, edit, frameRate, 0);

    EXPECT_EQ (track, original);
}

TEST (PitchEditTests, FramesOutsideEveryNoteAreUntouched)
{
    const auto original = makeTrack ({ 60.0f, 60.0f, 70.0f, 70.0f });
    auto track = original;

    PitchEdit edit;
    auto note = makeNote (0.0, 0.02);
    note.offsetSemitones = 1.0f;
    edit.notes.push_back (note);

    applyPitchEdit (track, edit, frameRate, 0);

    EXPECT_NEAR (midiAt (track, 0), 61.0f, 1.0e-3f);
    EXPECT_FLOAT_EQ (track[2], original[2]);
    EXPECT_FLOAT_EQ (track[3], original[3]);
}

TEST (PitchEditTests, TheFirstFrameSaysWhereTimeZeroIs)
{
    const auto original = makeTrack ({ 70.0f, 70.0f, 60.0f, 60.0f });
    auto track = original;

    PitchEdit edit;
    auto note = makeNote (0.0, 0.02);
    note.offsetSemitones = 2.0f;
    edit.notes.push_back (note);

    applyPitchEdit (track, edit, frameRate, 2);

    EXPECT_FLOAT_EQ (track[0], original[0]);
    EXPECT_FLOAT_EQ (track[1], original[1]);
    EXPECT_NEAR (midiAt (track, 2), 62.0f, 1.0e-3f);
    EXPECT_NEAR (midiAt (track, 3), 62.0f, 1.0e-3f);
}

TEST (PitchEditTests, TheSungNoteIsTheMedianSoASlideDoesNotDragIt)
{
    const auto track = makeTrack ({ 60.0f, 60.0f, 60.0f, 72.0f });

    EXPECT_NEAR (getSungMidiNote (track, frameRate, 0, 0.0, 0.04), 60.0f, 1.0e-3f);
}

TEST (PitchEditTests, AnUnvoicedStretchHasNoSungNote)
{
    const std::vector<float> track { 0.0f, 0.0f, 0.0f };

    EXPECT_FLOAT_EQ (getSungMidiNote (track, frameRate, 0, 0.0, 0.03), 0.0f);
}

TEST (PitchEditTests, MovingANoteChangesTheHash)
{
    PitchEdit edit;
    edit.notes.push_back (makeNote (0.0, 1.0));

    const auto before = edit.getHash();
    edit.notes.front().offsetSemitones = 1.0f;

    EXPECT_NE (edit.getHash(), before);
    EXPECT_FALSE (edit.isNeutral());
}

TEST (PitchEditTests, TiltRaisesTheStartAndFadesToNothingByTheMiddle)
{
    auto track = makeTrack ({ 60.0f, 60.0f, 60.0f, 60.0f, 60.0f });

    PitchEdit edit;
    auto note = makeNote (0.0, 0.05);
    note.tiltLeft = 2.0f;
    edit.notes.push_back (note);

    ASSERT_FALSE (edit.isNeutral());

    applyPitchEdit (track, edit, frameRate, 0);

    EXPECT_NEAR (midiAt (track, 0), 62.0f, 1.0e-3f);
    EXPECT_NEAR (midiAt (track, 1), 61.0f, 1.0e-3f);
    EXPECT_NEAR (midiAt (track, 2), 60.0f, 1.0e-3f);
    EXPECT_NEAR (midiAt (track, 3), 60.0f, 1.0e-3f);
    EXPECT_NEAR (midiAt (track, 4), 60.0f, 1.0e-3f);
}

TEST (PitchEditTests, TiltRaisesTheEndAndFadesBackToTheMiddle)
{
    auto track = makeTrack ({ 60.0f, 60.0f, 60.0f, 60.0f, 60.0f });

    PitchEdit edit;
    auto note = makeNote (0.0, 0.05);
    note.tiltRight = -4.0f;
    edit.notes.push_back (note);

    applyPitchEdit (track, edit, frameRate, 0);

    EXPECT_NEAR (midiAt (track, 0), 60.0f, 1.0e-3f);
    EXPECT_NEAR (midiAt (track, 2), 60.0f, 1.0e-3f);
    EXPECT_NEAR (midiAt (track, 3), 58.0f, 1.0e-3f);
    EXPECT_NEAR (midiAt (track, 4), 56.0f, 1.0e-3f);
}

TEST (PitchEditTests, TiltAddsToTheOffsetRatherThanReplacingIt)
{
    auto track = makeTrack ({ 60.0f, 60.0f, 60.0f });

    PitchEdit edit;
    auto note = makeNote (0.0, 0.03);
    note.offsetSemitones = 5.0f;
    note.tiltLeft = 1.0f;
    edit.notes.push_back (note);

    applyPitchEdit (track, edit, frameRate, 0);

    EXPECT_NEAR (midiAt (track, 0), 66.0f, 1.0e-3f);
    EXPECT_NEAR (midiAt (track, 1), 65.0f, 1.0e-3f);
    EXPECT_NEAR (midiAt (track, 2), 65.0f, 1.0e-3f);
}
