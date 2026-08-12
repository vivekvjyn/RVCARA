#pragma once

#include <vector>

namespace rvcara
{

/** Band-limited resampling with a Kaiser-windowed sinc kernel.

    Three rate conversions happen on every conversion — the host's rate down to
    16 kHz for analysis, and the model's 40 kHz back up to the host's rate — and each
    one is a place where quality can quietly be lost.

    A plain interpolator is not sufficient. Interpolating between samples reconstructs
    the signal only if it is already band-limited to the *output* Nyquist, which when
    downsampling it is not: 48 kHz material carries content up to 24 kHz, and sampling
    that at 16 kHz folds everything above 8 kHz back down into the audible range as
    inharmonic aliases. Those aliases would land in the content encoder's input and be
    faithfully rendered into the output voice. JUCE's `WindowedSinc` interpolator has a
    fixed unity cutoff and so has exactly this problem, which is why this class exists
    instead of using it.

    A windowed sinc whose cutoff *tracks the ratio* is simultaneously the interpolator
    and the anti-alias filter, which is what a resampler should be. Downsampling by
    three narrows the kernel's passband to a third, removing the content that would
    have aliased before it is ever sampled.

    Two further properties matter for a plugin that has to line its output up with a
    timeline:

    - The kernel is symmetric, so the filter is exactly linear phase, and its delay is
      a known constant that is compensated when reading taps. Output sample @c i
      corresponds to input time @c i * ratio with no offset, so the conversion stays
      aligned with the source region.
    - It is stateless across calls. Each call sees a whole buffer, which ARA provides,
      so there is no cross-block continuity to maintain and no possibility of a state
      bug producing a click at a block boundary.

    The design follows the same rolloff and stopband choices as the `soxr` high-quality
    preset that librosa — and therefore the reference pipeline — uses, so the analysis
    signal reaching the content encoder is close to what the reference produced.
*/
class SincResampler
{
public:
    /** Fraction of the output Nyquist the passband extends to.

        Below 1 so the transition band has somewhere to live. This is `soxr`'s
        high-quality value; raising it toward 1 trades stopband rejection for
        bandwidth and starts letting aliases through.
    */
    static constexpr double passbandRolloff = 0.945;

    /** Kaiser shape parameter, chosen for roughly 96 dB of stopband rejection —
        below the noise floor of any material this will process.
    */
    static constexpr double kaiserBeta = 9.4;

    /** Zero crossings of the sinc either side of centre.

        Sets the kernel length and so the transition width. Thirty-two gives a
        transition of well under a semitone at the corner, at a cost of 64 multiplies
        per output sample — immaterial next to the neural networks downstream.
    */
    static constexpr int numZeroCrossings = 32;

    /** Constructs a resampler for one fixed rate ratio.

        The kernel is built once here rather than per call, because a conversion
        resamples the same way for every chunk of a region.

        @param sourceSampleRate  Rate of the incoming signal, in hertz.
        @param targetSampleRate  Rate to produce, in hertz.
    */
    SincResampler (double sourceSampleRate, double targetSampleRate);

    /** Resamples a whole signal.

        @param source        Input samples.
        @param numSamples    How many to read.
        @returns             The resampled signal, of length
                             `round(numSamples * targetSampleRate / sourceSampleRate)`.
    */
    [[nodiscard]] std::vector<float> process (const float* source, int numSamples) const;

    /** @returns The output length this resampler produces for a given input length. */
    [[nodiscard]] int getOutputLength (int numSamples) const noexcept;

    /** @returns True when source and target rates match, in which case process() copies. */
    [[nodiscard]] bool isPassThrough() const noexcept { return isUnity; }

private:
    /** Evaluates the windowed sinc at a distance, in input samples, from a tap centre. */
    [[nodiscard]] float kernelAt (double distanceInInputSamples) const noexcept;

    double ratio { 1.0 };          // input samples advanced per output sample
    double cutoff { 1.0 };         // passband edge as a fraction of input Nyquist
    double kernelHalfWidth { 0.0 }; // in input samples
    bool isUnity { true };

    // The kernel sampled on a fine grid, so a tap is a table lookup with linear
    // interpolation rather than a sinc and a Bessel evaluation per multiply.
    std::vector<float> kernelTable;
    double tableEntriesPerInputSample { 0.0 };
};

/** Zeroth-order modified Bessel function of the first kind.

    Exposed because the Kaiser window is defined in terms of it and the unit tests
    check it against known values; there is no other reason for it to be public.

    @param x  Argument; the series converges quickly for the range a Kaiser window needs.
    @returns  I0(x).
*/
double modifiedBesselI0 (double x) noexcept;

} // namespace rvcara
