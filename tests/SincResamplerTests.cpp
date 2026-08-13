#include "dsp/SincResampler.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

using namespace rvcara;

namespace
{
    std::vector<float> makeSine (double frequencyHz, double sampleRate, int numSamples, double amplitude = 1.0)
    {
        std::vector<float> signal (static_cast<std::size_t> (numSamples));

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
            signal[static_cast<std::size_t> (sampleIndex)] =
                static_cast<float> (amplitude * std::sin (2.0 * std::numbers::pi * frequencyHz
                                                          * static_cast<double> (sampleIndex) / sampleRate));

        return signal;
    }

    double measureLevel (const std::vector<float>& signal)
    {
        const auto first = signal.size() / 8;
        const auto last = signal.size() - first;

        double sumOfSquares = 0.0;

        for (auto index = first; index < last; ++index)
            sumOfSquares += static_cast<double> (signal[index]) * static_cast<double> (signal[index]);

        return std::sqrt (sumOfSquares / static_cast<double> (last - first));
    }

    double measureEnergyAt (const std::vector<float>& signal, double frequencyHz, double sampleRate)
    {
        double real = 0.0;
        double imaginary = 0.0;

        const auto first = signal.size() / 8;
        const auto last = signal.size() - first;

        for (auto index = first; index < last; ++index)
        {
            const auto phase = 2.0 * std::numbers::pi * frequencyHz * static_cast<double> (index) / sampleRate;
            real += static_cast<double> (signal[index]) * std::cos (phase);
            imaginary += static_cast<double> (signal[index]) * std::sin (phase);
        }

        return 2.0 * std::hypot (real, imaginary) / static_cast<double> (last - first);
    }
} // namespace

TEST (SincResampler, TheModifiedBesselFunctionMatchesKnownValues)
{
    EXPECT_NEAR (modifiedBesselI0 (0.0), 1.0, 1.0e-12);
    EXPECT_NEAR (modifiedBesselI0 (1.0), 1.2660658777520084, 1.0e-12);
    EXPECT_NEAR (modifiedBesselI0 (5.0), 27.239871823604442, 1.0e-11);
    EXPECT_NEAR (modifiedBesselI0 (9.4), 1595.284377456265, 1.0e-6);
}

TEST (SincResampler, MatchedRatesPassThroughUntouched)
{
    const SincResampler resampler { 48000.0, 48000.0 };
    EXPECT_TRUE (resampler.isPassThrough());

    const auto input = makeSine (1000.0, 48000.0, 512);
    const auto output = resampler.process (input.data(), static_cast<int> (input.size()));

    ASSERT_EQ (output.size(), input.size());
    EXPECT_EQ (output, input);
}

TEST (SincResampler, OutputLengthFollowsTheRateRatio)
{
    EXPECT_EQ (SincResampler ({ 48000.0, 16000.0 }).getOutputLength (48000), 16000);
    EXPECT_EQ (SincResampler ({ 44100.0, 16000.0 }).getOutputLength (44100), 16000);
    EXPECT_EQ (SincResampler ({ 40000.0, 48000.0 }).getOutputLength (40000), 48000);
    EXPECT_EQ (SincResampler ({ 48000.0, 16000.0 }).getOutputLength (0), 0);
}

TEST (SincResampler, AToneSurvivesDownsamplingWithAmplitudeAndFrequencyIntact)
{
    const SincResampler resampler { 48000.0, 16000.0 };

    const auto input = makeSine (440.0, 48000.0, 48000, 0.5);
    const auto output = resampler.process (input.data(), static_cast<int> (input.size()));

    ASSERT_EQ (output.size(), 16000u);
    EXPECT_NEAR (measureLevel (output), measureLevel (input), measureLevel (input) * 0.02);
    EXPECT_NEAR (measureEnergyAt (output, 440.0, 16000.0), 0.5, 0.025);
}

TEST (SincResampler, DownsamplingRejectsContentThatWouldOtherwiseAlias)
{
    const SincResampler resampler { 48000.0, 16000.0 };

    const auto input = makeSine (10000.0, 48000.0, 48000, 0.5);
    const auto output = resampler.process (input.data(), static_cast<int> (input.size()));

    ASSERT_EQ (output.size(), 16000u);
    EXPECT_LT (measureEnergyAt (output, 6000.0, 16000.0), 0.005);
    EXPECT_LT (measureLevel (output), 0.01);
}

TEST (SincResampler, UpsamplingPreservesAToneAndAddsNoGrossDistortion)
{
    const SincResampler resampler { 40000.0, 48000.0 };

    const auto input = makeSine (1000.0, 40000.0, 40000, 0.5);
    const auto output = resampler.process (input.data(), static_cast<int> (input.size()));

    ASSERT_EQ (output.size(), 48000u);
    EXPECT_NEAR (measureEnergyAt (output, 1000.0, 48000.0), 0.5, 0.025);
    EXPECT_LT (measureEnergyAt (output, 2000.0, 48000.0), 0.01);
    EXPECT_LT (measureEnergyAt (output, 3000.0, 48000.0), 0.01);
}

TEST (SincResampler, DegenerateInputsAreHandledWithoutReadingOutOfBounds)
{
    const SincResampler resampler { 44100.0, 16000.0 };

    EXPECT_TRUE (resampler.process (nullptr, 100).empty());

    const std::vector<float> single { 1.0f };
    EXPECT_LE (resampler.process (single.data(), 1).size(), 1u);
}
