# RVCARA

Retrieval-based voice conversion as an ARA plug-in. Drop it on a vocal track in an
ARA-capable DAW, pick a voice, and the region is re-sung in that voice — offline, on the
CPU, with no Python at run time and nothing sent to a server.

The model is [RVC](https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI)
v2. Voices are trained with the sibling [RTVoice](https://github.com/vivekvjyn/RTVoice)
project and converted into inference assets by the exporter in `tools/`.

## Why ARA

RVC is not causal. Its content encoder is a twelve-layer transformer over the whole
utterance, its pitch estimator is a U-Net that sees the future, and its vocoder is
conditioned on a pitch track that has been gap-filled end to end. Running it as an ordinary
insert effect means a sliding window, several hundred milliseconds of latency, and audibly
worse output at the seams.

[ARA](https://github.com/Celemony/ARA_SDK) removes the problem instead of working around
it. The host grants the plug-in random access to the entire audio region up front, so the
model sees the whole performance, converts it once on a background thread, and playback
becomes a memory read. This is the same arrangement Melodyne and
[IK Multimedia's ReSing](https://www.ikmultimedia.com/products/resing/) use, and for the
same reason.

Loaded **without** ARA, the plug-in passes audio through unchanged and says so in its
interface. That is deliberate: a degraded streaming mode presented as the same product
would mislead.

## Layout

```
source/
  dsp/       Resampling, zero-phase filtering, mel spectrogram, pitch mathematics
  engine/    Manifest, ONNX sessions, retrieval, pitch estimation, the pipeline
  ara/       Document controller, audio modification, playback renderer
  gui/       Pitch curve view
  plugin/    AudioProcessor, editor, plug-in and ARA factory entry points
tools/       Python exporter: checkpoint to ONNX assets, plus the reference pipeline
tests/       Catch2 unit tests and cross-language fixtures
libs/        Submodules: JUCE, ARA SDK, hnswlib, Catch2
docs/        Naming conventions and architecture
```

The engine knows nothing about ARA or about plug-in formats; it takes a buffer and returns
a buffer, which is why the tests can link it directly.

## Building

Requires CMake 3.24+, a C++20 compiler, and the submodules. The ARA SDK is a superproject
whose own contents are submodules, so the recursive init matters:

```sh
git clone https://github.com/vivekvjyn/RVCARA.git
cd RVCARA
git submodule update --init --depth 1
git -C libs/ara-sdk submodule update --init --depth 1 ARA_API ARA_Library

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

ONNX Runtime is not a submodule: the pinned prebuilt release is fetched at configure time
and checked against a recorded SHA256. Building it from source takes tens of minutes and
buys nothing, since only the CPU execution provider is used. Point
`RVCARA_ONNXRUNTIME_ROOT` at an existing installation to override.

Artefacts land in `build/source/plugin/RVCARA_artefacts/`, with the ONNX Runtime shared
library copied beside each one.

On Linux, JUCE needs the usual development packages — ALSA, FreeType, Fontconfig, X11 and
GL. See `libs/juce/docs/Linux Dependencies.md`.

## Preparing a voice

The plug-in loads exported assets, not `.pth` checkpoints. Convert a trained RVC model
once:

```sh
cd tools
python -m rvcara_export female1 --rvc-root ~/Desktop/RVC --verify --verbose
```

That writes `assets/models/female1/` containing:

| File | What it is |
| --- | --- |
| `content_encoder.onnx` | ContentVec-initialised HuBERT, 768-D at 50 Hz |
| `pitch_estimator.onnx` | RMVPE, log-mel in, 360-class pitch salience out |
| `vocoder.onnx` | The trained voice: VITS flow into an NSF HiFi-GAN |
| `mel_filter_bank.bin` | librosa's HTK mel bank, so the front end cannot drift |
| `retrieval.bin` | The training set's content frames, the timbre codebook |
| `manifest.json` | Every pipeline constant, plus provenance |

`--verify` converts the reference renders in the RVC project's `output/` directory through
the exported assets and compares pitch, spectral envelope and duration against them. For
the reference model that reports pitch agreement within a few cents and correlation above
0.999.

The plug-in searches, in increasing precedence: the shared application data directory, the
user's application data directory, `assets/models` near the binary, and any path in
`RVCARA_MODEL_PATH`.

### Why nothing is compiled in

Every constant the pipeline needs is in `manifest.json` — sample rates, hop size, feature
width, latent width, mel parameters, salience decoding, retrieval search parameters, filter
coefficients. A model trained at 32 kHz or 48 kHz, or with a different feature width, loads
without a plug-in release. The ONNX tensor names are read from the manifest too, so a
third-party RVC export whose tensors are called `phone` and `pitchf` can be described by
editing JSON rather than source.

## Controls

| Control | RVC name | What it does |
| --- | --- | --- |
| Pitch | `f0_up_key` | Transposes the estimated melody, in semitones |
| Timbre | `index_rate` | How far to move toward the trained singer's timbre |
| Consonants | `protect` | Keeps unvoiced frames nearer the source features |
| Dynamics | `rms_mix_rate` | Restores the source's loudness contour |
| Variation | — | Seeds the vocoder's latent; changes the performance, not the notes |

Consonant protection is inert for models whose pitch track is gap-filled, which is all of
them — the reference pipeline interpolates across unvoiced frames, so nothing is left below
1 Hz for the blend to act on. The control is kept because it matches upstream; see
`PitchEstimator::fillUnvoicedGaps`.

## Performance

Measured on 16 cores, converting 37 seconds of vocal:

| Stage | Time | Relative to real time |
| --- | --- | --- |
| High-pass | 0.01 s | 3200× |
| Pitch estimator | 1.14 s | 32× |
| Content encoder | 3.35 s | 11× |
| Retrieval (approximate) | ~0.05 s | ~700× |
| Vocoder | 16.8 s | 2.2× |

About 1.5× real time overall, dominated by the vocoder's 400× upsampling to 40 kHz. A
three-minute vocal converts in roughly two minutes on a background thread while the
previous render keeps playing.

Retrieval uses [hnswlib](https://github.com/nmslib/hnswlib) rather than exact search. The
codebook for the reference voice holds 61,893 768-dimensional vectors; exact search is
about 9.5 GFLOP per second of audio and scales with training set size, while an approximate
graph answers in microseconds. The eight neighbours are averaged with inverse-fourth-power
weights, so the nearest one or two dominate and a miss in the tail is inaudible.

## Verification status

Honest about what has and has not been checked:

- **Verified.** The exported assets match the reference Python pipeline on real audio:
  pitch within 1.5–6.8 cents, correlation ≥ 0.999, log-mel distance 0.30–0.60, durations
  sample-exact. The C++ DSP is checked against NumPy and SciPy through committed fixtures —
  the mel spectrogram to 1e-3, the zero-phase high-pass to 1e-5. 28 test cases, 847
  assertions.
- **Verified.** Every exported graph is run at several awkward input lengths during export,
  because tracing had silently frozen the vocoder at one frame count.
- **Built and loads.** The VST3 builds with ARA enabled, exports its factory, and `dlopen`s
  cleanly.
- **Not yet verified.** End-to-end behaviour inside a real ARA host — region editing,
  persistence across a session reload, playback alignment against the timeline. That needs
  a DAW, or JUCE's AudioPluginHost built with `JUCE_PLUGINHOST_ARA=1`.

## Conventions

`docs/naming.md` is the authority on identifier naming, and is worth reading before
contributing: this codebase sits across plug-in framework, DSP, machine learning,
mathematics and music-theory vocabularies, and it says which one wins where they disagree.

## Licence

MIT, see `LICENSE`. The submodules keep their own licences — JUCE is dual AGPL/commercial,
the ARA SDK carries Celemony's licence, and a plug-in built from this repository is subject
to both.
