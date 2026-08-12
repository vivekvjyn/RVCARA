"""Export the vocoder: conditioned features and pitch in, waveform out.

This is the trained part of a voice — a VITS-style posterior flow feeding a
neural source-filter HiFi-GAN. Upstream's ``SynthesizerTrnMs768NSFsid.infer``
draws the latent noise internally; the graph takes it as an input instead, so the
plugin owns the seed and can offer a reproducible variation control.
"""

from __future__ import annotations

import contextlib
import logging
from pathlib import Path
from typing import Iterator

import torch
import torch.nn.functional as functional
from torch import nn

from .graphs import export_graph
from .rvc import VoiceCheckpoint, import_rvc_module

logger = logging.getLogger(__name__)

# The standard deviation applied to the sampled latent, hard-coded upstream.
LATENT_NOISE_SCALE = 0.66666

GRAPH_FILENAME = "vocoder.onnx"

INPUT_CONTENT_FEATURES = "contentFeatures"
INPUT_NUM_FRAMES = "numFrames"
INPUT_COARSE_PITCH = "coarsePitch"
INPUT_FUNDAMENTAL_FREQUENCY = "fundamentalFrequencyHz"
INPUT_SPEAKER_ID = "speakerId"
INPUT_LATENT_NOISE = "latentNoise"
OUTPUT_WAVEFORM = "waveform"


class VocoderGraph(nn.Module):
    """Reproduces ``SynthesizerTrnMs768NSFsid.infer`` with the noise lifted out.

    The body is a transcription of upstream's ``infer`` for the case RVCARA uses —
    pitch conditioning on, no partial-render head or tail. Keeping it a
    transcription rather than a call into ``infer`` is deliberate: ``infer`` samples
    its own latent, and a traced ``randn_like`` would leave the plugin unable to
    reproduce a render it had cached.
    """

    def __init__(self, synthesiser: nn.Module) -> None:
        super().__init__()
        self.synthesiser = synthesiser

    def forward(
        self,
        content_features: torch.Tensor,
        num_frames: torch.Tensor,
        coarse_pitch: torch.Tensor,
        fundamental_frequency_hz: torch.Tensor,
        speaker_id: torch.Tensor,
        latent_noise: torch.Tensor,
    ) -> torch.Tensor:
        """:param content_features: ``[batch, numFrames, 768]`` retrieved features.
        :param num_frames: ``[batch]`` int64 valid frame count, for the encoder mask.
        :param coarse_pitch: ``[batch, numFrames]`` int64 pitch embedding indices, 1-255.
        :param fundamental_frequency_hz: ``[batch, numFrames]`` continuous pitch,
            zero where unvoiced; drives the harmonic excitation.
        :param speaker_id: ``[batch]`` int64 index into the speaker embedding.
        :param latent_noise: ``[batch, 192, numFrames]`` standard normal samples.
        :return: ``[batch, 1, numFrames * upsampleFactor]`` waveform in ``[-1, 1]``.
        """
        synthesiser = self.synthesiser

        speakerEmbedding = synthesiser.emb_g(speaker_id).unsqueeze(-1)

        mean, logStandardDeviation, mask = synthesiser.enc_p(
            content_features, coarse_pitch, num_frames
        )
        latent = (
            mean + torch.exp(logStandardDeviation) * latent_noise * LATENT_NOISE_SCALE
        ) * mask
        latent = synthesiser.flow(latent, mask, g=speakerEmbedding, reverse=True)

        return synthesiser.dec(latent * mask, fundamental_frequency_hz, g=speakerEmbedding)


def _relative_position_to_absolute_position(self, x: torch.Tensor) -> torch.Tensor:  # noqa: ANN001
    """Trace-safe replacement for upstream's method of the same name.

    Mathematically identical to the original. The only change is that the padding
    width stays a traced value instead of being forced through ``int()``, which
    would freeze it at whatever length the example input happened to have.

    :param x: ``[batch, heads, length, 2 * length - 1]`` relative logits.
    :return: ``[batch, heads, length, length]`` absolute logits.
    """
    batch, heads, length, _ = x.size()

    x = functional.pad(x, [0, 1, 0, 0, 0, 0, 0, 0])
    flattened = x.view([batch, heads, length * 2 * length])
    flattened = functional.pad(flattened, [0, length - 1, 0, 0, 0, 0])

    return flattened.view([batch, heads, length + 1, 2 * length - 1])[
        :, :, :length, length - 1 :
    ]


def _absolute_position_to_relative_position(self, x: torch.Tensor) -> torch.Tensor:  # noqa: ANN001
    """Trace-safe replacement for upstream's method of the same name.

    :param x: ``[batch, heads, length, length]`` attention weights.
    :return: ``[batch, heads, length, 2 * length - 1]`` relative weights.
    """
    batch, heads, length, _ = x.size()

    x = functional.pad(x, [0, length - 1, 0, 0, 0, 0, 0, 0])
    flattened = x.view([batch, heads, length * length + length * (length - 1)])
    flattened = functional.pad(flattened, [length, 0, 0, 0, 0, 0])

    return flattened.view([batch, heads, length, 2 * length])[:, :, :, 1:]


def _get_relative_embeddings(self, relative_embeddings: torch.Tensor, length):  # noqa: ANN001
    """Trace-safe replacement for upstream's method of the same name.

    Upstream clamps the pad width at zero with ``max()`` and then either pads or
    does not, a branch that tracing resolves once and bakes in. Padding by a
    signed width instead removes the branch: ``torch.nn.functional.pad`` crops on a
    negative width, and cropping ``windowSize + 1 - length`` rows from each end of a
    ``2 * windowSize + 1`` row table selects exactly the rows upstream's slice would
    have. The two agree for every length, and this one stays symbolic.

    :param relative_embeddings: ``[1, 2 * windowSize + 1, channels]`` learned table.
    :param length: Sequence length, traced.
    :return: ``[1, 2 * length - 1, channels]`` slice of the table.
    """
    padWidth = length - (self.window_size + 1)
    padded = functional.pad(relative_embeddings, [0, 0, padWidth, padWidth, 0, 0])
    return padded[:, : 2 * length - 1]


@contextlib.contextmanager
def trace_safe_relative_attention() -> Iterator[None]:
    """Patch the relative-position attention for the duration of a trace.

    The vocoder's text encoder uses windowed relative-position attention, and three
    places in upstream's implementation convert a traced length to a Python integer.
    Tracing through them yields a graph that only accepts the frame count used for
    the trace: the reshape targets stay dynamic, so the failure is not a clean shape
    error but an off-by-one in a padding width, surfacing as a ``Reshape`` error deep
    in the first attention layer.

    Patching here rather than in the submodule keeps the RVC checkout pristine and
    keeps the reason for the change next to the code that needs it.
    """
    attentions = import_rvc_module("infer.module.attentions")
    attention = attentions.MultiHeadAttention

    originals = {
        "_relative_position_to_absolute_position": attention._relative_position_to_absolute_position,
        "_absolute_position_to_relative_position": attention._absolute_position_to_relative_position,
        "_get_relative_embeddings": attention._get_relative_embeddings,
    }
    replacements = {
        "_relative_position_to_absolute_position": _relative_position_to_absolute_position,
        "_absolute_position_to_relative_position": _absolute_position_to_relative_position,
        "_get_relative_embeddings": _get_relative_embeddings,
    }

    for name, replacement in replacements.items():
        setattr(attention, name, replacement)

    try:
        yield
    finally:
        for name, original in originals.items():
            setattr(attention, name, original)


def build_synthesiser(checkpoint: VoiceCheckpoint) -> nn.Module:
    """Instantiate the trained generator and strip it down for inference.

    Weight normalisation is removed and the discriminator-side posterior encoder is
    dropped: neither contributes to inference, and leaving weight normalisation in
    would export as a division per convolution on every call.

    :param checkpoint: The loaded ``.pth``.
    :return: The generator in float32 eval mode.
    """
    models = import_rvc_module("infer.module.models")

    synthesiser = models.SynthesizerTrnMs768NSFsid(*checkpoint.config, is_half=False)
    synthesiser.load_state_dict(checkpoint.weights, strict=False)

    if hasattr(synthesiser, "enc_q"):
        del synthesiser.enc_q

    synthesiser.eval().float()
    synthesiser.dec.remove_weight_norm()
    synthesiser.flow.remove_weight_norm()

    return synthesiser


def export_vocoder(
    checkpoint: VoiceCheckpoint, destination_dir: Path, *, speaker_id: int = 0
) -> Path:
    """Write ``vocoder.onnx``.

    :param checkpoint: The loaded ``.pth``.
    :param destination_dir: Directory to write into.
    :param speaker_id: Speaker index used for the example trace only; the graph
        keeps it as a run-time input.
    :return: Path to the written graph.
    """
    vocoder = VocoderGraph(build_synthesiser(checkpoint)).eval()
    destination = destination_dir / GRAPH_FILENAME

    numExampleFrames = 64
    featureDim = checkpoint.feature_dim
    latentDim = int(checkpoint.config[2])  # inter_channels

    example_inputs = (
        torch.zeros(1, numExampleFrames, featureDim, dtype=torch.float32),
        torch.tensor([numExampleFrames], dtype=torch.int64),
        torch.ones(1, numExampleFrames, dtype=torch.int64),
        torch.full((1, numExampleFrames), 220.0, dtype=torch.float32),
        torch.tensor([speaker_id], dtype=torch.int64),
        torch.zeros(1, latentDim, numExampleFrames, dtype=torch.float32),
    )

    logger.info("tracing vocoder (%d-D features, %d-D latent)", featureDim, latentDim)
    with trace_safe_relative_attention():
        export_graph(
            vocoder,
            example_inputs,
            destination,
            input_names=[
                INPUT_CONTENT_FEATURES,
                INPUT_NUM_FRAMES,
                INPUT_COARSE_PITCH,
                INPUT_FUNDAMENTAL_FREQUENCY,
                INPUT_SPEAKER_ID,
                INPUT_LATENT_NOISE,
            ],
            output_names=[OUTPUT_WAVEFORM],
            dynamic_axes={
                INPUT_CONTENT_FEATURES: {0: "batch", 1: "numFrames"},
                INPUT_NUM_FRAMES: {0: "batch"},
                INPUT_COARSE_PITCH: {0: "batch", 1: "numFrames"},
                INPUT_FUNDAMENTAL_FREQUENCY: {0: "batch", 1: "numFrames"},
                INPUT_SPEAKER_ID: {0: "batch"},
                INPUT_LATENT_NOISE: {0: "batch", 2: "numFrames"},
                OUTPUT_WAVEFORM: {0: "batch", 2: "numSamples"},
            },
        )
    return destination
