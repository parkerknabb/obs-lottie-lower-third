# OBS Lottie Lower Third

OBS Lottie Lower Third is an OBS Studio source plugin for rendering animated Lottie lower-third graphics exported from After Effects.

The source patches configured name/title text into selected Lottie text layers at load time, renders the animation through ThorVG, and manages show, hold, and hide playback using Lottie markers.

## Lottie Conventions

Expected text layer names:

* `NAME`
* `SUBTITLE`

Expected marker names:

* `intro`
* `hold start`
* `hold end` optional
* `pvw time` optional
* `outro`

If `hold end` is missing, the source holds on `hold start`. If `pvw time` is missing, the preview frame falls back to `hold start`.

## Build

This project uses a CMake/buildspec-based OBS plugin layout and vendors ThorVG for Lottie rendering.

Local build details depend on the target platform and OBS dependency setup. The project metadata lives in `buildspec.json`, and the source implementation lives in `src/lottie-lower-third-source.c`.
