"""Check exported assets against the reference implementation.

An export can fail in two ways that are easy to miss. A graph can trace
successfully but with a constant folded in that should have stayed dynamic, so it
produces plausible audio of the wrong length or pitch. Or a front-end constant can
be transcribed wrongly, so it produces the right shape and the wrong voice. Both
survive a shape check, so this compares audio.

Bit-exactness is not the target and cannot be: the vocoder's excitation is
stochastic, and the reference draws its noise from torch's generator while the
plugin draws from a seeded ONNX operator. Two renders of the same input by the
reference itself differ. What must match is everything else — pitch contour,
spectral envelope, timing and level — so those are measured directly.
"""

from __future__ import annotations

import logging
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .pipeline import ConversionSettings, VoiceModel, log_mel_spectrogram

logger = logging.getLogger(__name__)

# Tolerances. Chosen to be comfortably inside the run-to-run variation of the
# reference itself while still failing on a real transcription error, which shows
# up as a whole-number semitone offset or a grossly different spectral envelope.
MAXIMUM_LOG_MEL_DISTANCE = 1.0
MAXIMUM_PITCH_ERROR_CENTS = 50.0
MINIMUM_PITCH_CORRELATION = 0.95
MAXIMUM_DURATION_ERROR_SECONDS = 0.05

ANALYSIS_SAMPLE_RATE = 16_000


@dataclass(slots=True)
class ComparisonResult:
    """How closely one converted file matched its reference render."""

    name: str
    logMelDistance: float
    pitchErrorCents: float
    pitchCorrelation: float
    durationErrorSeconds: float

    @property
    def hasPassed(self) -> bool:
        return (
            self.logMelDistance <= MAXIMUM_LOG_MEL_DISTANCE
            and self.pitchErrorCents <= MAXIMUM_PITCH_ERROR_CENTS
            and self.pitchCorrelation >= MINIMUM_PITCH_CORRELATION
            and abs(self.durationErrorSeconds) <= MAXIMUM_DURATION_ERROR_SECONDS
        )

    def format_row(self) -> str:
        verdict = "pass" if self.hasPassed else "FAIL"
        return (
            f"  {self.name:<40} {verdict:>4}"
            f"  logmel {self.logMelDistance:5.2f}"
            f"  pitch {self.pitchErrorCents:6.1f} cents"
            f"  r {self.pitchCorrelation:5.3f}"
            f"  dt {self.durationErrorSeconds:+.3f} s"
        )


def load_mono(path: Path, sampleRate: int) -> np.ndarray:
    """Read an audio file as mono at the requested rate.

    :param path: File to read.
    :param sampleRate: Target rate in hertz.
    :return: Float32 mono signal.
    """
    import librosa
    import soundfile

    audio, fileSampleRate = soundfile.read(str(path), dtype="float32", always_2d=True)
    mono = audio.mean(axis=1)

    if fileSampleRate != sampleRate:
        mono = librosa.resample(mono, orig_sr=fileSampleRate, target_sr=sampleRate)

    return np.ascontiguousarray(mono, dtype=np.float32)


def measure_pitch(audio: np.ndarray, model: VoiceModel) -> np.ndarray:
    """Return a pitch track in cents, with unvoiced frames as NaN.

    Cents rather than hertz so the error metric is perceptually even across the
    range; NaN rather than zero so unvoiced frames drop out of the comparison
    instead of dragging the mean toward an arbitrary floor.
    """
    from .pipeline import decode_salience

    frontEnd = model.manifest["pitchFrontEnd"]
    frameCountMultiple = int(model.manifest["pitchDecoder"]["frameCountMultiple"])

    logMel = log_mel_spectrogram(audio, model)
    numFrames = logMel.shape[1]
    remainder = -numFrames % frameCountMultiple
    if remainder:
        logMel = np.pad(logMel, ((0, 0), (0, remainder)), mode="constant")

    inputName = model.pitchEstimator.get_inputs()[0].name
    salience = model.pitchEstimator.run(None, {inputName: logMel[None, :, :]})[0][0][:numFrames]

    hz = decode_salience(salience, model).astype(np.float64)
    cents = np.full_like(hz, np.nan)
    isVoiced = hz > 0.0
    cents[isVoiced] = 1200.0 * np.log2(hz[isVoiced] / float(frontEnd.get("centsReferenceHz", 10.0)))
    return cents


def compare(converted: np.ndarray, reference: np.ndarray, model: VoiceModel, name: str) -> ComparisonResult:
    """Measure the difference between a conversion and a reference render.

    Both signals are level-normalised before the spectral comparison: the reference
    pipeline peak-normalises and quantises to 16-bit on the way out, which the
    plugin deliberately does not do, and that gain difference is not a defect.

    :param converted: Our conversion, at :data:`ANALYSIS_SAMPLE_RATE`.
    :param reference: The reference render, at :data:`ANALYSIS_SAMPLE_RATE`.
    :param model: Supplies the analysis front end.
    :param name: Label for the report.
    :return: The measured result.
    """
    durationErrorSeconds = (len(converted) - len(reference)) / ANALYSIS_SAMPLE_RATE

    numSamples = min(len(converted), len(reference))
    left = converted[:numSamples].astype(np.float64)
    right = reference[:numSamples].astype(np.float64)

    def normalise(signal: np.ndarray) -> np.ndarray:
        rootMeanSquare = np.sqrt(np.mean(np.square(signal)))
        return signal / max(rootMeanSquare, 1e-9)

    left = normalise(left)
    right = normalise(right)

    leftLogMel = log_mel_spectrogram(left.astype(np.float32), model)
    rightLogMel = log_mel_spectrogram(right.astype(np.float32), model)
    numFrames = min(leftLogMel.shape[1], rightLogMel.shape[1])
    logMelDistance = float(
        np.mean(np.abs(leftLogMel[:, :numFrames] - rightLogMel[:, :numFrames]))
    )

    leftCents = measure_pitch(left.astype(np.float32), model)
    rightCents = measure_pitch(right.astype(np.float32), model)
    numPitchFrames = min(len(leftCents), len(rightCents))
    bothVoiced = ~np.isnan(leftCents[:numPitchFrames]) & ~np.isnan(rightCents[:numPitchFrames])

    if bothVoiced.sum() >= 2:
        leftVoiced = leftCents[:numPitchFrames][bothVoiced]
        rightVoiced = rightCents[:numPitchFrames][bothVoiced]
        pitchErrorCents = float(np.mean(np.abs(leftVoiced - rightVoiced)))
        pitchCorrelation = float(np.corrcoef(leftVoiced, rightVoiced)[0, 1])
    else:
        pitchErrorCents = float("inf")
        pitchCorrelation = 0.0

    return ComparisonResult(
        name=name,
        logMelDistance=logMelDistance,
        pitchErrorCents=pitchErrorCents,
        pitchCorrelation=pitchCorrelation,
        durationErrorSeconds=durationErrorSeconds,
    )


def find_reference_pairs(rvc_root: Path, name: str) -> list[tuple[Path, Path]]:
    """Locate the source and reference-render pairs the RVC CLI left behind.

    :param rvc_root: The RVC repository root.
    :param name: Voice name.
    :return: ``(source, render)`` pairs, sorted by name.
    """
    outputDir = rvc_root.expanduser() / "output" / name
    if not outputDir.is_dir():
        return []

    pairs = []
    for render in sorted(outputDir.glob(f"*__as_{name}.wav")):
        stem = render.name[: -len(f"__as_{name}.wav")]
        source = next(iter(sorted(outputDir.glob(f"{stem}__source.*"))), None)
        if source is not None:
            pairs.append((source, render))

    return pairs


def verify_export(
    model_dir: Path, *, reference_audio: Path | None, rvc_root: Path
) -> int:
    """Convert reference material through the exported assets and report the match.

    :param model_dir: Directory holding the exported assets.
    :param reference_audio: A single file to convert, or ``None`` to use the RVC
        project's own renders as both input and ground truth.
    :param rvc_root: The RVC repository root.
    :return: Process exit status; 0 when every comparison passes.
    """
    from .pipeline import convert

    model = VoiceModel(model_dir)
    settings = ConversionSettings(**model.manifest["defaults"])

    outputDir = model_dir.parent.parent.parent / "output" / model.manifest["name"]
    outputDir.mkdir(parents=True, exist_ok=True)

    if reference_audio is not None:
        pairs: list[tuple[Path, Path | None]] = [(reference_audio, None)]
    else:
        pairs = list(find_reference_pairs(rvc_root, model.manifest["name"]))
        if not pairs:
            print(
                f"error: no reference renders under {rvc_root}/output/{model.manifest['name']}; "
                "pass an audio file to --verify instead",
                file=sys.stderr,
            )
            return 1

    print(f"\nVerifying {len(pairs)} file(s) against the reference pipeline:")

    results = []
    for source, render in pairs:
        sourceAudio = load_mono(source, ANALYSIS_SAMPLE_RATE)
        logger.info("converting %s (%.1f s)", source.name, len(sourceAudio) / ANALYSIS_SAMPLE_RATE)

        converted = convert(sourceAudio, model, settings)

        import soundfile

        writtenPath = outputDir / f"{source.stem}__rvcara.wav"
        soundfile.write(str(writtenPath), converted, model.modelSampleRate)

        if render is None:
            print(f"  {source.name:<40} wrote {writtenPath.name} (no reference to compare)")
            continue

        convertedAtAnalysisRate = _resample(converted, model.modelSampleRate, ANALYSIS_SAMPLE_RATE)
        referenceAudio = load_mono(render, ANALYSIS_SAMPLE_RATE)

        result = compare(convertedAtAnalysisRate, referenceAudio, model, source.name)
        results.append(result)
        print(result.format_row())

    if not results:
        return 0

    numFailed = sum(1 for result in results if not result.hasPassed)
    print(
        f"\n{len(results) - numFailed}/{len(results)} matched the reference "
        f"(log-mel <= {MAXIMUM_LOG_MEL_DISTANCE}, pitch <= {MAXIMUM_PITCH_ERROR_CENTS} cents)"
    )
    return 1 if numFailed else 0


def _resample(audio: np.ndarray, fromSampleRate: int, toSampleRate: int) -> np.ndarray:
    """Resample with librosa, matching what the reference pipeline uses."""
    if fromSampleRate == toSampleRate:
        return audio

    import librosa

    return librosa.resample(audio, orig_sr=fromSampleRate, target_sr=toSampleRate)
