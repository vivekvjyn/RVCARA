#include "dsp/ZeroPhaseFilter.h"
#include "common/BinaryMatrix.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

using namespace rvcara;

namespace
{
    constexpr int referencePadLength = 18;
    constexpr double referenceSampleRate = 16000.0;
    constexpr int referenceNumSamples = 8000;

    std::vector<BiquadCoefficients> getReferenceHighPass()
    {
        return {
            { 0.9699606451838447, -0.9699606451838447, 0.0, 1.0, -0.9813258904926881, 0.0 },
            { 1.0, -2.0, 1.0, 1.0, -1.9696106864374547, 0.9699606452559844 },
            { 1.0, -2.0, 1.0, 1.0, -1.9880652422382208, 0.9884184800471566 },
        };
    }

    std::vector<float> makeSine (double frequencyHz, double sampleRate, int numSamples)
    {
        std::vector<float> signal (static_cast<std::size_t> (numSamples));

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
            signal[static_cast<std::size_t> (sampleIndex)] =
                static_cast<float> (std::sin (2.0 * std::numbers::pi * frequencyHz
                                              * static_cast<double> (sampleIndex) / sampleRate));

        return signal;
    }

    double measureLevel (const std::vector<float>& signal, int firstSample, int lastSample)
    {
        double sumOfSquares = 0.0;

        for (int sampleIndex = firstSample; sampleIndex < lastSample; ++sampleIndex)
        {
            const auto sample = static_cast<double> (signal[static_cast<std::size_t> (sampleIndex)]);
            sumOfSquares += sample * sample;
        }

        return std::sqrt (sumOfSquares / static_cast<double> (lastSample - firstSample));
    }
} // namespace

class ZeroPhaseFilterTest : public testing::Test
{
protected:
    const ZeroPhaseFilter filter { getReferenceHighPass(), referencePadLength };

    struct Levels
    {
        double before;
        double after;
    };

    Levels filterSine (double frequencyHz) const
    {
        auto signal = makeSine (frequencyHz, referenceSampleRate, referenceNumSamples);
        const auto before = measureLevel (signal, 1000, referenceNumSamples - 1000);

        filter.process (signal.data(), referenceNumSamples);
        return { before, measureLevel (signal, 1000, referenceNumSamples - 1000) };
    }
};

TEST (ZeroPhaseFilter, AnEmptyCascadeIsANoOp)
{
    const ZeroPhaseFilter filter;
    EXPECT_TRUE (filter.isEmpty());

    std::vector<float> signal { 1.0f, 2.0f, 3.0f, 4.0f };
    filter.process (signal.data(), static_cast<int> (signal.size()));

    EXPECT_EQ (signal, std::vector<float> ({ 1.0f, 2.0f, 3.0f, 4.0f }));
}

TEST_F (ZeroPhaseFilterTest, ContentBelowTheCornerIsHeavilyAttenuated)
{
    const auto levels = filterSine (25.0);
    EXPECT_LT (levels.after, levels.before * 0.02);
}

TEST_F (ZeroPhaseFilterTest, ContentAboveTheCornerPassesUntouched)
{
    const auto levels = filterSine (440.0);
    EXPECT_NEAR (levels.after, levels.before, levels.before * 0.001);
}

TEST_F (ZeroPhaseFilterTest, AConstantOffsetIsRemoved)
{
    std::vector<float> signal (referenceNumSamples, 0.5f);
    filter.process (signal.data(), referenceNumSamples);

    EXPECT_LT (std::abs (signal[referenceNumSamples / 2]), 1.0e-4f);
}

TEST_F (ZeroPhaseFilterTest, TheFilterIsZeroPhase)
{
    constexpr auto numSamples = 4001;
    constexpr auto burstHalfWidth = 400;
    constexpr auto centre = numSamples / 2;

    std::vector<float> signal (numSamples, 0.0f);

    for (int offset = -burstHalfWidth; offset <= burstHalfWidth; ++offset)
    {
        const auto raisedCosine = 0.5 - 0.5 * std::cos (std::numbers::pi
                                                        * (static_cast<double> (offset + burstHalfWidth)
                                                           / static_cast<double> (burstHalfWidth)));
        signal[static_cast<std::size_t> (centre + offset)] = static_cast<float> (raisedCosine);
    }

    filter.process (signal.data(), numSamples);

    auto worstAsymmetry = 0.0f;

    for (int offset = 1; offset < centre - 1; ++offset)
        worstAsymmetry = std::max (worstAsymmetry,
                                   std::abs (signal[static_cast<std::size_t> (centre + offset)]
                                             - signal[static_cast<std::size_t> (centre - offset)]));

    EXPECT_LT (worstAsymmetry, 1.0e-5f);
}

TEST_F (ZeroPhaseFilterTest, TheHighPassMatchesTheReferenceFiltfilt)
{
    const juce::File fixtures { RVCARA_TEST_FIXTURE_DIR };

    const auto sourceFixture = BinaryMatrix::load (fixtures.getChildFile ("test_signal.bin"));
    const auto expected = BinaryMatrix::load (fixtures.getChildFile ("expected_high_pass.bin"));

    ASSERT_TRUE (sourceFixture.isValid()) << sourceFixture.getError();
    ASSERT_TRUE (expected.isValid()) << expected.getError();

    const auto numSamples = sourceFixture.getNumColumns();
    ASSERT_EQ (expected.getNumColumns(), numSamples);

    std::vector<float> signal { sourceFixture.getData(), sourceFixture.getData() + numSamples };
    filter.process (signal.data(), numSamples);

    auto worstDifference = 0.0f;

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        worstDifference = std::max (worstDifference,
                                    std::abs (signal[static_cast<std::size_t> (sampleIndex)]
                                              - expected.getData()[sampleIndex]));

    EXPECT_LT (worstDifference, 1.0e-5f);
}
