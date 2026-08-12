#include "SincResampler.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace rvcara;
using Catch::Approx;

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

    /** Root mean square over an interior span, avoiding the kernel's edge behaviour. */
    double measureRootMeanSquare (const std::vector<float>& signal, double skipFraction = 0.15)
    {
        const auto first = static_cast<std::size_t> (static_cast<double> (signal.size()) * skipFraction);
        const auto last = signal.size() - first;

        double sumOfSquares = 0.0;

        for (auto index = first; index < last; ++index)
            sumOfSquares += static_cast<double> (signal[index]) * static_cast<double> (signal[index]);

        return std::sqrt (sumOfSquares / static_cast<double> (last - first));
    }

    /** Energy at a given frequency, by direct correlation against a complex exponential. */
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

        const auto count = static_cast<double> (last - first);
        return 2.0 * std::hypot (real, imaginary) / count;
    }
} // namespace

TEST_CASE ("the modified Bessel function matches known values", "[resampler]")
{
    // Underpins the Kaiser window; wrong here means a wrong window and a wrong stopband.
    CHECK (modifiedBesselI0 (0.0) == Approx (1.0));
    CHECK (modifiedBesselI0 (1.0) == Approx (1.2660658777520084).epsilon (1e-12));
    CHECK (modifiedBesselI0 (5.0) == Approx (27.239871823604442).epsilon (1e-12));
    CHECK (modifiedBesselI0 (9.4) == Approx (1595.284377456265).epsilon (1e-9));
}

TEST_CASE ("matched rates pass through untouched", "[resampler]")
{
    const SincResampler resampler { 48000.0, 48000.0 };
    CHECK (resampler.isPassThrough());

    const auto input = makeSine (1000.0, 48000.0, 512);
    const auto output = resampler.process (input.data(), static_cast<int> (input.size()));

    REQUIRE (output.size() == input.size());

    for (std::size_t index = 0; index < input.size(); ++index)
        CHECK (output[index] == input[index]);
}

TEST_CASE ("output length follows the rate ratio", "[resampler]")
{
    CHECK (SincResampler { 48000.0, 16000.0 }.getOutputLength (48000) == 16000);
    CHECK (SincResampler { 44100.0, 16000.0 }.getOutputLength (44100) == 16000);
    CHECK (SincResampler { 40000.0, 48000.0 }.getOutputLength (40000) == 48000);
    CHECK (SincResampler { 48000.0, 16000.0 }.getOutputLength (0) == 0);
}

TEST_CASE ("a tone survives downsampling with its amplitude and frequency intact", "[resampler]")
{
    // 440 Hz is well inside the 8 kHz passband of a 48 to 16 kHz conversion, so it should
    // come through at full level and at the same frequency.
    const SincResampler resampler { 48000.0, 16000.0 };

    const auto input = makeSine (440.0, 48000.0, 48000, 0.5);
    const auto output = resampler.process (input.data(), static_cast<int> (input.size()));

    REQUIRE (output.size() == 16000);

    CHECK (measureRootMeanSquare (output) == Approx (measureRootMeanSquare (input)).epsilon (0.02));
    CHECK (measureEnergyAt (output, 440.0, 16000.0) == Approx (0.5).epsilon (0.05));
}

TEST_CASE ("downsampling rejects content that would otherwise alias", "[resampler]")
{
    // The property a plain interpolator does not have, and the reason this class exists.
    // A 7 kHz tone sampled at 48 kHz would, on naive decimation to 16 kHz, fold to
    // |7000 - 16000| = 9000, then again to 7000 — but a 10 kHz tone folds to 6 kHz and
    // lands squarely in the middle of the voice band, where the content encoder would
    // read it as real.
    const SincResampler resampler { 48000.0, 16000.0 };

    const auto input = makeSine (10000.0, 48000.0, 48000, 0.5);
    const auto output = resampler.process (input.data(), static_cast<int> (input.size()));

    REQUIRE (output.size() == 16000);

    const auto aliasEnergy = measureEnergyAt (output, 6000.0, 16000.0);
    const auto totalLevel = measureRootMeanSquare (output);

    // With the tone removed before sampling, essentially nothing should remain.
    CHECK (aliasEnergy < 0.005);
    CHECK (totalLevel < 0.01);
}

TEST_CASE ("upsampling preserves a tone and adds no gross distortion", "[resampler]")
{
    // The 40 kHz to host-rate path.
    const SincResampler resampler { 40000.0, 48000.0 };

    const auto input = makeSine (1000.0, 40000.0, 40000, 0.5);
    const auto output = resampler.process (input.data(), static_cast<int> (input.size()));

    REQUIRE (output.size() == 48000);

    CHECK (measureEnergyAt (output, 1000.0, 48000.0) == Approx (0.5).epsilon (0.05));

    // No significant energy at the second or third harmonic, which is what a poor
    // interpolator would generate.
    CHECK (measureEnergyAt (output, 2000.0, 48000.0) < 0.01);
    CHECK (measureEnergyAt (output, 3000.0, 48000.0) < 0.01);
}

TEST_CASE ("degenerate inputs are handled without reading out of bounds", "[resampler]")
{
    const SincResampler resampler { 44100.0, 16000.0 };

    CHECK (resampler.process (nullptr, 100).empty());

    const std::vector<float> single { 1.0f };
    const auto output = resampler.process (single.data(), 1);

    // One input sample yields at most one output sample, and must not crash.
    CHECK (output.size() <= 1);
}
