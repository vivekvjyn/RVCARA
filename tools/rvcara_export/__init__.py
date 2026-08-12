"""Convert a trained RVC voice model into the assets the RVCARA plugin loads.

The plugin performs inference with ONNX Runtime on the CPU and never imports
Python, so every part of the reference pipeline in ``infer/vc/pipeline.py`` has to
be turned into one of three things:

* an ONNX graph, for the three neural networks (:mod:`content_encoder`,
  :mod:`pitch_estimator`, :mod:`vocoder`);
* a binary data file, for the two constant matrices — the retrieval codebook
  (:mod:`assets`) and the mel filter bank (:mod:`pitch_estimator`);
* a documented constant in ``manifest.json`` (:mod:`manifest`), for everything
  the C++ engine must reproduce arithmetically.

Nothing about the pipeline is left implicit in the C++: if the engine needs a
number, the manifest carries it, so a differently trained model stays loadable
without recompiling the plugin.
"""

from .manifest import MANIFEST_SCHEMA_VERSION, ModelManifest

__all__ = ["MANIFEST_SCHEMA_VERSION", "ModelManifest", "__version__"]

__version__ = "0.1.0"
