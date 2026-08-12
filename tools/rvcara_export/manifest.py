"""The manifest: the contract between this exporter and the C++ engine.

Every number the engine needs in order to reproduce the reference pipeline lives
here rather than in C++. That costs a JSON parse at load time and buys two things:
a model trained at a different sample rate, feature width or hop size loads
without a plugin release, and the pipeline's constants are all readable in one
place instead of scattered across a dozen translation units.

The engine refuses a manifest whose ``schemaVersion`` it does not know, so adding
a required field is a breaking change and must bump the version.
"""

from __future__ import annotations

import json
import platform
import subprocess
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

MANIFEST_SCHEMA_VERSION = 1
MANIFEST_FILENAME = "manifest.json"


@dataclass(slots=True)
class ModelManifest:
    """A voice model's complete description.

    :ivar name: Voice name, shown in the plugin's model browser.
    :ivar modelSampleRate: Rate the vocoder synthesises at, in hertz.
    :ivar featureDim: Width of the content-encoder output.
    :ivar numSpeakers: Size of the speaker embedding table.
    :ivar speakerId: Which speaker to synthesise; single-speaker models use 0.
    :ivar upsampleFactor: Output samples per conditioning frame.
    :ivar graphs: Interface description per ONNX graph, keyed by role.
    :ivar contentEncoder: Sample and frame rates of the content front end.
    :ivar pitchFrontEnd: Spectrogram parameters for the pitch estimator.
    :ivar pitchDecoder: Constants turning salience into hertz and into coarse bins.
    :ivar retrieval: Codebook location and search parameters.
    :ivar chunking: How long inputs are split and how much context each chunk gets.
    :ivar highPassFilter: The input filter applied before analysis.
    :ivar defaults: Initial values for the user-facing conversion settings.
    :ivar provenance: Where this model came from and what produced the assets.
    """

    name: str
    modelSampleRate: int
    featureDim: int
    numSpeakers: int
    speakerId: int
    upsampleFactor: int
    graphs: dict[str, Any] = field(default_factory=dict)
    contentEncoder: dict[str, Any] = field(default_factory=dict)
    pitchFrontEnd: dict[str, Any] = field(default_factory=dict)
    pitchDecoder: dict[str, Any] = field(default_factory=dict)
    retrieval: dict[str, Any] = field(default_factory=dict)
    chunking: dict[str, Any] = field(default_factory=dict)
    highPassFilter: dict[str, Any] = field(default_factory=dict)
    defaults: dict[str, Any] = field(default_factory=dict)
    provenance: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        """Return the manifest as the mapping written to disk."""
        return {
            "schemaVersion": MANIFEST_SCHEMA_VERSION,
            "name": self.name,
            "modelSampleRate": self.modelSampleRate,
            "featureDim": self.featureDim,
            "numSpeakers": self.numSpeakers,
            "speakerId": self.speakerId,
            "upsampleFactor": self.upsampleFactor,
            "graphs": self.graphs,
            "contentEncoder": self.contentEncoder,
            "pitchFrontEnd": self.pitchFrontEnd,
            "pitchDecoder": self.pitchDecoder,
            "retrieval": self.retrieval,
            "chunking": self.chunking,
            "highPassFilter": self.highPassFilter,
            "defaults": self.defaults,
            "provenance": self.provenance,
        }

    def write(self, destination_dir: Path) -> Path:
        """Write ``manifest.json`` into ``destination_dir`` and return its path."""
        destination = destination_dir / MANIFEST_FILENAME
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(self.to_dict(), indent=2) + "\n", encoding="utf-8")
        return destination


# Long-input splitting. The reference pipeline reflects context into the input,
# estimates pitch over the whole thing, and — past a length threshold — cuts the
# audio at local energy minima so that a seam never lands mid-phoneme. The values
# are upstream's float32 settings (x_pad, x_query, x_center, x_max).
CONTEXT_PADDING_SECONDS = 3.0
SPLIT_SEARCH_RADIUS_SECONDS = 10.0
CHUNK_STRIDE_SECONDS = 60.0
MAXIMUM_CHUNK_SECONDS = 65.0

# The input high-pass. A fifth-order Butterworth at 48 Hz, applied forwards and
# backwards so the result is zero-phase: pitch estimation is sensitive to group
# delay, and a one-directional filter would smear onsets. Coefficients are
# generated at export time for the fixed 16 kHz analysis rate.
HIGH_PASS_ORDER = 5
HIGH_PASS_CUTOFF_HZ = 48.0


def build_high_pass_description(sample_rate: int) -> dict[str, Any]:
    """Return the high-pass filter's coefficients and shape.

    Shipping the coefficients rather than a cutoff means the engine does not have
    to reimplement Butterworth design and cannot round it differently to SciPy.

    :param sample_rate: Analysis rate the filter runs at, in hertz.
    :return: Manifest entry with the transfer function's numerator and denominator.
    """
    from scipy import signal

    numerator, denominator = signal.butter(
        N=HIGH_PASS_ORDER, Wn=HIGH_PASS_CUTOFF_HZ, btype="high", fs=sample_rate
    )

    return {
        "order": HIGH_PASS_ORDER,
        "cutoffHz": HIGH_PASS_CUTOFF_HZ,
        "sampleRate": sample_rate,
        "isZeroPhase": True,
        "numerator": [float(value) for value in numerator],
        "denominator": [float(value) for value in denominator],
    }


def build_chunking_description() -> dict[str, Any]:
    """Return the long-input splitting parameters."""
    return {
        "contextPaddingSeconds": CONTEXT_PADDING_SECONDS,
        "splitSearchRadiusSeconds": SPLIT_SEARCH_RADIUS_SECONDS,
        "chunkStrideSeconds": CHUNK_STRIDE_SECONDS,
        "maximumChunkSeconds": MAXIMUM_CHUNK_SECONDS,
    }


def build_defaults_description() -> dict[str, Any]:
    """Return the initial conversion settings.

    These match ``config.yaml`` in the RVC project so a render from the plugin and a
    render from the CLI start out sounding the same.
    """
    return {
        "pitchShiftSemitones": 0.0,
        "retrievalRatio": 0.75,
        "consonantProtection": 0.33,
        "envelopeFollowRatio": 0.0,
        "latentNoiseSeed": 1,
    }


def build_provenance_description(
    *,
    weights_path: Path,
    index_path: Path | None,
    checkpoint_info: str,
    architecture: dict[str, Any],
    rvc_commit: str | None,
) -> dict[str, Any]:
    """Record what these assets were built from.

    A converted asset directory is otherwise anonymous, and "which checkpoint is
    this" becomes unanswerable the moment two voices are trained.

    :param weights_path: The source ``.pth``.
    :param index_path: The source ``.index``, if the model has one.
    :param checkpoint_info: The checkpoint's own training note.
    :param architecture: The checkpoint's config, as named fields.
    :param rvc_commit: Commit of the upstream RVC tree used for the export.
    :return: Manifest provenance entry.
    """
    from . import __version__

    return {
        "exporterVersion": __version__,
        "exportedAt": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "host": f"{platform.system()} {platform.machine()}",
        "sourceWeights": weights_path.name,
        "sourceIndex": index_path.name if index_path is not None else None,
        "trainingInfo": checkpoint_info,
        "rvcCommit": rvc_commit,
        "architecture": architecture,
    }


def describe_git_commit(repository_root: Path) -> str | None:
    """Return the short commit of a git working tree, or ``None`` if unavailable."""
    try:
        completed = subprocess.run(
            ["git", "-C", str(repository_root), "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            check=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return None

    return completed.stdout.strip() or None
