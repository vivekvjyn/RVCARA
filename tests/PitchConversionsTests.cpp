#include "dsp/PitchConversions.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace rvcara;

namespace
{
    constexpr double tolerance = 1.0e-9;

    double relative (double expected)
    {
        return std::abs (expected) * tolerance;
    }

    CoarsePitchQuantiser makeReferenceQuantiser()
    {
        return CoarsePitchQuantiser { 50.0, 1100.0, 255 };
    }
} // namespace

TEST (PitchConversions, HzToMelMatchesTheReferenceFormula)
{
    EXPECT_NEAR (hzToMel (0.0), 0.0, tolerance);
    EXPECT_NEAR (hzToMel (50.0), 77.75496616579426, relative (77.75));
    EXPECT_NEAR (hzToMel (440.0), 549.6415135509689, relative (549.64));
    EXPECT_NEAR (hzToMel (1100.0), 1064.408233163639, relative (1064.41));
}

TEST (PitchConversions, MelConversionRoundTrips)
{
    for (const auto frequencyHz : { 55.0, 110.0, 220.0, 440.0, 880.0, 1046.5 })
        EXPECT_NEAR (melToHz (hzToMel (frequencyHz)), frequencyHz, relative (frequencyHz));
}

TEST (PitchConversions, ZeroAndNegativeFrequenciesMeanUnvoiced)
{
    EXPECT_EQ (hzToMel (0.0), 0.0);
    EXPECT_EQ (hzToMel (-100.0), 0.0);
    EXPECT_EQ (hzToCents (0.0, 10.0), 0.0);
    EXPECT_EQ (hzToCents (440.0, 0.0), 0.0);
}

TEST (PitchConversions, CentsAreConsistentAndCorrectlyScaled)
{
    EXPECT_NEAR (hzToCents (880.0, 440.0), 1200.0, tolerance);
    EXPECT_NEAR (hzToCents (440.0, 880.0), -1200.0, tolerance);
    EXPECT_NEAR (centsToHz (1200.0, 440.0), 880.0, tolerance);

    for (const auto frequencyHz : { 55.0, 261.63, 987.77 })
        EXPECT_NEAR (centsToHz (hzToCents (frequencyHz, 10.0), 10.0), frequencyHz, relative (frequencyHz));
}

TEST (PitchConversions, SemitonesToRatioGivesTheExpectedIntervals)
{
    EXPECT_NEAR (semitonesToRatio (0.0), 1.0, tolerance);
    EXPECT_NEAR (semitonesToRatio (12.0), 2.0, tolerance);
    EXPECT_NEAR (semitonesToRatio (-12.0), 0.5, tolerance);
    EXPECT_NEAR (semitonesToRatio (1.0), 1.0594630943592953, tolerance);
}

TEST (CoarsePitchQuantiser, EndpointsAndUnvoicedFramesLandInRange)
{
    const auto quantiser = makeReferenceQuantiser();

    EXPECT_EQ (quantiser.toBin (50.0), 1);
    EXPECT_EQ (quantiser.toBin (1100.0), 255);
    EXPECT_EQ (quantiser.toBin (0.0), 1);
    EXPECT_EQ (quantiser.toBin (10.0), 1);
    EXPECT_EQ (quantiser.toBin (20000.0), 255);
}

TEST (CoarsePitchQuantiser, TheMappingIsMonotonic)
{
    const auto quantiser = makeReferenceQuantiser();
    auto previousBin = quantiser.toBin (50.0);

    for (double frequencyHz = 55.0; frequencyHz <= 1100.0; frequencyHz += 5.0)
    {
        const auto bin = quantiser.toBin (frequencyHz);
        EXPECT_GE (bin, previousBin);
        previousBin = bin;
    }
}

TEST (CoarsePitchQuantiser, TheSpacingIsMelRatherThanLogarithmic)
{
    const auto quantiser = makeReferenceQuantiser();

    EXPECT_GT (quantiser.toBin (880.0) - quantiser.toBin (830.61),
               quantiser.toBin (110.0) - quantiser.toBin (103.83));
}

TEST (CoarsePitchQuantiser, KnownBinsAgreeWithTheReferenceQuantiser)
{
    const auto quantiser = makeReferenceQuantiser();

    EXPECT_EQ (quantiser.toBin (220.0), 60);
    EXPECT_EQ (quantiser.toBin (440.0), 122);
    EXPECT_EQ (quantiser.toBin (880.0), 217);
}
