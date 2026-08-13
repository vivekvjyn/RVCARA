Exported voices live here, one directory each, holding `manifest.json` and the
assets it names. A build compiles this path in, so a voice dropped here is found
without an install step, and the plug-in loads the first one it finds.

The plug-in also searches the shared and per-user application data directories —
`RVCARA/models` under each — and any path in `RVCARA_MODEL_PATH`.
