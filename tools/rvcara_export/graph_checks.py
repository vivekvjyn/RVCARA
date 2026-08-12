"""Confirm that exported graphs really accept variable-length input.

Tracing a network whose control flow reads a tensor's shape can silently produce a
graph that only works for the length used during tracing. It is a quiet failure:
the reshape targets stay dynamic, so what surfaces is not "wrong input size" but an
off-by-one in some padding width, reported as a reshape error deep inside an
attention layer — and only for inputs that differ from the example.

Every export therefore runs each graph at several lengths, including deliberately
awkward ones, and refuses to write a manifest if any of them fail. The cost is a
few seconds; the alternative is shipping a voice that renders four-second regions
and crashes on five-second ones.
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import onnxruntime as ort

logger = logging.getLogger(__name__)

# Deliberately not round numbers, and not multiples of each other: a graph that
# happens to work for powers of two can still be broken.
CONTENT_SAMPLE_COUNTS = (4_000, 16_000, 21_777)
PITCH_FRAME_COUNTS = (32, 96, 512)
VOCODER_FRAME_COUNTS = (12, 97, 301)


class GraphCheckFailed(RuntimeError):
    """Raised when a graph rejects a length it must accept."""


@dataclass(slots=True)
class GraphCheck:
    """One length that a graph was run at."""

    graph: str
    length: int
    outputShape: tuple[int, ...] | None
    error: str | None

    @property
    def hasPassed(self) -> bool:
        return self.error is None


def _open(path: Path) -> ort.InferenceSession:
    options = ort.SessionOptions()
    options.log_severity_level = 3  # errors only; a failed check is expected here
    return ort.InferenceSession(str(path), options, providers=["CPUExecutionProvider"])


def check_content_encoder(path: Path) -> list[GraphCheck]:
    """Run the content encoder at several input lengths."""
    session = _open(path)
    inputName = session.get_inputs()[0].name
    checks = []

    for numSamples in CONTENT_SAMPLE_COUNTS:
        try:
            output = session.run(None, {inputName: np.zeros((1, numSamples), np.float32)})[0]
            checks.append(GraphCheck("contentEncoder", numSamples, output.shape, None))
        except Exception as error:
            checks.append(GraphCheck("contentEncoder", numSamples, None, str(error)[:200]))

    return checks


def check_pitch_estimator(path: Path, *, numMelBins: int) -> list[GraphCheck]:
    """Run the pitch estimator at several frame counts."""
    session = _open(path)
    inputName = session.get_inputs()[0].name
    checks = []

    for numFrames in PITCH_FRAME_COUNTS:
        try:
            feed = np.zeros((1, numMelBins, numFrames), np.float32)
            output = session.run(None, {inputName: feed})[0]
            error = None if output.shape[1] == numFrames else f"returned {output.shape[1]} frames"
            checks.append(GraphCheck("pitchEstimator", numFrames, output.shape, error))
        except Exception as error:
            checks.append(GraphCheck("pitchEstimator", numFrames, None, str(error)[:200]))

    return checks


def check_vocoder(
    path: Path, *, featureDim: int, latentDim: int, upsampleFactor: int, speakerId: int
) -> list[GraphCheck]:
    """Run the vocoder at several frame counts and confirm the output length scales."""
    session = _open(path)
    checks = []

    for numFrames in VOCODER_FRAME_COUNTS:
        feeds = {
            "contentFeatures": np.zeros((1, numFrames, featureDim), np.float32),
            "numFrames": np.array([numFrames], np.int64),
            "coarsePitch": np.full((1, numFrames), 128, np.int64),
            "fundamentalFrequencyHz": np.full((1, numFrames), 220.0, np.float32),
            "speakerId": np.array([speakerId], np.int64),
            "latentNoise": np.zeros((1, latentDim, numFrames), np.float32),
        }
        try:
            named = {actual.name: feeds[actual.name] for actual in session.get_inputs()}
            output = session.run(None, named)[0]
            expected = numFrames * upsampleFactor
            error = (
                None
                if output.shape[-1] == expected
                else f"returned {output.shape[-1]} samples, expected {expected}"
            )
            checks.append(GraphCheck("vocoder", numFrames, output.shape, error))
        except Exception as error:
            checks.append(GraphCheck("vocoder", numFrames, None, str(error)[:200]))

    return checks


def report(checks: list[GraphCheck]) -> None:
    """Log every check and raise if any failed.

    :param checks: Results to report.
    :raises GraphCheckFailed: If one or more lengths were rejected.
    """
    failures = [check for check in checks if not check.hasPassed]

    for check in checks:
        if check.hasPassed:
            logger.info("  %s at %d: %s", check.graph, check.length, check.outputShape)
        else:
            logger.error("  %s at %d: %s", check.graph, check.length, check.error)

    if failures:
        summary = ", ".join(f"{check.graph} at {check.length}" for check in failures)
        raise GraphCheckFailed(
            f"{len(failures)} graph(s) rejected a valid input length ({summary}). "
            "This usually means a tensor length was baked into the trace; see "
            "vocoder.trace_safe_relative_attention for the shape of that bug."
        )
