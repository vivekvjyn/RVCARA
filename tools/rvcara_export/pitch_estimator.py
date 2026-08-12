"""Export the pitch estimator: log-mel spectrogram in, pitch salience out.

RMVPE is a deep U-Net followed by a bidirectional GRU that scores 360 pitch
classes per frame, one every 20 cents from 1997.38 cents above 10 Hz — roughly
C1 to B6. The network is exported on its own and the spectrogram front end is
reproduced in C++, for two reasons: ``torch.stft`` exports to an ONNX operator
that ONNX Runtime implements less efficiently than a hand-written radix-2
transform, and keeping the front end in C++ lets the filter bank stay a data file
the engine validates rather than a constant fused into a graph.
"""

from __future__ import annotations

import logging
from pathlib import Path

import numpy as np
import torch
from torch import nn

from .assets import write_matrix
from .graphs import export_graph
from .rvc import RvcTree, import_rvc_module

logger = logging.getLogger(__name__)

# Spectrogram front end. These are the values upstream constructs MelSpectrogram
# with, and the C++ front end must match every one of them.
PITCH_SAMPLE_RATE = 16_000
FFT_SIZE = 1_024
WINDOW_SIZE = 1_024
HOP_SIZE = 160
NUM_MEL_BINS = 128
MEL_MINIMUM_HZ = 30.0
MEL_MAXIMUM_HZ = 8_000.0
MEL_SCALE = "htk"
MAGNITUDE_FLOOR = 1e-5

# Frame rate implied by the hop: 16000 / 160 = 100 Hz. This is also the rate the
# vocoder is conditioned at, which is why the features from the 50 Hz content
# encoder get duplicated on the way in.
PITCH_FRAME_RATE = PITCH_SAMPLE_RATE // HOP_SIZE

# The U-Net halves both axes five times, so the frame count must be a multiple of
# 32. 128 mel bins divide evenly; the time axis is padded by the caller.
FRAME_COUNT_MULTIPLE = 32

# Salience decoding. Class i sits at CENTS_ORIGIN + i * CENTS_PER_BIN cents above
# 10 Hz; a frame's pitch is the salience-weighted mean of the four classes either
# side of the peak, and frames whose peak falls below the threshold are unvoiced.
NUM_PITCH_BINS = 360
CENTS_ORIGIN = 1997.3794084376191
CENTS_PER_BIN = 20.0
CENTS_REFERENCE_HZ = 10.0
LOCAL_AVERAGE_RADIUS = 4
SALIENCE_THRESHOLD = 0.03

# Pitch range the coarse quantiser spans. Not RMVPE's own range — these bound the
# generator's 255-entry pitch embedding.
PITCH_MINIMUM_HZ = 50.0
PITCH_MAXIMUM_HZ = 1_100.0
NUM_COARSE_PITCH_BINS = 255

# Architecture arguments upstream instantiates E2E with: four residual blocks per
# stage, one GRU layer, 2x2 kernels.
NUM_RESIDUAL_BLOCKS = 4
NUM_GRU_LAYERS = 1
KERNEL_SIZE = (2, 2)

GRAPH_FILENAME = "pitch_estimator.onnx"
FILTER_BANK_FILENAME = "mel_filter_bank.bin"
INPUT_MEL = "logMelSpectrogram"
OUTPUT_SALIENCE = "salience"


def build_mel_filter_bank() -> np.ndarray:
    """Return the ``[128, 513]`` filter bank the spectrogram front end applies.

    Generated with librosa so it is bit-identical to the one RMVPE was trained
    with, including the HTK mel scale and librosa's default Slaney area
    normalisation.

    :return: Float32 matrix mapping FFT magnitude bins to mel bins.
    """
    from librosa.filters import mel  # lazily imported; only the exporter needs librosa

    filter_bank = mel(
        sr=PITCH_SAMPLE_RATE,
        n_fft=FFT_SIZE,
        n_mels=NUM_MEL_BINS,
        fmin=MEL_MINIMUM_HZ,
        fmax=MEL_MAXIMUM_HZ,
        htk=MEL_SCALE == "htk",
    )
    return np.asarray(filter_bank, dtype=np.float32)


class PitchEstimatorGraph(nn.Module):
    """Wraps RMVPE's ``E2E`` so tracing sees a plain tensor-to-tensor function."""

    def __init__(self, network: nn.Module) -> None:
        super().__init__()
        self.network = network

    def forward(self, log_mel_spectrogram: torch.Tensor) -> torch.Tensor:
        """:param log_mel_spectrogram: ``[batch, 128, numFrames]``, ``numFrames`` a
            multiple of 32.
        :return: ``[batch, numFrames, 360]`` salience in ``[0, 1]``.
        """
        return self.network(log_mel_spectrogram)


def load_pitch_estimator(tree: RvcTree) -> nn.Module:
    """Load ``rmvpe.pt`` into upstream's ``E2E`` in float32 eval mode.

    :param tree: Resolved RVC layout.
    :return: The loaded network.
    :raises FileNotFoundError: If the RMVPE checkpoint was never downloaded.
    """
    if not tree.rmvpe_checkpoint.is_file():
        raise FileNotFoundError(
            f"no RMVPE weights at {tree.rmvpe_checkpoint}. "
            "Train or convert once with the RVC CLI to populate the asset cache."
        )

    rmvpe = import_rvc_module("infer.rmvpe")
    network = rmvpe.E2E(NUM_RESIDUAL_BLOCKS, NUM_GRU_LAYERS, KERNEL_SIZE)
    network.load_state_dict(torch.load(tree.rmvpe_checkpoint, map_location="cpu"))
    return network.float().eval()


def export_pitch_estimator(tree: RvcTree, destination_dir: Path) -> tuple[Path, Path]:
    """Write ``pitch_estimator.onnx`` and ``mel_filter_bank.bin``.

    :param tree: Resolved RVC layout.
    :param destination_dir: Directory to write into.
    :return: Paths to the graph and the filter bank.
    """
    estimator = PitchEstimatorGraph(load_pitch_estimator(tree)).eval()
    graph_path = destination_dir / GRAPH_FILENAME

    # A 32-frame example: the smallest legal input, so tracing cannot accidentally
    # bake in a larger frame count.
    example_mel = torch.zeros(1, NUM_MEL_BINS, FRAME_COUNT_MULTIPLE, dtype=torch.float32)

    logger.info("tracing pitch estimator")
    export_graph(
        estimator,
        (example_mel,),
        graph_path,
        input_names=[INPUT_MEL],
        output_names=[OUTPUT_SALIENCE],
        dynamic_axes={
            INPUT_MEL: {0: "batch", 2: "numFrames"},
            OUTPUT_SALIENCE: {0: "batch", 1: "numFrames"},
        },
    )

    filter_bank_path = destination_dir / FILTER_BANK_FILENAME
    filter_bank = build_mel_filter_bank()
    write_matrix(filter_bank_path, filter_bank)
    logger.info("wrote mel filter bank %s", filter_bank.shape)

    return graph_path, filter_bank_path


def describe_front_end() -> dict[str, object]:
    """Return the spectrogram parameters the C++ front end has to reproduce."""
    return {
        "sampleRate": PITCH_SAMPLE_RATE,
        "fftSize": FFT_SIZE,
        "windowSize": WINDOW_SIZE,
        "hopSizeInSamples": HOP_SIZE,
        "window": "hann",
        "isCentred": True,
        "numMelBins": NUM_MEL_BINS,
        "numBins": FFT_SIZE // 2 + 1,
        "minimumFrequencyHz": MEL_MINIMUM_HZ,
        "maximumFrequencyHz": MEL_MAXIMUM_HZ,
        "melScale": MEL_SCALE,
        "magnitudeFloor": MAGNITUDE_FLOOR,
        "filterBankFile": FILTER_BANK_FILENAME,
    }


def describe_decoder() -> dict[str, object]:
    """Return the constants that turn salience into hertz."""
    return {
        "numPitchBins": NUM_PITCH_BINS,
        "centsOrigin": CENTS_ORIGIN,
        "centsPerBin": CENTS_PER_BIN,
        "centsReferenceHz": CENTS_REFERENCE_HZ,
        "localAverageRadius": LOCAL_AVERAGE_RADIUS,
        "salienceThreshold": SALIENCE_THRESHOLD,
        "frameCountMultiple": FRAME_COUNT_MULTIPLE,
        "minimumFrequencyHz": PITCH_MINIMUM_HZ,
        "maximumFrequencyHz": PITCH_MAXIMUM_HZ,
        "numCoarsePitchBins": NUM_COARSE_PITCH_BINS,
    }
