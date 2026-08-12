# Naming and style

This document is the single authority for identifier naming in RVCARA. It exists
because the project sits at the junction of five vocabularies — plugin framework,
signal processing, machine learning, mathematics and music theory — each with its
own established conventions. Where those conventions disagree, the rule below
says which one wins.

The guiding principle: **a reader who knows the domain should recognise the name
without a comment, and a reader who knows only C++ should still be able to
pronounce it.**

## 1. Precedence

When two conventions conflict, apply the first rule that matches:

1. **JUCE house style** governs everything written in C++. RVCARA is a JUCE
   plugin; matching the framework it is built on beats matching any external
   guide. See [JUCE coding standards](https://juce.com/blog/coding-standards/).
2. **The originating API's spelling is preserved at the boundary.** ARA entry
   points, ONNX graph tensor names and file-format keys keep the spelling their
   own specification uses, even where that clashes with rule 1. The mismatch is
   confined to one adapter layer and never leaks inward.
3. **The domain's published term wins over a paraphrase.** `fundamentalFrequency`
   and `cents` are not renamed to something more "programmer-ish"; a reader from
   music information retrieval must recognise them.

## 2. C++ — JUCE house style

| Kind | Convention | Example |
| --- | --- | --- |
| Namespace | lowercase, one word | `rvcara` |
| Class, struct, enum class | `PascalCase` | `ConversionEngine`, `MelSpectrogram` |
| Type alias | `PascalCase` | `using SampleBuffer = juce::AudioBuffer<float>;` |
| Template parameter | `PascalCase`, descriptive | `SampleType`, not `T` |
| Function, method | `camelCase`, verb first | `estimatePitch`, `getNumFrames` |
| Variable (local, member, parameter) | `camelCase` | `sampleRate`, `numFrames` |
| `constexpr` constant | `camelCase` | `constexpr int contentFrameRate = 50;` |
| Enumerator | `camelCase` | `enum class F0Method { rmvpe, fcpe, pm };` |
| Macro | `SCREAMING_SNAKE_CASE`, prefixed | `RVCARA_ASSERT` |
| File | `PascalCase`, one primary type per file | `PitchEstimator.h` |
| Directory | lowercase | `src/`, `res/`, `libs/` |

Rules that follow from JUCE style and are enforced here:

- **No leading or trailing underscores, ever.** JUCE forbids them; a leading
  underscore is reserved for the standard library at file scope and reads as
  foreign in user code. Member variables are *not* decorated — if a function is
  long enough that you cannot tell a member from a local, the function is too
  long.
- **`#pragma once`**, never include guards.
- **Allman braces, four spaces**, no tabs.
- **No `k` prefix on constants.** `kSampleRate` is Google style, not JUCE style.
- **`num` prefixes counts**, and the plural names a container:
  `numChannels` is a count, `channels` is the collection.
- **Booleans read as predicates**: `isVoiced`, `hasIndex`, `shouldNormalise`,
  `canRenderInPlace`.
- **Getters and setters are `getFoo` / `setFoo`**, matching JUCE, including for
  trivial accessors.
- `const` on everything that does not change; `auto` only where the type is
  evident from the initialiser or spelling it adds nothing.

### Class member order

Public interface, then protected, then private data — C++ Core Guidelines
[NL.16](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#nl16-use-a-conventional-class-member-declaration-order).
The reader wants the contract before the implementation.

## 3. Units and quantities

Ambiguity about units is the most expensive class of naming bug in audio code,
so **the unit is part of the name whenever the quantity has more than one
plausible unit**:

| Quantity | Name | Never |
| --- | --- | --- |
| Frequency in hertz | `frequencyHz`, `pitchHz`, `cutoffHz` | `freq` |
| Duration in seconds | `durationSeconds` | `duration`, `time` |
| Duration in samples | `lengthInSamples`, `hopSizeInSamples` | `length` |
| Position on a timeline | `positionInSamples`, `startTimeSeconds` | `pos` |
| Level, logarithmic | `gainDecibels` | `gain`, `db` |
| Level, linear | `gainLinear`, `amplitude` | `gain` |
| Pitch interval | `pitchShiftSemitones`, `detuneCents` | `shift` |

Exceptions, both narrow:

- `sampleRate` is unqualified. It is hertz throughout audio software and
  `sampleRateHz` reads as noise.
- A ratio in `[0, 1]` needs no unit but must say what it is a ratio *of*:
  `retrievalRatio`, `dryWetRatio`.

## 4. Digital signal processing

The JUCE vocabulary is the reference, because callers cross between our code and
`juce::dsp` constantly.

| Concept | Name |
| --- | --- |
| Frames in a block | `numSamples` |
| Channel count | `numChannels` |
| Largest block the host may ask for | `maximumBlockSize` |
| Analysis hop | `hopSizeInSamples` |
| Window length, transform length | `windowSize`, `fftSize` |
| Spectrum bin count | `numBins` (`fftSize / 2 + 1`) |
| Per-bin values | `magnitude`, `phase`, `binIndex` |
| Half the sample rate | `nyquistHz` |
| Filter shape | `cutoffHz`, `qFactor`, `filterOrder` |

Loop variables over these axes are named for their axis rather than `i`:
`sampleIndex`, `channelIndex`, `frameIndex`, `binIndex`. A bare `i` is
acceptable only in a loop short enough to read in one glance.

Frame-versus-sample discipline: **`sample` is one audio datum, `frame` is one
analysis window's worth of them.** The conversion engine works in frames at
100 Hz and samples at 16 kHz and 40 kHz simultaneously; mixing the two words
is how that goes wrong. Any variable holding a count on a specific rate is
suffixed accordingly when more than one rate is in scope:
`numFramesAtContentRate`, `numSamplesAt16k`.

## 5. Machine learning

C++ identifiers follow §2 — `numFrames`, `featureDim`, not `n_frames`. Only two
things keep ML's `snake_case`, and both are data crossing a boundary rather than
code:

1. **ONNX graph input and output names**, because they are produced by
   `torch.onnx.export` in the exporter and matched by string in C++. They are
   never retyped as literals in C++: the engine binds them through
   `manifest.json`, so a third-party RVC ONNX export with different tensor names
   can be loaded by editing the manifest rather than the source.
2. **Manifest keys**, which mirror the exporter's spelling verbatim.

Tensor conventions:

- Every tensor variable documents its layout in a trailing comment using
  bracketed axis names — `// [batch, numFrames, featureDim]`. Axis order is
  never left implicit.
- `features` is the content-encoder output; `salience` is the pitch network's
  per-bin activation; `latent` is a sampled latent; `logits` is pre-activation.
- Model roles are named for what they do, not what they are:
  `ContentEncoder`, `PitchEstimator`, `Vocoder` — not `HubertModel`,
  `RmvpeModel`, `NsfHifiGan`. The concrete architecture is an implementation
  detail recorded in the manifest, and the plugin should keep working when it
  changes.
- `numMelBins`, `numPitchBins`, `featureDim`, `hiddenDim`, `speakerId`.

## 6. Mathematics and physics

- Mathematical symbols from a published formula may be used **inside the
  function that implements that formula**, where the paper's notation is the
  clearest possible name, and only when the docstring cites the formula. So
  `mel = 1127 * std::log(1.0 + hz / 700.0)` may use `hz` and `mel`, and a
  Butterworth section may use `a` and `b` for its coefficient arrays, because
  every filter text calls them that. Outside such a scope, spell the word.
- Never `l`, `O` or `I` as an identifier — indistinguishable from `1` and `0`.
- Physical and mathematical constants are `constexpr` and named in full:
  `constexpr double speedOfSoundMetresPerSecond = 343.0;`
- Prefer `std::numbers::pi` to a hand-rolled constant.
- Conversion functions read `fromTo`: `hzToMel`, `melToHz`, `hzToCents`,
  `centsToHz`, `decibelsToGain`. The direction is unambiguous in the name, so
  no caller has to guess.

## 7. Music theory and MIR

The published term wins, even when it is longer:

| Concept | Name | Note |
| --- | --- | --- |
| Fundamental frequency | `fundamentalFrequencyHz`, or `f0Hz` locally | `f0` is universal in MIR and permitted in tight scopes |
| Voicing decision | `isVoiced`, `voicedProbability` | |
| Unvoiced | `isUnvoiced` | not `uv` |
| Interval, hundredths of a semitone | `cents` | |
| Transposition | `pitchShiftSemitones` | |
| Note number | `midiNote` | integer 0–127, `midiNoteFractional` when continuous |
| Spectral character | `timbre`, `formantShift` | |

## 8. Mapping from RVC's vocabulary

Upstream RVC uses terse Python names, several of them opaque. The engine renames
them once, at the boundary, and uses the clear name everywhere inside. This table
is the contract between the two projects — a reader comparing our C++ to
`infer/vc/pipeline.py` needs it.

| RVC | RVCARA | Meaning |
| --- | --- | --- |
| `f0_up_key` | `pitchShiftSemitones` | Transposition applied to the estimated pitch |
| `f0_coarse` | `coarsePitchBin` | Pitch quantised to 1–255 mel bins, the generator's pitch embedding index |
| `f0bak`, `pitchf` | `fundamentalFrequencyHz` | Continuous pitch in hertz, the NSF source excitation |
| `index_rate` | `retrievalRatio` | How far to move features toward the retrieved timbre |
| `protect` | `consonantProtection` | Keeps unvoiced frames closer to the source features |
| `rms_mix_rate` | `envelopeFollowRatio` | How much of the source loudness envelope to restore |
| `phone` | `contentFeatures` | Content-encoder output fed to the generator |
| `p_len` | `numContentFrames` | |
| `sid`, `ds` | `speakerId` | |
| `t_pad` | `contextPaddingInSamples` | Reflected context added before analysis |
| `x_max`, `t_center`, `t_query` | `maximumChunkSeconds`, `chunkStrideSeconds`, `splitSearchRadiusSeconds` | Long-input splitting |
| `tgt_sr` | `modelSampleRate` | The rate the generator synthesises at |
| `net_g` | `Vocoder` | |
| `hubert` | `ContentEncoder` | |
| `rmvpe` | `PitchEstimator` | |

## 9. Computer science and structure

- Suffix a type with the pattern it implements only when that is the type's
  whole point: `VoiceModelLibrary` (a registry), `ConversionCache`,
  `DocumentController` (ARA's term). No `FooManager`, `FooHelper`, `FooUtils` —
  they describe nothing.
- Ownership is spelled by the type, not the name: `std::unique_ptr<VoiceModel>`
  is `model`, never `modelPtr`.
- Lock-free single-producer/single-consumer structures are named for the
  discipline they require, because the caller must honour it: `SpscRingBuffer`.
- Atomic members are named for the value, not the mechanism: `renderProgress`,
  not `atomicProgress`. The type already says it.
- Collections are plural (`playbackRegions`); maps read
  `<key>To<value>`: `sourceToConversion`.

## 10. Files, directories and assets

- All C++ lives flat in `src/`, in one `rvcara` namespace. An earlier layout split it into
  `dsp/`, `engine/`, `ara/`, `gui/` and `plugin/` with a namespace each; for around forty
  files that was more ceremony than navigation aid, and every cross-layer reference paid for
  it twice — once in the include path and once in the qualification.
- `res/` holds runtime resources, chiefly the exported voices under `res/models/`.
- `libs/` holds submodules only, each named exactly as its upstream repository —
  `JUCE`, `ARA_SDK`, `hnswlib`, `Catch2` — so it is obvious what a directory is
  without opening `.gitmodules`.
- Exported model assets are lowercase with a `snake_case` role:
  `content_encoder.onnx`, `pitch_estimator.onnx`, `vocoder.onnx`,
  `retrieval.bin`, `manifest.json`. They are data read by both languages, so
  they follow the data convention, not the C++ one.
