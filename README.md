# RVCARA

Retrieval-based voice conversion as an audio plug-in. Drop it on a vocal track and the vocal
is re-sung in the trained voice — offline, on the CPU, with no Python at run time and nothing
sent to a server. The voice loads by itself; there is nothing to choose.

The model is [RVC](https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI)
v2. Voices are trained and exported with the sibling
[RTVoice](https://github.com/vivekvjyn/RTVoice) project; this repository is the plug-in
only, and contains no Python at all.

## Two ways it runs

RVC is **not causal**. Its content encoder is a twelve-layer transformer over the whole
utterance, its pitch estimator is a U-Net that sees the future, and its vocoder is
conditioned on a pitch track gap-filled end to end. It cannot convert audio as it streams
past. Measured on sixteen cores, a 0.25 s window costs *more* than 0.25 s to convert; only
past about a second of window does the cost drop to roughly half of real time, and by then
the latency and the crossfade seams have cost more quality than they saved.

So conversion is always offline. What differs is how the plug-in gets hold of the audio.

**ARA mode** — the good one. [ARA](https://github.com/Celemony/ARA_SDK) has the host grant
random access to the whole region up front, so the model sees the entire performance,
converts once on a background thread, and playback becomes a memory read. Region edits,
per-region settings and session persistence all work properly. Same arrangement Melodyne and
[IK Multimedia's ReSing](https://www.ikmultimedia.com/products/resing/) use.

**Insert mode** — for hosts with no ARA. Drop it on the vocal and press play:

1. The plug-in captures the audio against the host timeline as it goes, passing the dry
   signal through so you hear something.
2. Shortly after the transport stops — or when you press **Convert** — the captured take is
   converted at full quality, the whole phrase in view, exactly as the ARA path does it.
3. On the next pass the converted voice plays back in place, sample aligned, with no added
   latency and no seams.

The only cost is that the first pass is dry. Changing a parameter re-converts immediately from
the existing capture, so you do not have to replay to audition a change.

Neither mode is usable for live monitoring, and no amount of engineering makes this class of
model suitable for it.

## Layout

```
src/
  Processor.{h,cpp}    the plug-in
  Editor.{h,cpp}       the panel
  common/              shared value types and readers: ConversionSettings, BinaryMatrix
  dsp/                 framework-free signal processing: resampler, filter, mel, pitch maths
  model/               the voice and its inference: manifest, sessions, engine, loader
  ara/                 the ARA path: document controller, playback renderer, modification
  insert/              the non-ARA path: capture and render in place
  ui/                  the look and feel and the pitch display
res/                   runtime resources; exported voices live in res/models/
libs/                  submodules: JUCE, ARA_SDK, onnxruntime, googletest, hnswlib
tests/                 GoogleTest unit tests and fixtures
docs/                  Doxyfile; `cmake --build build --target rvcara_docs` writes docs/html
```

Only the two JUCE entry points sit at the top of `src/`. Everything else is grouped by the
layer it belongs to, and includes are path-qualified — `#include "dsp/SincResampler.h"` — so a
file's dependencies name their layer.

The code carries no comments. What a name cannot say is either documented as Doxygen on the
declaration or written down here.

`dsp/`, `common/` and `model/` know nothing about ARA or about plug-in formats: they take a
buffer and return a buffer, which is why the tests link them directly without instantiating a
plug-in. `ara/` and `insert/` are the two ways audio arrives, and both drive the same engine
through the same `VoiceLoader`.

Pure C++ and CMake. Nothing interpreted at run time, and nothing of ours interpreted at build
time — the one exception is ONNX Runtime's own build, below.

## Building

Requires CMake 3.24+, a C++20 compiler, and the submodules. Two of them are superprojects
whose own contents are submodules, so the extra inits matter:

```sh
git clone https://github.com/vivekvjyn/RVCARA.git
cd RVCARA
git submodule update --init --depth 1
git -C libs/ARA_SDK submodule update --init --depth 1 ARA_API ARA_Library
git -C libs/onnxruntime submodule update --init --depth 1 cmake/external/onnx

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

**Every dependency is a submodule, ONNX Runtime included, so the commits this repository names
are the whole of what it runs.** That has a price worth knowing before the first build:

- ONNX Runtime takes 25–45 minutes to compile, once. It is built into
  `build/onnxruntime-install` and reused; rebuilding the plug-in does not touch it.
- Its build needs a **Python 3.10+ interpreter**, because its CMake generates the export
  symbol list by running `tools/ci_build/gen_def.py`. Nothing in RVCARA needs Python and
  nothing runs at plug-in load; this is ONNX Runtime's build requirement, and CMake checks
  for it up front.
- Its configure step reaches the network: ONNX Runtime fetches abseil, protobuf,
  flatbuffers, re2, eigen, cpuinfo and the rest from `libs/onnxruntime/cmake/deps.txt`
  rather than vendoring them.
- Its build needs about 6 GB of disk and, at ten parallel jobs, around 8 GB of memory.

`-DRVCARA_ONNXRUNTIME_ROOT=<path>` skips all of that and uses an existing installation —
including one of Microsoft's published archives.

**VST3 and LV2 are built everywhere, AU on macOS only** — the format list follows the host
operating system, locally and in CI alike. Artefacts land in `build/RVCARA_artefacts/`, each
with the ONNX Runtime shared library and its soname chain copied beside it, and `$ORIGIN` in
the runpath so they are found there.

On Linux, JUCE needs the usual development packages — ALSA, FreeType, Fontconfig, X11 and
GL. See `libs/JUCE/docs/Linux Dependencies.md`.

## Voices

The plug-in loads **exported assets**, not `.pth` checkpoints. A voice is a directory under
`res/models/<name>/` containing:

| File | What it is |
| --- | --- |
| `content_encoder.onnx` | ContentVec-initialised HuBERT, 768-D at 50 Hz |
| `pitch_estimator.onnx` | RMVPE, log-mel in, 360-class pitch salience out |
| `vocoder.onnx` | The trained voice: VITS flow into an NSF HiFi-GAN |
| `mel_filter_bank.bin` | librosa's HTK mel bank, so the front end cannot drift |
| `retrieval.bin` | The training set's content frames, the timbre codebook |
| `manifest.json` | Every pipeline constant, plus provenance |

Producing that directory from a trained checkpoint requires tracing the networks with
PyTorch, so it happens in [RTVoice](https://github.com/vivekvjyn/RTVoice) rather than here —
this repository stays free of Python. The exporter previously lived in `tools/`; it is
recoverable from history with `git checkout b6ea4bd -- tools` if you need it before it lands
upstream.

The plug-in searches, in increasing precedence: the shared application data directory, the
user's application data directory, this working copy's `res/models` — compiled in at
configure time, since a plug-in is loaded from the host's plug-in folder and cannot reach the
repository by any relative path — and any path in `RVCARA_MODEL_PATH`.

**Nothing has to be chosen.** An instance loads the first voice it finds as soon as the host
means to use it, and converts with it; the voice menu exists only for switching between
several. In insert mode that means the only thing the user does is play the track.

### Why nothing is compiled in

Every constant the pipeline needs is in `manifest.json` — sample rates, hop size, feature
width, latent width, mel parameters, salience decoding, retrieval search parameters, filter
coefficients. A model trained at 32 kHz or 48 kHz, or with a different feature width, loads
without a plug-in release. The ONNX tensor names are read from the manifest too, so a
third-party RVC export whose tensors are called `phone` and `pitchf` can be described by
editing JSON rather than source.

## The panel

There are no knobs. The panel is a header, a display and a footer: which voice is loaded, the
melody the model followed over the loudness it produced, and what the conversion is doing. The
voice loads itself and the take converts itself, so a control would be one more thing to set
that the plug-in has already decided.

The parameters below still exist — the host can automate every one, and reaches them through
the generic parameter view it provides for any plug-in. They are simply not on the panel,
because tuning them is not what using this is like.

| Parameter | RVC name | What it does |
| --- | --- | --- |
| Pitch | `f0_up_key` | Transposes the estimated melody, in semitones |
| Timbre | `index_rate` | How far to move toward the trained singer's timbre |
| Consonants | `protect` | Keeps unvoiced frames nearer the source features |
| Dynamics | `rms_mix_rate` | Restores the source's loudness contour |
| Variation | — | Seeds the vocoder's latent; changes the performance, not the notes |
| Bypass | — | On the panel, next to the voice |

Consonant protection is inert for models whose pitch track is gap-filled, which is all of
them — the reference pipeline interpolates across unvoiced frames, so nothing is left below
1 Hz for the blend to act on. The control is kept because it matches upstream; see
`PitchEstimator::fillUnvoicedGaps`.

## Performance

Where the time actually goes, measured stage by stage on a Ryzen 7 5800HS (8 cores, 16
hardware threads) converting 37 seconds of vocal in 21.1 s — **0.57× real time**:

| Stage | Time | Share |
| --- | --- | --- |
| Vocoder | 15.05 s | **71.3%** |
| Content encoder | 3.31 s | 15.7% |
| Pitch estimator | 1.36 s | 6.4% |
| Resample out | 0.48 s | 2.3% |
| Retrieval (approximate) | 0.46 s | 2.2% |
| Resample in | 0.44 s | 2.1% |
| High-pass, padding, frame expansion | 0.02 s | 0.1% |

The vocoder's 400× upsampling to 40 kHz is the whole cost. Nothing outside it is worth
optimising: making the resamplers, the filter and the retrieval search *infinitely* fast
would buy 7%.

**One optimisation was worth taking.** ONNX Runtime's default intra-op thread count is the
number of hardware threads; pinning it to the number of *physical* cores is 8% faster
end to end — 6.1 s against 5.6 s on the same 8.5-second phrase — because the matrix kernels
already saturate each core's vector units, so a second thread on the same core only adds
contention. Setting denormals-as-zero made no measurable difference; there are none to flush.

Beyond that, the remaining levers all cost something other than time: int8 dynamic
quantisation of the vocoder (which belongs in the exporter, and risks audible artefacts in a
generative vocoder), a smaller vocoder, or caching the content features so that a re-render
after a parameter change skips the encoder — worth at most the encoder's 16%, and only on a
re-render.

A warm voice load is 2 s. The first ever load of a voice is around 45 s, because the retrieval
graph is built and then cached beside the model.

Retrieval uses [hnswlib](https://github.com/nmslib/hnswlib) rather than exact search. The
codebook for the reference voice holds 61,893 768-dimensional vectors; exact search is
about 9.5 GFLOP per second of audio and scales with training set size, while an approximate
graph answers in microseconds. The eight neighbours are averaged with inverse-fourth-power
weights, so the nearest one or two dominate and a miss in the tail is inaudible.

## Verification status

Honest about what has and has not been checked:

- **Verified.** 38 GoogleTest cases pass. The exported assets match the reference implementation on real audio: pitch
  within 1.5–6.8 cents, correlation ≥ 0.999, log-mel distance 0.30–0.60, durations
  sample-exact. The C++ DSP is checked against NumPy and SciPy through the committed
  fixtures in `tests/fixtures` — the mel spectrogram to 1e-3, the zero-phase high-pass to
  1e-5.
- **Verified end to end, outside a host.** The plug-in's own engine — discovery, load and the
  whole conversion — was run on two sung phrases and compared against the reference render of
  the same files: log-spectrogram correlation 0.93 and 0.91 against the reference, against
  0.64 and 0.71 for the untouched source, with matching loudness. A 37-second phrase converts
  in 21 seconds on sixteen cores, 0.58× real time, and a warm voice load takes 2 seconds.
- **Built and loads.** The VST3 builds with ARA enabled, exports its factory, and `dlopen`s
  cleanly.
- **The from-source ONNX Runtime agrees with the prebuilt.** Converting the same phrase
  through both gives a correlation of 0.999937, peak difference 2.5e-2 — not bit-identical,
  because MLAS selects different kernels for the machine it was compiled on, and floating
  point addition is not associative.
- **Not yet verified.** End-to-end behaviour in a real host — ARA region editing, session
  persistence, and insert-mode capture alignment against the timeline. That needs a DAW, or
  JUCE's AudioPluginHost built with `JUCE_PLUGINHOST_ARA=1`.

## Conventions

This codebase sits across plug-in framework, DSP, machine learning, mathematics and
music-theory vocabularies. Where they disagree, the order of precedence is: JUCE house style
for anything written in C++, the originating API's spelling at a boundary, then the field's
published term over a paraphrase.

Formatting is JUCE house style — Allman braces, four spaces, `foo (bar)` — applied by hand
rather than by a checked-in `clang-format` configuration.

Names follow the framework and the field they belong to, in that order: JUCE house style for
C++ (`PascalCase` types and files, `camelCase` functions and variables, `num` for counts, no
member decoration, no `k` prefix), the originating API's spelling at a boundary (ARA entry
points, ONNX tensor names, manifest keys), and the published term over a paraphrase everywhere
else — `fundamentalFrequency`, `cents`, `retrievalRatio`. Colours are named for their role
rather than their hue, `describe...` returns text meant for a human, and directories are the
layers listed above.

### Warnings

Two tiers, split by ownership rather than by taste. Every target gets `-Wall -Wextra
-Wpedantic` plus `-Wshadow`, `-Wnon-virtual-dtor`, `-Wcast-align` and `-Woverloaded-virtual`,
which JUCE's own sources pass. A strict tier — `-Wconversion`, `-Wsign-conversion`,
`-Wdouble-promotion`, `-Wfloat-conversion`, `-Wold-style-cast`, `-Wuseless-cast`,
`-Wcast-qual`, `-Wextra-semi` — goes on the translation units that include no third-party
header, which is why the DSP core is kept free of framework headers.

A warning flag judges every header a unit includes, and JUCE's contain 6,166 old-style casts
and 116 float promotions of their own. Nothing here silences them: the flags simply are not
applied to units that include JUCE, and the cost is that those files are checked less
strictly. Checked by hand with the strict set appended and the output filtered to `src/`,
**every source in this repository is clean under the full strict tier.**

A full build emits two warnings, both `-Wmaybe-uninitialized` inside JUCE's vendored copy of
harfbuzz, both GCC 16 false positives in code that is not ours to change. They are left
visible rather than switched off.

## Licence

MIT, see `LICENSE`. The submodules keep their own licences — JUCE is dual AGPL/commercial,
the ARA SDK carries Celemony's licence, and a plug-in built from this repository is subject
to both.
