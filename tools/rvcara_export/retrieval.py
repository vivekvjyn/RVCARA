"""Export the retrieval codebook.

Retrieval is the "R" in RVC and the reason a model trained on a few minutes of
audio sounds like the singer rather than like an average of them. Training stores
every content frame of the training set in a FAISS index; at inference each frame
of the incoming performance is replaced by an inverse-square-distance weighted
mean of its eight nearest training frames, then blended back toward the original
by ``retrievalRatio``.

FAISS itself is far too heavy to link into a plugin — it pulls in BLAS, OpenMP and
a large template surface for an operation the engine performs one way. The index
is therefore flattened here into the raw codebook, and the plugin builds a
hnswlib graph over it on first load. Approximate search is a deliberate choice:
the eight neighbours get averaged, so a rare substitution in the tail of the
neighbour list is inaudible, and it is the difference between microseconds and
tens of milliseconds per frame.
"""

from __future__ import annotations

import logging
from pathlib import Path

import numpy as np

from .binary_matrix import write_matrix

logger = logging.getLogger(__name__)

CODEBOOK_FILENAME = "retrieval.bin"

# Neighbour count, fixed upstream. Kept as a manifest value rather than a C++
# constant so a future model can change it without a plugin release.
NUM_NEIGHBOURS = 8

# hnswlib construction parameters the engine should use. M is the graph degree and
# efConstruction the build-time candidate list; these are hnswlib's own
# recommendations for a few hundred thousand vectors of this width, and they put
# recall of the eight true neighbours above 99% at efSearch 64.
GRAPH_DEGREE = 16
CONSTRUCTION_CANDIDATE_LIST_SIZE = 200
SEARCH_CANDIDATE_LIST_SIZE = 64


class RetrievalIndexError(RuntimeError):
    """Raised when the FAISS index cannot be read or does not match the model."""


def read_codebook(index_path: Path, *, expected_feature_dim: int) -> np.ndarray:
    """Reconstruct every vector held in a FAISS index.

    ``reconstruct_n`` is used rather than reading the raw storage because the index
    may be quantised or clustered; reconstruction yields the vectors the reference
    pipeline actually sums, whatever the internal layout.

    :param index_path: The ``.index`` written by training.
    :param expected_feature_dim: Feature width the voice model expects.
    :return: ``[numVectors, featureDim]`` float32 codebook.
    :raises RetrievalIndexError: If the index is unreadable, empty, or the wrong width.
    """
    try:
        import faiss
    except ImportError as error:  # pragma: no cover - declared dependency
        raise RetrievalIndexError(
            "faiss is required to read the retrieval index; install faiss-cpu"
        ) from error

    try:
        index = faiss.read_index(str(index_path))
    except Exception as error:  # faiss raises bare RuntimeError subclasses
        raise RetrievalIndexError(f"cannot read {index_path.name}: {error}") from error

    if index.ntotal == 0:
        raise RetrievalIndexError(f"{index_path.name} holds no vectors")

    if index.d != expected_feature_dim:
        raise RetrievalIndexError(
            f"{index_path.name} holds {index.d}-D vectors but the voice model expects "
            f"{expected_feature_dim}-D"
        )

    logger.info("reconstructing %d x %d codebook from FAISS", index.ntotal, index.d)
    codebook = index.reconstruct_n(0, index.ntotal)

    return np.ascontiguousarray(codebook, dtype=np.float32)


def export_codebook(
    index_path: Path, destination_dir: Path, *, expected_feature_dim: int
) -> tuple[Path, np.ndarray]:
    """Write ``retrieval.bin``.

    :param index_path: The ``.index`` written by training.
    :param destination_dir: Directory to write into.
    :param expected_feature_dim: Feature width the voice model expects.
    :return: The written path and the codebook, so callers can reuse it for verification.
    """
    codebook = read_codebook(index_path, expected_feature_dim=expected_feature_dim)
    destination = destination_dir / CODEBOOK_FILENAME
    num_bytes = write_matrix(destination, codebook)
    logger.info("wrote codebook: %d vectors, %.1f MiB", len(codebook), num_bytes / 1024**2)
    return destination, codebook


def blend_features(
    features: np.ndarray,
    codebook: np.ndarray,
    *,
    retrieval_ratio: float,
    num_neighbours: int = NUM_NEIGHBOURS,
) -> np.ndarray:
    """Exact reference implementation of the retrieval blend.

    This is the specification the plugin's approximate search is measured against,
    and it is what :mod:`verify` compares the engine's output to. It is a brute
    force search on purpose — correctness here matters, speed does not.

    :param features: ``[numFrames, featureDim]`` content-encoder output.
    :param codebook: ``[numVectors, featureDim]`` training-set frames.
    :param retrieval_ratio: 0 leaves ``features`` untouched, 1 replaces it entirely.
    :param num_neighbours: Neighbours to average.
    :return: ``[numFrames, featureDim]`` blended features.
    """
    if retrieval_ratio <= 0.0:
        return features

    # Squared euclidean distance via the expansion |a - b|^2 = |a|^2 - 2ab + |b|^2.
    # FAISS reports squared distances and the weights are 1/distance^2, so squared
    # distances are what the weighting expects.
    codebookSquaredNorms = np.einsum("ij,ij->i", codebook, codebook)
    featureSquaredNorms = np.einsum("ij,ij->i", features, features)
    squaredDistances = (
        featureSquaredNorms[:, None] - 2.0 * features @ codebook.T + codebookSquaredNorms[None, :]
    )
    np.maximum(squaredDistances, 0.0, out=squaredDistances)

    neighbourIndices = np.argpartition(squaredDistances, num_neighbours - 1, axis=1)[
        :, :num_neighbours
    ]
    neighbourDistances = np.take_along_axis(squaredDistances, neighbourIndices, axis=1)

    weights = 1.0 / np.square(np.maximum(neighbourDistances, np.finfo(np.float32).tiny))
    weights /= weights.sum(axis=1, keepdims=True)

    retrieved = np.einsum("nkd,nk->nd", codebook[neighbourIndices], weights)

    return (retrieval_ratio * retrieved + (1.0 - retrieval_ratio) * features).astype(np.float32)


def describe_codebook(codebook: np.ndarray) -> dict[str, object]:
    """Return the manifest entry describing the codebook and how to search it."""
    return {
        "file": CODEBOOK_FILENAME,
        "numVectors": int(codebook.shape[0]),
        "featureDim": int(codebook.shape[1]),
        "numNeighbours": NUM_NEIGHBOURS,
        "graphDegree": GRAPH_DEGREE,
        "constructionCandidateListSize": CONSTRUCTION_CANDIDATE_LIST_SIZE,
        "searchCandidateListSize": SEARCH_CANDIDATE_LIST_SIZE,
    }
