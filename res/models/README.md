# Models

Nothing in this directory is committed except this file and the two configurations that
describe the shared analysis models. Everything else is an asset: put it here, or in the user
or shared application data directory, or anywhere named by `RVCARA_MODEL_PATH`.

```
res/models/
    ContentVec/     content encoder, shared by every voice
        config.json
        hubert_base.onnx
    RMVPE/          pitch estimator, shared by every voice
        config.json
        rmvpe.onnx
        mel_filter_bank.bin
    GAME/           note segmenter, downloaded at configure time
        config.json
        encoder.onnx  segmenter.onnx  estimator.onnx  bd2dur.onnx  dur2bd.onnx
    <voice>/        one directory per voice
        manifest.json
        vocoder.onnx
        retrieval.bin
```

A directory is offered as a voice only when it holds a `manifest.json`, so the three shared
models never appear in the selector.

The content encoder and the pitch estimator are not trained per singer, which is why they are
installed once rather than copied into every voice, and why each carries the configuration
that describes it rather than being described by the voices that borrow it. A voice that ships
its own copy of either still loads from its own directory, and is then described by its own
manifest.

`GAME/` comes from the openvpi/GAME release and is fetched by CMake; see
`-DRVCARA_FETCH_NOTE_MODEL`.

The full account of what each file is and how it is found is in `docs/mainpage.dox`.
