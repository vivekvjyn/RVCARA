# Architecture

## The pipeline

Converting a region runs seven stages. Each is a transcription of
`infer/vc/pipeline.py` in the RVC project, mediated by
`tools/rvcara_export/pipeline.py` — the NumPy implementation that was checked against
reference renders before any C++ was written. When the C++ and the reference disagree, that
Python file is the arbiter.

```
source audio (host rate, stereo)
  │  mix to mono, resample                     SincResampler
  ▼
16 kHz mono
  │  zero-phase 48 Hz high-pass                ZeroPhaseFilter
  ▼
filtered
  │  reflect 3 s of context at each end
  ▼
padded ──────────────┬──────────────────────────────────────┐
                     │                                      │
        log-mel spectrogram                        content encoder
        MelSpectrogram                             content_encoder.onnx
                     │                                      │
        RMVPE salience                             768-D at 50 Hz
        pitch_estimator.onnx                                │
                     │                            retrieval blend
        decode → interpolate → transpose          FeatureRetriever (hnswlib)
        PitchEstimator                                      │
                     │                            repeat ×2 → 100 Hz
      ┌──────────────┴───────────────┐                      │
  coarse bins                  pitch in Hz                   │
  (embedding index)            (excitation)                  │
      └──────────────┬───────────────┴──────────────────────┘
                     ▼
                  vocoder.onnx  (VITS flow → NSF HiFi-GAN, 400× upsample)
                     │
                  40 kHz mono
                     │  trim context, resample to source rate
                     ▼
            cached conversion, sample-aligned with the source region
```

Three things about this diagram are load-bearing:

**Pitch is estimated once over the whole padded signal**, not per chunk. The gap-filling
interpolation that keeps the harmonic excitation phase-continuous through consonants would
restart at every seam otherwise.

**Retrieval happens before the frame-rate doubling.** Half as many queries for an identical
result, since both copies of a duplicated frame retrieve the same neighbours.

**The cache is at the source's sample rate, not the model's.** That makes the renderer's
song-time-to-cache-position mapping the identity, so no rate arithmetic happens per block.

## Why the model is three graphs, not one

The content encoder and pitch estimator are generic — every RVC v2 voice shares them. Only
the vocoder and the retrieval codebook are trained. Splitting them means a second voice
costs 105 MB of vocoder plus its codebook rather than another 700 MB, and it keeps the
front ends replaceable: a better pitch estimator can be dropped in by re-exporting one
graph and editing the manifest.

## Threading

Four contexts, with one lock between them.

| Context | What runs there |
| --- | --- |
| Audio thread | `PlaybackRenderer::processBlock` — a memory copy out of the cache, or a dry read |
| Message thread | Editor, ARA model mutation, publishing finished renders, notifying the host |
| Render pool (1 thread) | Voice loading and conversion |
| Read-ahead thread (shared) | Buffering the dry fallback reads |

`DocumentController` owns a `juce::ReadWriteLock`. Renderers take it with
`ScopedTryReadLock` and clear the buffer if they cannot get it; anything that swaps a voice
or publishes a render takes it for writing. This is the pattern JUCE's own ARA demo uses,
and it is correct here for a specific reason: publishing a render replaces an entire buffer
that the audio thread may be part way through reading.

The render pool has one thread deliberately. ONNX Runtime sessions are not safe to call
concurrently at these options, and each conversion already saturates the cores it is given
— a second worker would contend rather than help.

Renders are cancellable. `ConversionEngine::convert` polls an abort flag between stages and
between chunks, so dragging a slider abandons the in-flight render rather than queueing
behind it.

## What ARA gives us, concretely

- **The whole region, up front.** `ARAAudioSourceReader` reads the source the host owns.
  This is what makes a non-causal model usable.
- **One document controller per session**, shared by every plug-in instance. The voice is
  hundreds of megabytes and the retrieval graph takes seconds to build; that cost is paid
  once.
- **A persistence hook.** Settings and voice choice go into the session archive. Rendered
  audio does not — it is tens of megabytes per region and reproducible from the settings
  and the seed, so a restored session re-renders.
- **A content-change notification.** `notifyContentChanged` tells the host a modification's
  audio changed, so a host that caches aggressively re-reads instead of playing the previous
  render.

## Determinism

ARA expects re-rendering the same region with the same settings to produce the same samples.
The vocoder is stochastic in two places, and both are pinned:

- The **latent sample** is an explicit graph input, generated in C++ from
  `latentNoiseSeed`. That is also what makes the Variation control possible.
- The **harmonic source's initial phase and noise floor** are internal `Random*` operators.
  The exporter rewrites their `seed` attributes after tracing, so ONNX Runtime draws the
  same values every run.

## The one deliberate infidelity

None. Where upstream does something odd, it is reproduced rather than corrected, because the
trained weights were validated against that behaviour:

- Unvoiced frames are interpolated across, which leaves `consonantProtection` with nothing
  to act on. Kept, and documented at the site.
- The high-pass is applied forwards and backwards, squaring its magnitude response so the
  realised filter is steeper than fifth-order.

The one place the C++ is *better* than a naive port is the filter's state initialisation:
starting each biquad section from the steady state for its first sample, as SciPy's
`filtfilt` does, rather than from zero. Starting from zero made a 48 Hz high-pass ring on
the opening of every region for tens of milliseconds. That is a bug fix, not a divergence —
it moved agreement with the reference from 1e-1 to 3e-8.

## Failure behaviour

Everything degrades toward audible output rather than silence:

| Situation | What happens |
| --- | --- |
| No voice chosen | Dry passthrough; the editor asks for a voice |
| Voice still loading | Dry passthrough; progress shown |
| Render in flight | Previous render plays if there is one, otherwise dry |
| Settings changed | Stale render keeps playing, marked out of date |
| Render failed | Dry passthrough; the error is shown |
| Host sample rate ≠ source rate | Dry passthrough, rather than a pitch-shifted conversion |
| Manifest from a newer schema | Refused at load with a message, rather than rendered wrongly |
| Truncated codebook | Refused at load; the header's length is checked against the file |
| Not bound to ARA | Passthrough, and the editor explains why |
