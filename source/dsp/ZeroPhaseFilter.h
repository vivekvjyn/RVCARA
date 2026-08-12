#pragma once

#include <vector>

namespace rvcara::dsp
{

/** One second-order section of an IIR filter, in the layout the manifest ships.

    Six coefficients in SciPy's `sos` order. A section with @c b2, @c a2 both zero is
    a first-order section padded out, which is what an odd-order Butterworth design
    produces for its unpaired real pole.
*/
struct BiquadCoefficients
{
    double b0 { 1.0 };
    double b1 { 0.0 };
    double b2 { 0.0 };
    double a0 { 1.0 };
    double a1 { 0.0 };
    double a2 { 0.0 };
};

/** A cascade of biquads applied forwards and then backwards, giving zero phase.

    The input high-pass exists to keep rumble and DC drift out of pitch estimation.
    It has to be zero-phase: a one-directional filter's group delay varies with
    frequency, which would shift the pitch track relative to the content features
    that are meant to line up with it, smearing consonant onsets by a few
    milliseconds in a way that survives all the way to the output.

    Filtering forwards and then backwards over the reversed signal squares the
    magnitude response and cancels the phase exactly. Two consequences follow, both
    deliberate:

    - The realised magnitude response is the square of the designed one, so the
      48 Hz corner is steeper than fifth-order. This matches the reference
      implementation, which is what matters.
    - It is not causal, so it cannot run in a streaming context. That is not a
      constraint here; ARA hands the plugin the whole region up front.

    The signal is extended by odd reflection before filtering and trimmed after, so
    the filter's start-up transient plays out in material that gets discarded rather
    than in the first milliseconds of the region.

    Implemented as a transposed direct form II per section. A fifth-order direct-form
    filter would be poorly conditioned; per-section state keeps each pole pair in a
    range single precision handles comfortably. State is `double` regardless of the
    sample type for the same reason.
*/
class ZeroPhaseFilter
{
public:
    ZeroPhaseFilter() = default;

    /** @param sections   The cascade, outermost first; order is irrelevant to the result.
        @param padLength  Samples of odd extension added at each end before filtering.
    */
    ZeroPhaseFilter (std::vector<BiquadCoefficients> sections, int padLength);

    /** Filters a signal in place.

        @param samples     The signal; modified in place.
        @param numSamples  Its length. Signals shorter than the pad length are left
                           untouched, because the odd extension would read past the
                           ends and such a signal is far too short to convert anyway.
    */
    void process (float* samples, int numSamples) const;

    /** @returns The samples of odd extension used at each end. */
    [[nodiscard]] int getPadLength() const noexcept { return padding; }

    /** @returns Whether any sections were configured. */
    [[nodiscard]] bool isEmpty() const noexcept { return cascade.empty(); }

private:
    /** Runs the cascade once over a buffer, forwards. */
    void processForward (double* samples, int numSamples) const;

    std::vector<BiquadCoefficients> cascade;
    int padding { 0 };
};

} // namespace rvcara::dsp
