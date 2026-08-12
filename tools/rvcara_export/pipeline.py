"""A NumPy and ONNX Runtime conversion pipeline.

This module has one job beyond being runnable: it is the executable specification
for ``source/engine/ConversionEngine.cpp``. Everything the C++ does — the
zero-phase high-pass, the centred spectrogram, the salience decode, the mel
quantiser, the retrieval blend, the chunk seams — is written here first, in the
smallest amount of code that can be read against ``infer/vc/pipeline.py`` line by
line. When the C++ and the reference disagree, this is where the argument gets
settled.

It deliberately uses no torch. Anything that still needed torch would be something
the plugin cannot do.
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
import onnxruntime as ort

from .binary_matrix import read_matrix
from .manifest import MANIFEST_FILENAME
from .retrieval import blend_features

logger = logging.getLogger(__name__)


@dataclass(frozen=True, slots=True)
class ConversionSettings:
    """The user-facing controls.

    :ivar pitchShiftSemitones: Transposition applied to the estimated pitch.
    :ivar retrievalRatio: How far to move features toward the retrieved timbre.
    :ivar consonantProtection: Below 0.5, keeps unvoiced frames nearer the source.
    :ivar envelopeFollowRatio: How much of the source loudness envelope to restore.
    :ivar latentNoiseSeed: Seed for the vocoder's latent; fixes the render.
    """

    pitchShiftSemitones: float = 0.0
    retrievalRatio: float = 0.75
    consonantProtection: float = 0.33
    envelopeFollowRatio: float = 0.0
    latentNoiseSeed: int = 1


class VoiceModel:
    """A loaded set of exported assets.

    Owns the three ONNX sessions, the mel filter bank and the retrieval codebook,
    and reads every pipeline constant from the manifest rather than assuming it.
    """

    def __init__(self, model_dir: Path, *, num_threads: int = 0) -> None:
        """:param model_dir: Directory holding ``manifest.json`` and the assets.
        :param num_threads: Intra-op threads; 0 lets ONNX Runtime decide.
        """
        import json

        self.directory = model_dir
        self.manifest: dict[str, Any] = json.loads(
            (model_dir / MANIFEST_FILENAME).read_text(encoding="utf-8")
        )

        options = ort.SessionOptions()
        options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        if num_threads > 0:
            options.intra_op_num_threads = num_threads

        def open_session(role: str) -> ort.InferenceSession:
            filename = self.manifest["graphs"][role]["file"]
            return ort.InferenceSession(
                str(model_dir / filename), options, providers=["CPUExecutionProvider"]
            )

        self.contentEncoder = open_session("contentEncoder")
        self.pitchEstimator = open_session("pitchEstimator")
        self.vocoder = open_session("vocoder")

        self.melFilterBank = read_matrix(model_dir / self.manifest["pitchFrontEnd"]["filterBankFile"])
        self.codebook = read_matrix(model_dir / self.manifest["retrieval"]["file"])

    @property
    def modelSampleRate(self) -> int:
        return int(self.manifest["modelSampleRate"])

    @property
    def contentSampleRate(self) -> int:
        return int(self.manifest["contentEncoder"]["sampleRate"])

    @property
    def hopSizeInSamples(self) -> int:
        return int(self.manifest["pitchFrontEnd"]["hopSizeInSamples"])


def high_pass_filter(audio: np.ndarray, model: VoiceModel) -> np.ndarray:
    """Apply the manifest's zero-phase high-pass.

    Forward-backward filtering cancels the filter's phase response, which matters
    because a group delay that varies with frequency would move onsets relative to
    the pitch track.

    :param audio: Mono signal at the analysis rate.
    :param model: Supplies the coefficients.
    :return: Filtered signal, same length.
    """
    from scipy import signal

    description = model.manifest["highPassFilter"]
    return signal.filtfilt(
        np.asarray(description["numerator"], dtype=np.float64),
        np.asarray(description["denominator"], dtype=np.float64),
        audio,
    ).astype(np.float32)


def periodic_hann_window(windowSize: int) -> np.ndarray:
    """Return the periodic Hann window torch and SciPy both use for analysis.

    The periodic form divides by ``windowSize`` rather than ``windowSize - 1``. The
    symmetric form — which is what ``numpy.hanning`` returns — would put every
    magnitude bin slightly off and shift the mel energies enough to move the pitch
    estimate. It is the single easiest thing to get wrong here.
    """
    n = np.arange(windowSize, dtype=np.float64)
    return (0.5 - 0.5 * np.cos(2.0 * np.pi * n / windowSize)).astype(np.float32)


def log_mel_spectrogram(audio: np.ndarray, model: VoiceModel) -> np.ndarray:
    """Compute the log-mel spectrogram the pitch estimator expects.

    :param audio: Mono signal at the pitch front end's sample rate.
    :param model: Supplies the front-end parameters and the filter bank.
    :return: ``[numMelBins, numFrames]`` natural-log mel magnitudes.
    """
    frontEnd = model.manifest["pitchFrontEnd"]
    fftSize = int(frontEnd["fftSize"])
    hopSize = int(frontEnd["hopSizeInSamples"])
    magnitudeFloor = float(frontEnd["magnitudeFloor"])

    # Centred framing: reflect half a transform either side so frame k is centred on
    # sample k * hopSize, giving one frame per hop plus one.
    padded = np.pad(audio, (fftSize // 2, fftSize // 2), mode="reflect")
    numFrames = 1 + len(audio) // hopSize

    window = periodic_hann_window(int(frontEnd["windowSize"]))
    frameStarts = np.arange(numFrames) * hopSize
    frames = padded[frameStarts[:, None] + np.arange(fftSize)[None, :]] * window

    magnitude = np.abs(np.fft.rfft(frames, n=fftSize, axis=1)).T  # [numBins, numFrames]
    melMagnitude = model.melFilterBank @ magnitude

    return np.log(np.maximum(melMagnitude, magnitudeFloor)).astype(np.float32)


def decode_salience(salience: np.ndarray, model: VoiceModel) -> np.ndarray:
    """Turn the pitch network's per-class salience into hertz.

    Each frame's pitch is the salience-weighted mean of the classes within
    ``localAverageRadius`` of the peak, which recovers resolution finer than the
    20-cent class spacing. Frames whose peak salience is below threshold are
    reported as zero, meaning unvoiced.

    :param salience: ``[numFrames, numPitchBins]`` activations in ``[0, 1]``.
    :param model: Supplies the decoder constants.
    :return: ``[numFrames]`` pitch in hertz, zero where unvoiced.
    """
    decoder = model.manifest["pitchDecoder"]
    radius = int(decoder["localAverageRadius"])
    centsOrigin = float(decoder["centsOrigin"])
    centsPerBin = float(decoder["centsPerBin"])
    referenceHz = float(decoder["centsReferenceHz"])
    threshold = float(decoder["salienceThreshold"])

    numBins = salience.shape[1]
    centsPerClass = centsOrigin + centsPerBin * np.arange(numBins, dtype=np.float64)

    # Zero-pad both the salience and the cents map so a peak at either end still has
    # a full window, and the padded entries contribute nothing to the weighted mean.
    paddedSalience = np.pad(salience, ((0, 0), (radius, radius)))
    paddedCents = np.pad(centsPerClass, (radius, radius))

    peakIndices = np.argmax(salience, axis=1) + radius
    offsets = np.arange(-radius, radius + 1)
    windowIndices = peakIndices[:, None] + offsets[None, :]

    windowSalience = np.take_along_axis(paddedSalience, windowIndices, axis=1)
    windowCents = paddedCents[windowIndices]

    weightSum = windowSalience.sum(axis=1)
    cents = np.divide(
        (windowSalience * windowCents).sum(axis=1),
        weightSum,
        out=np.zeros_like(weightSum),
        where=weightSum > 0.0,
    )

    fundamentalFrequencyHz = referenceHz * np.power(2.0, cents / 1200.0)
    fundamentalFrequencyHz[salience.max(axis=1) <= threshold] = 0.0
    fundamentalFrequencyHz[cents == 0.0] = 0.0

    return fundamentalFrequencyHz.astype(np.float32)


def fill_unvoiced_gaps(fundamentalFrequencyHz: np.ndarray) -> np.ndarray:
    """Interpolate across unvoiced frames.

    The reference pipeline does this so the harmonic excitation stays continuous
    through consonants instead of restarting its phase, which would click. A
    consequence worth knowing: after this step nothing is zero, so the
    ``consonantProtection`` blend downstream has nothing to act on. That is
    upstream's behaviour and the plugin reproduces it rather than quietly
    "improving" it — the control is kept, and documented as inert for models whose
    pitch track is gap-filled.

    :param fundamentalFrequencyHz: ``[numFrames]``, zero where unvoiced.
    :return: The same array with gaps filled; unchanged if nothing is voiced.
    """
    isUnvoiced = fundamentalFrequencyHz == 0.0
    if isUnvoiced.all() or not isUnvoiced.any():
        return fundamentalFrequencyHz

    filled = fundamentalFrequencyHz.copy()
    filled[isUnvoiced] = np.interp(
        np.flatnonzero(isUnvoiced),
        np.flatnonzero(~isUnvoiced),
        fundamentalFrequencyHz[~isUnvoiced],
    )
    return filled


def quantise_coarse_pitch(fundamentalFrequencyHz: np.ndarray, model: VoiceModel) -> np.ndarray:
    """Map pitch in hertz onto the generator's 1-255 pitch embedding indices.

    The spacing is mel, not linear or logarithmic, so a semitone near the bottom of
    the range spans more indices than one near the top.

    :param fundamentalFrequencyHz: ``[numFrames]`` pitch, zero where unvoiced.
    :param model: Supplies the range and bin count.
    :return: ``[numFrames]`` int64 indices; unvoiced frames land on 1.
    """
    decoder = model.manifest["pitchDecoder"]
    numBins = int(decoder["numCoarsePitchBins"])

    def hzToMel(hz: np.ndarray | float) -> np.ndarray | float:
        return 1127.0 * np.log(1.0 + np.asarray(hz, dtype=np.float64) / 700.0)

    melMinimum = hzToMel(float(decoder["minimumFrequencyHz"]))
    melMaximum = hzToMel(float(decoder["maximumFrequencyHz"]))

    mel = hzToMel(fundamentalFrequencyHz)
    scaled = np.where(
        mel > 0.0,
        (mel - melMinimum) * (numBins - 1) / (melMaximum - melMinimum) + 1.0,
        mel,
    )
    return np.rint(np.clip(scaled, 1.0, float(numBins))).astype(np.int64)


def estimate_pitch(
    audio: np.ndarray, model: VoiceModel, *, pitchShiftSemitones: float
) -> tuple[np.ndarray, np.ndarray]:
    """Run the pitch estimator over a whole signal.

    :param audio: Mono signal at the pitch front end's sample rate.
    :param model: The loaded voice model.
    :param pitchShiftSemitones: Transposition applied after estimation, so that the
        shift lands on the pitch the singer actually sang.
    :return: Coarse bin indices and continuous pitch in hertz, both ``[numFrames]``.
    """
    frameCountMultiple = int(model.manifest["pitchDecoder"]["frameCountMultiple"])

    logMel = log_mel_spectrogram(audio, model)
    numFrames = logMel.shape[1]

    # The U-Net halves the time axis five times, so pad up to a multiple of 32 and
    # discard the extra frames afterwards.
    remainder = -numFrames % frameCountMultiple
    if remainder:
        logMel = np.pad(logMel, ((0, 0), (0, remainder)), mode="constant")

    inputName = model.pitchEstimator.get_inputs()[0].name
    salience = model.pitchEstimator.run(None, {inputName: logMel[None, :, :]})[0][0]
    salience = salience[:numFrames]

    fundamentalFrequencyHz = fill_unvoiced_gaps(decode_salience(salience, model))
    fundamentalFrequencyHz = fundamentalFrequencyHz * (2.0 ** (pitchShiftSemitones / 12.0))

    return quantise_coarse_pitch(fundamentalFrequencyHz, model), fundamentalFrequencyHz.astype(
        np.float32
    )


def encode_content(audio: np.ndarray, model: VoiceModel) -> np.ndarray:
    """Run the content encoder.

    :param audio: Mono signal at the content encoder's sample rate.
    :param model: The loaded voice model.
    :return: ``[numFrames, featureDim]`` features at the encoder's frame rate.
    """
    inputName = model.contentEncoder.get_inputs()[0].name
    return model.contentEncoder.run(None, {inputName: audio[None, :].astype(np.float32)})[0][0]


def upsample_frames(features: np.ndarray, factor: int) -> np.ndarray:
    """Repeat each frame ``factor`` times.

    The content encoder emits 50 frames a second and the vocoder is conditioned at
    100, so every feature frame covers two conditioning frames. Nearest-neighbour
    repetition is what the reference does; interpolating would blur phoneme
    boundaries across a 20 ms window.
    """
    return np.repeat(features, factor, axis=0)


def synthesise(
    contentFeatures: np.ndarray,
    coarsePitch: np.ndarray,
    fundamentalFrequencyHz: np.ndarray,
    model: VoiceModel,
    settings: ConversionSettings,
) -> np.ndarray:
    """Run the vocoder over one chunk.

    :param contentFeatures: ``[numFrames, featureDim]`` conditioning features.
    :param coarsePitch: ``[numFrames]`` int64 pitch embedding indices.
    :param fundamentalFrequencyHz: ``[numFrames]`` continuous pitch.
    :param model: The loaded voice model.
    :param settings: Supplies the latent seed.
    :return: ``[numSamples]`` waveform at the model's sample rate.
    """
    numFrames = contentFeatures.shape[0]
    latentDim = model.vocoder.get_inputs()[5].shape[1]
    if not isinstance(latentDim, int):
        latentDim = 192

    generator = np.random.default_rng(settings.latentNoiseSeed)
    latentNoise = generator.standard_normal((1, latentDim, numFrames), dtype=np.float32)

    feeds = {
        "contentFeatures": contentFeatures[None, :, :].astype(np.float32),
        "numFrames": np.array([numFrames], dtype=np.int64),
        "coarsePitch": coarsePitch[None, :].astype(np.int64),
        "fundamentalFrequencyHz": fundamentalFrequencyHz[None, :].astype(np.float32),
        "speakerId": np.array([int(model.manifest["speakerId"])], dtype=np.int64),
        "latentNoise": latentNoise,
    }
    named = {actual.name: feeds[actual.name] for actual in model.vocoder.get_inputs()}

    return model.vocoder.run(None, named)[0][0, 0]


def find_split_positions(audio: np.ndarray, model: VoiceModel) -> list[int]:
    """Choose seam positions for an input too long to convert in one pass.

    Seams are placed at the quietest point within a search window either side of
    each nominal boundary, so a join lands in a breath rather than mid-vowel. The
    energy measure is a moving sum of absolute amplitude over one hop.

    :param audio: Filtered mono signal at the analysis rate.
    :param model: Supplies the chunking parameters.
    :return: Split positions in samples, empty when the input fits in one chunk.
    """
    chunking = model.manifest["chunking"]
    sampleRate = model.contentSampleRate
    hopSize = model.hopSizeInSamples

    maximumChunk = int(chunking["maximumChunkSeconds"] * sampleRate)
    stride = int(chunking["chunkStrideSeconds"] * sampleRate)
    searchRadius = int(chunking["splitSearchRadiusSeconds"] * sampleRate)
    contextPadding = int(chunking["contextPaddingSeconds"] * sampleRate)

    if len(audio) + 2 * contextPadding <= maximumChunk:
        return []

    padded = np.pad(audio, (hopSize // 2, hopSize // 2), mode="reflect")
    movingEnergy = np.zeros(len(audio), dtype=np.float64)
    for offset in range(hopSize):
        movingEnergy += np.abs(padded[offset : offset + len(audio)])

    positions = []
    for boundary in range(stride, len(audio), stride):
        start = max(boundary - searchRadius, 0)
        end = min(boundary + searchRadius, len(audio))
        if end <= start:
            continue
        positions.append(start + int(np.argmin(movingEnergy[start:end])))

    return positions


def convert(
    audio: np.ndarray,
    model: VoiceModel,
    settings: ConversionSettings | None = None,
    *,
    exact_retrieval: bool = True,
) -> np.ndarray:
    """Convert a mono signal, returning audio at the model's sample rate.

    :param audio: Mono signal at the content encoder's sample rate, 16 kHz.
    :param model: The loaded voice model.
    :param settings: Conversion controls; manifest defaults are used when omitted.
    :param exact_retrieval: Use brute-force nearest neighbours. The plugin searches
        approximately; the exact path is here so the two can be compared.
    :return: ``[numSamples]`` converted waveform.
    """
    settings = settings or ConversionSettings(**model.manifest["defaults"])

    sampleRate = model.contentSampleRate
    modelSampleRate = model.modelSampleRate
    hopSize = model.hopSizeInSamples
    contextPadding = int(model.manifest["chunking"]["contextPaddingSeconds"] * sampleRate)
    outputContextPadding = int(
        model.manifest["chunking"]["contextPaddingSeconds"] * modelSampleRate
    )
    featureUpsampleFactor = (
        int(model.manifest["pitchFrontEnd"]["sampleRate"] // hopSize)
        // int(model.manifest["contentEncoder"]["frameRate"])
    )

    filtered = high_pass_filter(audio, model)
    splitPositions = find_split_positions(filtered, model)

    padded = np.pad(filtered, (contextPadding, contextPadding), mode="reflect")
    coarsePitch, fundamentalFrequencyHz = estimate_pitch(
        padded, model, pitchShiftSemitones=settings.pitchShiftSemitones
    )

    numFrames = len(padded) // hopSize
    coarsePitch = coarsePitch[:numFrames]
    fundamentalFrequencyHz = fundamentalFrequencyHz[:numFrames]

    # Chunk boundaries in the padded signal. Each chunk carries contextPadding of
    # real neighbouring audio on both sides, which is then trimmed off its output,
    # so the seams cross-fade nothing and simply abut.
    boundaries = [position // hopSize * hopSize for position in splitPositions]
    chunkRanges: list[tuple[int, int | None]] = []
    start = 0
    for boundary in boundaries:
        chunkRanges.append((start, boundary + 2 * contextPadding + hopSize))
        start = boundary
    chunkRanges.append((start, None))

    segments = []
    for chunkStart, chunkEnd in chunkRanges:
        chunk = padded[chunkStart:chunkEnd]
        frameStart = chunkStart // hopSize
        frameEnd = None if chunkEnd is None else (chunkEnd - hopSize) // hopSize

        features = encode_content(chunk, model)
        originalFeatures = upsample_frames(features, featureUpsampleFactor)

        if exact_retrieval:
            features = blend_features(
                features,
                model.codebook,
                retrieval_ratio=settings.retrievalRatio,
                num_neighbours=int(model.manifest["retrieval"]["numNeighbours"]),
            )
        features = upsample_frames(features, featureUpsampleFactor)

        chunkCoarsePitch = coarsePitch[frameStart:frameEnd]
        chunkPitchHz = fundamentalFrequencyHz[frameStart:frameEnd]

        numChunkFrames = min(len(chunk) // hopSize, features.shape[0], len(chunkCoarsePitch))
        features = features[:numChunkFrames]
        originalFeatures = originalFeatures[:numChunkFrames]
        chunkCoarsePitch = chunkCoarsePitch[:numChunkFrames]
        chunkPitchHz = chunkPitchHz[:numChunkFrames]

        if settings.consonantProtection < 0.5:
            weight = np.where(chunkPitchHz > 0.0, 1.0, settings.consonantProtection)
            weight = np.where(chunkPitchHz < 1.0, settings.consonantProtection, weight)[:, None]
            features = features * weight + originalFeatures * (1.0 - weight)

        rendered = synthesise(
            features.astype(np.float32), chunkCoarsePitch, chunkPitchHz, model, settings
        )
        segments.append(rendered[outputContextPadding:-outputContextPadding])

    converted = np.concatenate(segments) if len(segments) > 1 else segments[0]

    if settings.envelopeFollowRatio > 0.0:
        converted = follow_source_envelope(
            audio, sampleRate, converted, modelSampleRate, settings.envelopeFollowRatio
        )

    return converted.astype(np.float32)


def follow_source_envelope(
    source: np.ndarray,
    sourceSampleRate: int,
    converted: np.ndarray,
    convertedSampleRate: int,
    ratio: float,
) -> np.ndarray:
    """Restore part of the source's loudness contour onto the conversion.

    Both envelopes are measured over half-second windows and resampled to the
    output length; the output is then scaled by ``sourceEnvelope ** (1 - ratio) *
    convertedEnvelope ** (ratio - 1)``, which at ratio 0 imposes the source
    envelope entirely and at ratio 1 leaves the conversion alone.

    :param source: Mono source signal.
    :param sourceSampleRate: Its sample rate.
    :param converted: Converted signal.
    :param convertedSampleRate: Its sample rate.
    :param ratio: 0 follows the source fully, 1 disables the effect.
    :return: Scaled conversion.
    """
    def envelope(signal: np.ndarray, sampleRate: int) -> np.ndarray:
        windowSize = sampleRate // 2
        numWindows = max(1 + len(signal) // windowSize, 2)
        padded = np.pad(signal, (0, max(0, numWindows * windowSize - len(signal))))
        frames = padded[: numWindows * windowSize].reshape(numWindows, windowSize)
        rootMeanSquare = np.sqrt(np.mean(np.square(frames, dtype=np.float64), axis=1))
        return np.interp(
            np.linspace(0.0, 1.0, len(converted)),
            np.linspace(0.0, 1.0, numWindows),
            rootMeanSquare,
        )

    sourceEnvelope = envelope(source, sourceSampleRate)
    convertedEnvelope = np.maximum(envelope(converted, convertedSampleRate), 1e-6)

    scale = np.power(sourceEnvelope, 1.0 - ratio) * np.power(convertedEnvelope, ratio - 1.0)
    return (converted * scale).astype(np.float32)
