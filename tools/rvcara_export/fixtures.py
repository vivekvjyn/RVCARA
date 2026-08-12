"""Generate the cross-language fixtures the C++ unit tests check themselves against.

The engine's DSP front end is a transcription of NumPy and SciPy code, and a
transcription is exactly the kind of thing that looks right and is wrong by a
factor of two, an off-by-one frame, or a symmetric window where a periodic one
belongs. Analytic tests catch some of that; they do not catch "this is a valid
spectrogram of the wrong thing".

So the reference implementation writes its own output to disk, and the C++ tests
assert against it. Both sides read the same container format the model assets use,
which incidentally exercises that reader too.

Run with ``python -m rvcara_export.fixtures``.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

from .binary_matrix import write_matrix
from .manifest import build_high_pass_description
from .pitch_estimator import (
    FFT_SIZE,
    HOP_SIZE,
    MAGNITUDE_FLOOR,
    NUM_MEL_BINS,
    PITCH_SAMPLE_RATE,
    WINDOW_SIZE,
    build_mel_filter_bank,
)

# A fixed length and seed so the fixture is reproducible. Not a multiple of the hop, so
# an off-by-one in the frame count has somewhere to show up.
#
# Two seconds rather than a quarter of one, deliberately. A 48 Hz high-pass at 16 kHz has
# poles at a radius of about 0.988, so it settles over several hundred samples; on a very
# short signal the whole thing is start-up transient and edge handling dominates
# everywhere, which makes the fixture a test of boundary conventions rather than of the
# filter. Two seconds leaves a genuine steady-state interior to compare.
NUM_FIXTURE_SAMPLES = 32_099
FIXTURE_SEED = 4_099


def build_test_signal() -> np.ndarray:
    """Return a deterministic signal with broadband, tonal and low-frequency content.

    All three matter: broadband noise exercises every mel bin, the tones give the
    spectrogram something whose position can be reasoned about, and the 25 Hz
    component is below the high-pass corner so the filter has something to remove.
    """
    generator = np.random.default_rng(FIXTURE_SEED)
    time = np.arange(NUM_FIXTURE_SAMPLES, dtype=np.float64) / PITCH_SAMPLE_RATE

    signal = 0.05 * generator.standard_normal(NUM_FIXTURE_SAMPLES)
    signal += 0.40 * np.sin(2.0 * np.pi * 220.0 * time)
    signal += 0.20 * np.sin(2.0 * np.pi * 1_760.0 * time)
    # Rumble below the high-pass corner, at a level a real recording might carry rather
    # than the dominant component — at 0.3 the filter's job would swamp everything else.
    signal += 0.02 * np.sin(2.0 * np.pi * 25.0 * time)

    return signal.astype(np.float32)


def periodic_hann_window(window_size: int) -> np.ndarray:
    """The periodic Hann window, matching torch and SciPy."""
    n = np.arange(window_size, dtype=np.float64)
    return (0.5 - 0.5 * np.cos(2.0 * np.pi * n / window_size)).astype(np.float64)


def reference_log_mel(signal: np.ndarray) -> np.ndarray:
    """Compute the log-mel spectrogram the way the pitch estimator was trained on.

    :param signal: Mono signal at 16 kHz.
    :return: ``[numMelBins, numFrames]`` natural-log mel magnitudes.
    """
    padded = np.pad(signal.astype(np.float64), (FFT_SIZE // 2, FFT_SIZE // 2), mode="reflect")
    num_frames = 1 + len(signal) // HOP_SIZE

    window = periodic_hann_window(WINDOW_SIZE)
    starts = np.arange(num_frames) * HOP_SIZE
    frames = padded[starts[:, None] + np.arange(FFT_SIZE)[None, :]] * window

    magnitude = np.abs(np.fft.rfft(frames, n=FFT_SIZE, axis=1)).T
    mel_magnitude = build_mel_filter_bank().astype(np.float64) @ magnitude

    return np.log(np.maximum(mel_magnitude, MAGNITUDE_FLOOR)).astype(np.float32)


def reference_high_pass(signal: np.ndarray) -> np.ndarray:
    """Apply the high-pass exactly as the reference pipeline does."""
    from scipy import signal as scipy_signal

    description = build_high_pass_description(PITCH_SAMPLE_RATE)

    return scipy_signal.filtfilt(
        np.asarray(description["numerator"], dtype=np.float64),
        np.asarray(description["denominator"], dtype=np.float64),
        signal.astype(np.float64),
    ).astype(np.float32)


def write_fixtures(destination_dir: Path) -> list[Path]:
    """Write every fixture and return the paths written."""
    destination_dir.mkdir(parents=True, exist_ok=True)

    signal = build_test_signal()
    written = []

    for name, matrix in (
        ("test_signal.bin", signal[None, :]),
        ("expected_log_mel.bin", reference_log_mel(signal)),
        ("expected_high_pass.bin", reference_high_pass(signal)[None, :]),
        ("mel_filter_bank.bin", build_mel_filter_bank()),
    ):
        path = destination_dir / name
        write_matrix(path, matrix)
        written.append(path)
        print(f"  {name:<24} {matrix.shape}")

    return written


def main() -> int:
    """Write the fixtures into ``tests/fixtures`` and report."""
    destination = Path(__file__).resolve().parents[2] / "tests" / "fixtures"
    print(f"writing fixtures to {destination}")
    write_fixtures(destination)
    return 0


if __name__ == "__main__":
    sys.exit(main())
