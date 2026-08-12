"""Shared plumbing for turning a torch module into a graph the plugin can load."""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Any, Iterable, Sequence

import onnx
import torch

logger = logging.getLogger(__name__)

# Opset 17 is the floor for the operators used here and the ceiling that the
# ONNX Runtime version pinned in cmake/OnnxRuntime.cmake fully supports.
OPSET_VERSION = 17

# Every operator whose output depends on a random draw. The vocoder contains
# three: the latent sampling, the harmonic source's initial phase, and the
# additive noise floor in the NSF excitation.
RANDOM_OPERATOR_TYPES = frozenset(
    {
        "RandomNormal",
        "RandomNormalLike",
        "RandomUniform",
        "RandomUniformLike",
        "Multinomial",
        "Bernoulli",
    }
)


def export_graph(
    module: torch.nn.Module,
    example_inputs: tuple[torch.Tensor, ...],
    destination: Path,
    *,
    input_names: Sequence[str],
    output_names: Sequence[str],
    dynamic_axes: dict[str, dict[int, str]],
) -> None:
    """Trace ``module`` and write it to ``destination`` as ONNX.

    The TorchScript exporter is requested explicitly. The newer dynamo exporter
    handles ``dynamic_axes`` differently and, for the recurrent and transposed
    convolution stacks in these three networks, produces graphs that ONNX Runtime
    optimises less well.

    :param module: Module to trace; put it in eval mode before calling.
    :param example_inputs: Positional arguments for one forward pass.
    :param destination: File to write; parent directories are created.
    :param input_names: Graph input names, in the order ``example_inputs`` gives them.
    :param output_names: Graph output names.
    :param dynamic_axes: Axes that vary at run time, keyed by tensor name.
    """
    destination.parent.mkdir(parents=True, exist_ok=True)

    export_arguments: dict[str, Any] = {
        "input_names": list(input_names),
        "output_names": list(output_names),
        "dynamic_axes": dynamic_axes,
        "opset_version": OPSET_VERSION,
        "do_constant_folding": True,
    }

    with torch.inference_mode():
        try:
            torch.onnx.export(
                module, example_inputs, str(destination), dynamo=False, **export_arguments
            )
        except TypeError:
            # Torch releases before the dynamo exporter landed have no such keyword.
            torch.onnx.export(module, example_inputs, str(destination), **export_arguments)


def freeze_random_seeds(path: Path, seed: int) -> int:
    """Give every random operator in a graph a fixed seed, in place.

    ARA caches a conversion and expects re-rendering the same region with the same
    settings to produce the same samples; a host that re-renders on every edit
    would otherwise expose the vocoder's stochastic excitation as audible drift.
    Seeding here rather than in C++ keeps the guarantee a property of the asset.

    Each operator gets ``seed + n`` so that two draws in the same graph stay
    independent.

    :param path: Graph to rewrite.
    :param seed: Base seed.
    :return: How many operators were seeded.
    """
    model = onnx.load(str(path))
    num_seeded = 0

    for node in model.graph.node:
        if node.op_type not in RANDOM_OPERATOR_TYPES:
            continue

        attributes = [a for a in node.attribute if a.name != "seed"]
        del node.attribute[:]
        node.attribute.extend(attributes)
        node.attribute.append(onnx.helper.make_attribute("seed", float(seed + num_seeded)))
        num_seeded += 1

    if num_seeded:
        onnx.save(model, str(path))
        logger.info("%s: seeded %d random operator(s)", path.name, num_seeded)

    return num_seeded


def describe_graph(path: Path) -> dict[str, Any]:
    """Summarise a graph's interface, for the manifest and for the export log.

    The engine binds tensors by the names recorded here, so reading them back off
    the file rather than repeating the strings that were passed to the exporter
    means the manifest cannot disagree with the asset.

    :param path: Graph to inspect.
    :return: Mapping with ``file``, ``inputs``, ``outputs`` and ``opsetVersion``.
    """
    model = onnx.load(str(path), load_external_data=False)
    initialiser_names = {initialiser.name for initialiser in model.graph.initializer}

    return {
        "file": path.name,
        "opsetVersion": max(
            (opset.version for opset in model.opset_import if opset.domain in ("", "ai.onnx")),
            default=OPSET_VERSION,
        ),
        "inputs": [
            _describe_value(value)
            for value in model.graph.input
            if value.name not in initialiser_names
        ],
        "outputs": [_describe_value(value) for value in model.graph.output],
    }


def _describe_value(value: onnx.ValueInfoProto) -> dict[str, Any]:
    """Return one graph input or output as ``{name, elementType, shape}``."""
    tensor_type = value.type.tensor_type
    shape: list[Any] = []

    for dimension in tensor_type.shape.dim:
        if dimension.HasField("dim_param"):
            shape.append(dimension.dim_param)
        elif dimension.HasField("dim_value"):
            shape.append(dimension.dim_value)
        else:
            shape.append(None)

    return {
        "name": value.name,
        "elementType": onnx.TensorProto.DataType.Name(tensor_type.elem_type),
        "shape": shape,
    }


def report_sizes(paths: Iterable[Path]) -> str:
    """Format a human-readable size list for the export summary."""
    lines = []
    for path in paths:
        if path.is_file():
            lines.append(f"  {path.name:<28} {path.stat().st_size / 1024 ** 2:>8.1f} MiB")
    return "\n".join(lines)
