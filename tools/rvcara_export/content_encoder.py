"""Export the content encoder: 16 kHz audio in, phonetic features out.

RVC v2 uses the twelfth encoder layer of a ContentVec-initialised HuBERT, taken
before the final projection, giving 768 dimensions at 50 Hz. Upstream reaches it
through ``HubertModel.last_hidden_state``; this graph does the same thing so the
two cannot diverge.
"""

from __future__ import annotations

import logging
from pathlib import Path

import torch
from torch import nn

from .onnx_utils import export_graph
from .rvc_bridge import RvcTree

logger = logging.getLogger(__name__)

# The audio sample rate the encoder was trained at, and the rate at which it emits
# frames. The convolutional front end strides by 320 samples, so 16000 / 320 = 50.
CONTENT_SAMPLE_RATE = 16_000
CONTENT_FRAME_RATE = 50

# Receptive field of the convolutional front end. Anything shorter yields no
# frames at all, which is worth rejecting with a clear message rather than an
# opaque shape error deep in the graph.
MINIMUM_NUM_SAMPLES = 400

GRAPH_FILENAME = "content_encoder.onnx"
INPUT_AUDIO = "audio"
OUTPUT_FEATURES = "features"


class ContentEncoderGraph(nn.Module):
    """Wraps a HuBERT so that tracing sees one tensor in and one tensor out.

    ``HubertModel.forward`` returns a dataclass and accepts keyword-only options;
    neither survives tracing. This adapter fixes those options to the values the
    reference pipeline uses and returns the bare hidden states.
    """

    def __init__(self, hubert: nn.Module) -> None:
        super().__init__()
        self.hubert = hubert

    def forward(self, audio: torch.Tensor) -> torch.Tensor:
        """:param audio: ``[batch, numSamples]`` at 16 kHz, unnormalised.
        :return: ``[batch, numFrames, 768]`` content features.
        """
        return self.hubert(
            input_values=audio,
            attention_mask=None,
            output_hidden_states=False,
            return_dict=True,
        ).last_hidden_state


def load_content_encoder(tree: RvcTree) -> nn.Module:
    """Load the cached HuBERT in float32 eval mode.

    Attention is forced to the eager implementation: the fused scaled-dot-product
    kernels that Transformers selects by default are not traceable.

    :param tree: Resolved RVC layout.
    :return: The loaded module.
    """
    from transformers import HubertModel  # imported lazily; heavy and only needed here

    class HubertWithFinalProjection(HubertModel):
        """Mirrors upstream's subclass so the checkpoint's keys all bind.

        ``final_proj`` belongs to the v1 path and is never evaluated here, but it is
        present in the checkpoint and Transformers warns about unexpected keys
        without it.
        """

        def __init__(self, config) -> None:  # noqa: ANN001 - Transformers config type
            super().__init__(config)
            self.final_proj = nn.Linear(config.hidden_size, config.classifier_proj_size)

    model = HubertWithFinalProjection.from_pretrained(
        str(tree.hubert_dir),
        local_files_only=True,
        torch_dtype=torch.float32,
        attn_implementation="eager",
    )
    return model.eval()


def export_content_encoder(tree: RvcTree, destination_dir: Path) -> Path:
    """Write ``content_encoder.onnx``.

    :param tree: Resolved RVC layout.
    :param destination_dir: Directory to write the graph into.
    :return: Path to the written graph.
    """
    encoder = ContentEncoderGraph(load_content_encoder(tree)).eval()
    destination = destination_dir / GRAPH_FILENAME

    # One second of silence is enough to trace; the sample count is a dynamic axis.
    example_audio = torch.zeros(1, CONTENT_SAMPLE_RATE, dtype=torch.float32)

    logger.info("tracing content encoder")
    export_graph(
        encoder,
        (example_audio,),
        destination,
        input_names=[INPUT_AUDIO],
        output_names=[OUTPUT_FEATURES],
        dynamic_axes={
            INPUT_AUDIO: {0: "batch", 1: "numSamples"},
            OUTPUT_FEATURES: {0: "batch", 1: "numFrames"},
        },
    )
    return destination
