#include "PitchConversions.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace rvcara;
using Catch::Approx;

TEST_CASE ("hzToMel matches the reference formula", "[pitch]")
{
    // RVC's mel scale, 1127 * ln(1 + f / 700). Values computed with the same expression
    // in the Python reference so a transcription slip in either constant is caught.
    CHECK (hzToMel (0.0) == Approx (0.0));
    CHECK (hzToMel (50.0) == Approx (77.75496616579426).epsilon (1e-9));
    CHECK (hzToMel (440.0) == Approx (549.6415135509689).epsilon (1e-9));
    CHECK (hzToMel (1100.0) == Approx (1064.408233163639).epsilon (1e-9));
}

TEST_CASE ("mel conversion round-trips", "[pitch]")
{
    for (const auto frequencyHz : { 55.0, 110.0, 220.0, 440.0, 880.0, 1046.5 })
        CHECK (melToHz (hzToMel (frequencyHz)) == Approx (frequencyHz).epsilon (1e-9));
}

TEST_CASE ("zero and negative frequencies mean unvoiced rather than a logarithm of zero", "[pitch]")
{
    // The pipeline uses 0 Hz as the unvoiced marker, so every conversion has to survive
    // it. Returning -inf here would propagate through the quantiser into the vocoder.
    CHECK (hzToMel (0.0) == 0.0);
    CHECK (hzToMel (-100.0) == 0.0);
    CHECK (hzToCents (0.0, 10.0) == 0.0);
    CHECK (hzToCents (440.0, 0.0) == 0.0);
}

TEST_CASE ("cents conversions are consistent and correctly scaled", "[pitch]")
{
    // An octave is 1200 cents, a semitone 100.
    CHECK (hzToCents (880.0, 440.0) == Approx (1200.0).epsilon (1e-12));
    CHECK (hzToCents (440.0, 880.0) == Approx (-1200.0).epsilon (1e-12));
    CHECK (centsToHz (1200.0, 440.0) == Approx (880.0).epsilon (1e-12));

    for (const auto frequencyHz : { 55.0, 261.63, 987.77 })
        CHECK (centsToHz (hzToCents (frequencyHz, 10.0), 10.0) == Approx (frequencyHz).epsilon (1e-9));
}

TEST_CASE ("semitonesToRatio gives the expected intervals", "[pitch]")
{
    CHECK (semitonesToRatio (0.0) == Approx (1.0));
    CHECK (semitonesToRatio (12.0) == Approx (2.0).epsilon (1e-12));
    CHECK (semitonesToRatio (-12.0) == Approx (0.5).epsilon (1e-12));
    CHECK (semitonesToRatio (1.0) == Approx (1.0594630943592953).epsilon (1e-12));
}

TEST_CASE ("the coarse pitch quantiser spans its range", "[pitch]")
{
    // The reference range and bin count: 50 Hz to 1100 Hz over 255 bins.
    const CoarsePitchQuantiser quantiser { 50.0, 1100.0, 255 };

    SECTION ("the endpoints land on the first and last bin")
    {
        CHECK (quantiser.toBin (50.0) == 1);
        CHECK (quantiser.toBin (1100.0) == 255);
    }

    SECTION ("unvoiced frames land on bin 1, not bin 0")
    {
        // The embedding table has no entry for "no pitch"; the vocoder gates the
        // excitation with the continuous track instead.
        CHECK (quantiser.toBin (0.0) == 1);
    }

    SECTION ("frequencies outside the range are clamped rather than wrapped")
    {
        CHECK (quantiser.toBin (10.0) == 1);
        CHECK (quantiser.toBin (20000.0) == 255);
    }

    SECTION ("the mapping is monotonic")
    {
        auto previous = quantiser.toBin (50.0);

        for (double frequencyHz = 55.0; frequencyHz <= 1100.0; frequencyHz += 5.0)
        {
            const auto bin = quantiser.toBin (frequencyHz);
            CHECK (bin >= previous);
            previous = bin;
        }
    }

    SECTION ("the spacing is mel, not logarithmic")
    {
        // The discriminating property: under mel spacing a fixed *ratio* covers more
        // bins as frequency rises, because d(mel)/d(log f) grows with f. Under
        // logarithmic spacing the two spans below would be equal. Both are one semitone.
        const auto lowSpan = quantiser.toBin (110.0) - quantiser.toBin (103.83);
        const auto highSpan = quantiser.toBin (880.0) - quantiser.toBin (830.61);

        CHECK (highSpan > lowSpan);
    }

    SECTION ("known bins agree with the reference quantiser")
    {
        // Computed with the NumPy implementation in tools/rvcara_export/pipeline.py.
        CHECK (quantiser.toBin (220.0) == 60);
        CHECK (quantiser.toBin (440.0) == 122);
        CHECK (quantiser.toBin (880.0) == 217);
    }
}
