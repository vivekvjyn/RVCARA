"""Access to the upstream RVC codebase and the artefacts it produces.

The exporter reuses upstream's own module definitions rather than reimplementing
them: a reimplementation would silently drift from the checkpoint it is meant to
load. That means the RVC source tree has to be importable, so everything that
depends on where it lives is confined to this module.
"""

from __future__ import annotations

import importlib
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import torch

# The layout of the ``config`` list stored in an RVC checkpoint. Upstream splats it
# positionally into the synthesiser constructor, so the order is load-bearing and
# worth naming.
CHECKPOINT_CONFIG_FIELDS = (
    "spec_channels",
    "segment_size",
    "inter_channels",
    "hidden_channels",
    "filter_channels",
    "n_heads",
    "n_layers",
    "kernel_size",
    "p_dropout",
    "resblock",
    "resblock_kernel_sizes",
    "resblock_dilation_sizes",
    "upsample_rates",
    "upsample_initial_channel",
    "upsample_kernel_sizes",
    "spk_embed_dim",
    "gin_channels",
    "sr",
)


class RvcSourceNotFound(RuntimeError):
    """Raised when the upstream RVC source tree cannot be located."""


@dataclass(frozen=True, slots=True)
class RvcTree:
    """Filesystem layout of a checked-out RVC project.

    :ivar project_root: The ``RVC`` repository root, holding ``models/`` and ``libs/``.
    :ivar source_root: The vendored upstream submodule, importable as ``infer.*``.
    :ivar assets_root: Where the pretrained HuBERT and RMVPE weights were cached.
    """

    project_root: Path
    source_root: Path
    assets_root: Path

    @property
    def hubert_dir(self) -> Path:
        return self.assets_root / "hubert_base"

    @property
    def rmvpe_checkpoint(self) -> Path:
        return self.assets_root / "rmvpe" / "rmvpe.pt"

    def model_dir(self, name: str) -> Path:
        return self.project_root / "models" / name


def locate_rvc_tree(project_root: Path) -> RvcTree:
    """Resolve the RVC directory layout and make ``infer.*`` importable.

    The pretrained assets are looked for in the project's download cache first and
    then inside the submodule, because ``rtvoice`` caches them outside the vendored
    tree while upstream expects them inside it.

    :param project_root: Path to the ``RVC`` repository.
    :return: The resolved layout.
    :raises RvcSourceNotFound: If the submodule or the pretrained assets are absent.
    """
    project_root = project_root.expanduser().resolve()
    source_root = project_root / "libs" / "rvc-src"

    if not (source_root / "infer" / "module" / "models.py").is_file():
        raise RvcSourceNotFound(
            f"no RVC source tree at {source_root}. "
            "Run `git submodule update --init` in the RVC repository."
        )

    candidates = [project_root / ".cache" / "assets", source_root / "assets"]
    assets_root = next((c for c in candidates if (c / "hubert_base" / "config.json").is_file()), None)
    if assets_root is None:
        raise RvcSourceNotFound(
            "no cached HuBERT weights found. Looked in: "
            + ", ".join(str(c) for c in candidates)
        )

    if str(source_root) not in sys.path:
        sys.path.insert(0, str(source_root))

    # Upstream reads these at import time and asserts on absence.
    os.environ.setdefault("rmvpe_root", str(assets_root / "rmvpe"))
    os.environ.setdefault("weight_root", str(assets_root / "weights"))
    os.environ.setdefault("index_root", str(source_root / "logs"))
    os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

    return RvcTree(project_root=project_root, source_root=source_root, assets_root=assets_root)


def import_rvc_module(name: str) -> Any:
    """Import a module from the vendored RVC tree.

    :param name: Dotted module path, for example ``infer.module.models``.
    :return: The imported module.
    :raises RvcSourceNotFound: If :func:`locate_rvc_tree` has not run yet.
    """
    try:
        return importlib.import_module(name)
    except ModuleNotFoundError as error:
        raise RvcSourceNotFound(
            f"cannot import {name!r}; call locate_rvc_tree() before importing RVC modules"
        ) from error


@dataclass(frozen=True, slots=True)
class VoiceCheckpoint:
    """A trained RVC voice, as stored in a ``.pth`` produced by training.

    :ivar name: Voice name, taken from the weights filename.
    :ivar weights: The generator state dict.
    :ivar config: Positional synthesiser arguments, see :data:`CHECKPOINT_CONFIG_FIELDS`.
    :ivar sample_rate: Rate the generator synthesises at, in hertz.
    :ivar has_pitch_conditioning: Whether the generator takes an F0 input. RVCARA
        requires this: a model without pitch conditioning cannot follow a sung line.
    :ivar version: RVC feature version, ``v1`` (256-D) or ``v2`` (768-D).
    :ivar info: Free-text training note, for example ``200epoch``.
    """

    name: str
    weights: dict[str, torch.Tensor]
    config: list[Any]
    sample_rate: int
    has_pitch_conditioning: bool
    version: str
    info: str

    @property
    def feature_dim(self) -> int:
        return 768 if self.version == "v2" else 256

    @property
    def num_speakers(self) -> int:
        return int(self.config[CHECKPOINT_CONFIG_FIELDS.index("spk_embed_dim")])

    @property
    def upsample_rates(self) -> list[int]:
        return list(self.config[CHECKPOINT_CONFIG_FIELDS.index("upsample_rates")])

    @property
    def upsample_factor(self) -> int:
        """Samples of output per content frame, the product of the upsample rates."""
        factor = 1
        for rate in self.upsample_rates:
            factor *= int(rate)
        return factor

    def describe(self) -> dict[str, Any]:
        """Return the checkpoint's architecture as a JSON-friendly mapping."""
        return dict(zip(CHECKPOINT_CONFIG_FIELDS, self.config))


def load_voice_checkpoint(weights_path: Path) -> VoiceCheckpoint:
    """Read a trained ``.pth`` without instantiating the network.

    :param weights_path: Path to the trained generator weights.
    :return: The parsed checkpoint.
    :raises ValueError: If the file is not an RVC checkpoint, or is a variant RVCARA
        cannot serve — a v1 model, or one trained without pitch conditioning.
    """
    checkpoint = torch.load(weights_path, map_location="cpu", weights_only=False)

    missing = {"weight", "config", "sr", "f0", "version"} - set(checkpoint)
    if missing:
        raise ValueError(
            f"{weights_path.name} is not an RVC checkpoint; missing keys: {sorted(missing)}"
        )

    version = str(checkpoint["version"])
    if version != "v2":
        raise ValueError(
            f"{weights_path.name} is an RVC {version} model. RVCARA supports v2 (768-D) only, "
            "because the v1 path needs the HuBERT final projection that v2 discards."
        )

    if int(checkpoint["f0"]) != 1:
        raise ValueError(
            f"{weights_path.name} was trained without pitch conditioning. "
            "RVCARA needs an F0-conditioned generator to follow the source melody."
        )

    sample_rate = checkpoint["config"][CHECKPOINT_CONFIG_FIELDS.index("sr")]

    return VoiceCheckpoint(
        name=weights_path.stem,
        weights=checkpoint["weight"],
        config=list(checkpoint["config"]),
        sample_rate=int(sample_rate),
        has_pitch_conditioning=True,
        version=version,
        info=str(checkpoint.get("info", "")),
    )
