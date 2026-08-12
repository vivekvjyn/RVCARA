"""Command line entry point for the asset exporter."""

from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from . import content_encoder, graph_checks, pitch_estimator, retrieval, vocoder
from .manifest import (
    ModelManifest,
    build_chunking_description,
    build_defaults_description,
    build_high_pass_description,
    build_provenance_description,
    describe_git_commit,
)
from .onnx_utils import describe_graph, freeze_random_seeds, report_sizes
from .rvc_bridge import RvcSourceNotFound, load_voice_checkpoint, locate_rvc_tree

logger = logging.getLogger("rvcara_export")

DEFAULT_RVC_ROOT = Path.home() / "Desktop" / "RVC"

# Base seed for the vocoder's stochastic operators. Any fixed value works; this one
# is recorded so a future change is visible as a change to the assets.
RANDOM_SEED = 20_260_812


def build_argument_parser() -> argparse.ArgumentParser:
    """Return the exporter's argument parser."""
    parser = argparse.ArgumentParser(
        prog="rvcara-export",
        description="Convert a trained RVC voice into the ONNX assets RVCARA loads.",
    )
    parser.add_argument("model", help="Voice name under RVC's models/ directory, or a path to a .pth")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="Destination directory (default: assets/models/<name> in this repository)",
    )
    parser.add_argument(
        "--rvc-root",
        type=Path,
        default=DEFAULT_RVC_ROOT,
        help=f"Path to the RVC repository (default: {DEFAULT_RVC_ROOT})",
    )
    parser.add_argument(
        "--speaker-id",
        type=int,
        default=0,
        help="Speaker index to synthesise for multi-speaker checkpoints (default: 0)",
    )
    parser.add_argument(
        "--skip-retrieval",
        action="store_true",
        help="Do not export the codebook. The voice still works, with retrieval disabled.",
    )
    parser.add_argument(
        "--verify",
        nargs="?",
        const="auto",
        default=None,
        metavar="AUDIO",
        help="After exporting, convert an audio file and compare against the reference "
        "render. Pass 'auto' or no value to use RVC's own output/<name> renders.",
    )
    parser.add_argument("-v", "--verbose", action="store_true", help="Log every step")
    return parser


def resolve_model_paths(reference: str, rvc_root: Path) -> tuple[Path, Path | None]:
    """Resolve a voice name or path into weights and index paths.

    :param reference: Voice name, directory, or ``.pth`` path.
    :param rvc_root: The RVC repository root.
    :return: Weights path and index path, the latter ``None`` when absent.
    :raises FileNotFoundError: If no weights can be found.
    """
    candidate = Path(reference).expanduser()

    if candidate.is_file() and candidate.suffix == ".pth":
        weights = candidate
    elif candidate.is_dir():
        matches = sorted(candidate.glob("*.pth"))
        if not matches:
            raise FileNotFoundError(f"no .pth in {candidate}")
        weights = matches[0]
    else:
        directory = rvc_root / "models" / reference
        weights = directory / f"{reference}.pth"
        if not weights.is_file():
            available = sorted(p.name for p in (rvc_root / "models").glob("*") if p.is_dir())
            raise FileNotFoundError(
                f"no model named {reference!r} under {rvc_root / 'models'}. "
                f"Trained models: {', '.join(available) or 'none'}"
            )

    index = next(iter(sorted(weights.parent.glob("*.index"))), None)
    return weights, index


def export_model(arguments: argparse.Namespace, repository_root: Path) -> Path:
    """Run the full export and return the directory the assets were written to."""
    tree = locate_rvc_tree(arguments.rvc_root)
    weightsPath, indexPath = resolve_model_paths(arguments.model, tree.project_root)
    checkpoint = load_voice_checkpoint(weightsPath)

    if arguments.speaker_id >= checkpoint.num_speakers:
        raise ValueError(
            f"speaker id {arguments.speaker_id} is out of range for a model with "
            f"{checkpoint.num_speakers} speaker embeddings"
        )

    destination = arguments.output or (repository_root / "assets" / "models" / checkpoint.name)
    destination.mkdir(parents=True, exist_ok=True)

    logger.info("exporting %s -> %s", weightsPath.name, destination)
    logger.info(
        "  %s, %d Hz, %d-D features, %d speaker embedding(s), %sx upsampling",
        checkpoint.info or "unlabelled",
        checkpoint.sample_rate,
        checkpoint.feature_dim,
        checkpoint.num_speakers,
        checkpoint.upsample_factor,
    )

    contentPath = content_encoder.export_content_encoder(tree, destination)
    pitchPath, filterBankPath = pitch_estimator.export_pitch_estimator(tree, destination)
    vocoderPath = vocoder.export_vocoder(
        checkpoint, destination, speaker_id=arguments.speaker_id
    )

    # Only the vocoder is stochastic, but seeding all three is cheap and means a
    # future graph cannot introduce drift unnoticed.
    for path in (contentPath, pitchPath, vocoderPath):
        freeze_random_seeds(path, RANDOM_SEED)

    logger.info("checking that every graph accepts variable-length input")
    graph_checks.report(
        graph_checks.check_content_encoder(contentPath)
        + graph_checks.check_pitch_estimator(pitchPath, numMelBins=pitch_estimator.NUM_MEL_BINS)
        + graph_checks.check_vocoder(
            vocoderPath,
            featureDim=checkpoint.feature_dim,
            latentDim=int(checkpoint.config[2]),
            upsampleFactor=checkpoint.upsample_factor,
            speakerId=arguments.speaker_id,
        )
    )

    retrievalDescription: dict[str, object] = {}
    if indexPath is not None and not arguments.skip_retrieval:
        _, codebook = retrieval.export_codebook(
            indexPath, destination, expected_feature_dim=checkpoint.feature_dim
        )
        retrievalDescription = retrieval.describe_codebook(codebook)
    else:
        logger.warning(
            "no retrieval index exported; the voice will sound closer to the source performer"
        )

    manifest = ModelManifest(
        name=checkpoint.name,
        modelSampleRate=checkpoint.sample_rate,
        featureDim=checkpoint.feature_dim,
        numSpeakers=checkpoint.num_speakers,
        speakerId=arguments.speaker_id,
        upsampleFactor=checkpoint.upsample_factor,
        graphs={
            "contentEncoder": describe_graph(contentPath),
            "pitchEstimator": describe_graph(pitchPath),
            "vocoder": describe_graph(vocoderPath),
        },
        contentEncoder={
            "sampleRate": content_encoder.CONTENT_SAMPLE_RATE,
            "frameRate": content_encoder.CONTENT_FRAME_RATE,
            "minimumNumSamples": content_encoder.MINIMUM_NUM_SAMPLES,
        },
        pitchFrontEnd=pitch_estimator.describe_front_end(),
        pitchDecoder=pitch_estimator.describe_decoder(),
        retrieval=retrievalDescription,
        chunking=build_chunking_description(),
        highPassFilter=build_high_pass_description(content_encoder.CONTENT_SAMPLE_RATE),
        defaults=build_defaults_description(),
        provenance=build_provenance_description(
            weights_path=weightsPath,
            index_path=None if not retrievalDescription else indexPath,
            checkpoint_info=checkpoint.info,
            architecture=checkpoint.describe(),
            rvc_commit=describe_git_commit(tree.source_root),
        ),
    )
    manifestPath = manifest.write(destination)

    logger.info("wrote %s", manifestPath.name)
    print("\nExported assets:")
    print(
        report_sizes(
            [contentPath, pitchPath, vocoderPath, filterBankPath, manifestPath]
            + ([destination / retrieval.CODEBOOK_FILENAME] if retrievalDescription else [])
        )
    )

    return destination


def main(argv: list[str] | None = None) -> int:
    """Export a voice model, optionally verifying it, and return an exit status."""
    arguments = build_argument_parser().parse_args(argv)

    logging.basicConfig(
        level=logging.INFO if arguments.verbose else logging.WARNING,
        format="%(message)s",
        stream=sys.stderr,
    )

    repositoryRoot = Path(__file__).resolve().parents[2]

    try:
        destination = export_model(arguments, repositoryRoot)
    except (RvcSourceNotFound, FileNotFoundError, ValueError, graph_checks.GraphCheckFailed) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    if arguments.verify is not None:
        from .verify import verify_export

        return verify_export(
            destination,
            reference_audio=None if arguments.verify == "auto" else Path(arguments.verify),
            rvc_root=arguments.rvc_root,
        )

    return 0
