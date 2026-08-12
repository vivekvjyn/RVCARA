"""The non-graph assets: the binary matrix container and the retrieval codebook.

Two of the exported files are large constant matrices — the retrieval codebook, which for a
few minutes of training audio runs to around 180 MB, and the mel filter bank. Both share one
trivial container so that C++ can validate a header and then use the payload in place,
memory-mapped, without dragging a parser into the plug-in.

===========  ======  ==========================================================
Offset       Type    Meaning
===========  ======  ==========================================================
0            char[8] ``RVCARAM1`` magic, identifying the container and revision
8            uint32  Format version, currently 1
12           uint32  Element type; 0 is float32 and the only value in use
16           uint32  Row count
20           uint32  Column count
24           uint64  Reserved, written as zero, pads the header to 32 bytes
32           data    ``numRows * numColumns`` elements, row-major
===========  ======  ==========================================================

All fields are little-endian. The 32-byte header keeps the payload 32-byte aligned so the
engine can point a tensor straight at it without copying.
"""

from __future__ import annotations

import logging
import struct
from pathlib import Path

import numpy as np

logger = logging.getLogger(__name__)

MAGIC = b"RVCARAM1"
FORMAT_VERSION = 1
ELEMENT_TYPE_FLOAT32 = 0
HEADER_SIZE = 32

_HEADER_STRUCT = struct.Struct("<8sIIIIQ")


def write_matrix(path: Path, matrix: np.ndarray) -> int:
    """Write a 2-D float32 matrix and return the number of bytes written.

    :param path: Destination file; parent directories are created.
    :param matrix: Two-dimensional array, converted to little-endian float32.
    :return: Total file size in bytes.
    :raises ValueError: If ``matrix`` is not two-dimensional.
    """
    if matrix.ndim != 2:
        raise ValueError(f"expected a 2-D matrix, got shape {matrix.shape}")

    payload = np.ascontiguousarray(matrix, dtype="<f4")
    header = _HEADER_STRUCT.pack(
        MAGIC,
        FORMAT_VERSION,
        ELEMENT_TYPE_FLOAT32,
        payload.shape[0],
        payload.shape[1],
        0,
    )
    assert len(header) == HEADER_SIZE

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as stream:
        stream.write(header)
        stream.write(payload.tobytes(order="C"))

    return HEADER_SIZE + payload.nbytes


def read_matrix(path: Path) -> np.ndarray:
    """Read a matrix written by :func:`write_matrix`.

    Used by the verification pass to confirm the file on disk is what the C++
    engine will see, rather than trusting the array that was written.

    :param path: File to read.
    :return: Two-dimensional float32 array.
    :raises ValueError: If the magic, version or element type is unrecognised, or
        the payload length disagrees with the header.
    """
    with path.open("rb") as stream:
        magic, version, element_type, num_rows, num_columns, _ = _HEADER_STRUCT.unpack(
            stream.read(HEADER_SIZE)
        )
        if magic != MAGIC:
            raise ValueError(f"{path.name}: not an RVCARA matrix (magic {magic!r})")
        if version != FORMAT_VERSION:
            raise ValueError(f"{path.name}: unsupported format version {version}")
        if element_type != ELEMENT_TYPE_FLOAT32:
            raise ValueError(f"{path.name}: unsupported element type {element_type}")

        expected_bytes = num_rows * num_columns * 4
        payload = stream.read()
        if len(payload) != expected_bytes:
            raise ValueError(
                f"{path.name}: header declares {expected_bytes} bytes of data, found {len(payload)}"
            )

    return np.frombuffer(payload, dtype="<f4").reshape(num_rows, num_columns)


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
