"""The binary container for the constant matrices the engine memory-maps.

Two of the exported assets are plain dense float32 matrices — the retrieval
codebook and the mel filter bank. Both are large enough that JSON or ``.npy``
would be wasteful or would drag a parser into the plugin, so they share one
trivial format that C++ can validate and then cast in place:

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

All fields are little-endian, which every platform the plugin targets already
is. The 32-byte header keeps the payload 32-byte aligned so the engine can point
a tensor straight at it without copying.
"""

from __future__ import annotations

import struct
from pathlib import Path

import numpy as np

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
