# OBS Lottie Lower Third

OBS Lottie Lower Third is an OBS Studio source plugin for rendering animated
Lottie lower-third graphics exported from Adobe After Effects.

The source patches configured name/title text into selected Lottie text layers
at load time, renders the animation through ThorVG, and manages show, hold, and
hide playback using Lottie markers.

## What the Plugin Expects

### Editable text layers

Create two editable After Effects text layers and keep them as text in the
Bodymovin export:

* `NAME`
* `SUBTITLE`

Layer names must match exactly, including capitalization. The plugin uses these
layers for the OBS source properties labeled **Name** and **Title**.

Do not flatten these two layers into a PNG sequence. Logo, texture, and
background animation can be flattened when that produces a cleaner or more
reliable Lottie export, but the editable text should remain separate and above
the flattened artwork.

### Timeline markers

Recommended marker names:

| Marker | Required | Placement | Purpose |
| --- | --- | --- | --- |
| `intro` | No | Beginning of the entrance animation, usually frame 0. | Identifies where the lower third starts animating on. |
| `hold_start` | Yes | First frame where the intro has stopped moving. | Defines the visible hold frame or the start of a hold loop. |
| `hold_end` | No | End of the looping hold animation. | If present and after `hold_start`, the plugin loops the hold area. If omitted, the source holds on `hold_start` as a still frame. |
| `pvw_time` | No | Preview frame for OBS properties, when different from `hold_start`. | Overrides the frame shown while editing source properties. |
| `outro` | Yes | First frame of the exit animation. | Identifies where the hide animation begins. |

The parser also accepts `hold start`, `hold end`, and `pvw time`, but new
exports should use the underscore names above for consistency.

## Preparing an After Effects Lower Third

Use a 16:9 composition width, normally 1920 px wide, and keep the composition
background transparent unless the design intentionally includes a solid panel.

Recommended composition setup:

* Keep all theme variants at the same composition size, position, and safe area
  so switching styles in OBS does not require scene repositioning.
* Keep the animation short and purposeful: animate in, hold, optionally loop
  the hold region, then animate out.
* Use Lottie-friendly shapes where possible.
* Flatten complex logo or background animation into a PNG sequence when the
  original After Effects artwork does not export cleanly.
* Avoid unsupported or fragile After Effects features such as video footage,
  audio, complex effects, 3D cameras, heavy expressions, and unnecessary
  raster-heavy image sequences.

### Helper glyph layers

For reliable editable text rendering, include a hidden helper text layer for
each font used by `NAME` or `SUBTITLE`. Each helper layer should contain every
character the renderer may need after OBS replaces the text.

Recommended helper text:

```text
ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,;:!?"()-_/&+
```

The plugin logs a warning if patched ASCII text uses glyphs that are not embedded
in the exported Lottie `chars` table.

## Exporting with Bodymovin

Open Bodymovin from After Effects with **Window > Extensions > Bodymovin**,
select the final lower-third composition, and choose an output folder for the
exported JSON.

Use a clear filename for each style, for example `WinterReady.json`. OBS shows
the filename without `.json` in the style picker.

Enable these Bodymovin settings for each style:

* **Glyphs**
* **Hidden**
* **Assets > Include in JSON**

Render the JSON, then keep the exported folder structure intact if the export
uses any supporting assets.

## Loading Styles in OBS

1. Open OBS Studio.
2. Go to **Tools > Lottie Lower Third Styles...**.
3. Set **Style directory** to the folder that contains exported `.json` files,
   or add individual JSON files under **Individual style files**.
4. Add a **Lottie Lower Third** source to a scene.
5. Choose the exported style from the **Style** dropdown, or enable
   **Custom File** to browse directly to one JSON file.
6. Enter test values in **Name** and **Title**.
7. Show the source to play the intro. Hide the source to play the outro.

Validate the result in the OBS source before using it live. Confirm text
replacement, marker behavior, transparency, animation timing, font rendering,
punctuation, safe margins, and asset loading.

If OBS feeds another production system, also test the full output path. For
example, verify that alpha/keying, scaling, color, and timing still look correct
after the signal reaches a switcher, capture device, NDI/SDI route, streaming
encoder, or other downstream compositor.

## Quality Control Checklist

Before approving a style, verify:

* The lower third animates in and out cleanly.
* `NAME` and `SUBTITLE` update from the OBS source properties.
* Timeline markers are present, spelled correctly, and placed on the intended
  frames.
* Any hold animation loops cleanly between `hold_start` and `hold_end`.
* If `hold_end` is omitted, the lower third holds as a still frame on
  `hold_start`.
* The background remains transparent.
* Text remains readable over representative background video.
* Theme variations share the same composition size and position.
* JSON and supporting assets load without missing-file errors.
* The graphic performs smoothly in OBS and composites correctly in the final
  output workflow.

## Troubleshooting

| Issue | Likely cause | Fix |
| --- | --- | --- |
| `NAME` or `SUBTITLE` does not update. | Editable text layers are missing, flattened, hidden incorrectly, or named differently than expected. | Confirm the text layers are named exactly `NAME` and `SUBTITLE` and remain separate text layers in the exported Lottie. |
| Animation does not hold correctly. | Timeline markers are missing, misspelled, or placed on the wrong frame. | Check `intro`, `hold_start`, optional `hold_end`, optional `pvw_time`, and `outro`. |
| Hold animation does not loop. | `hold_end` is missing or placed incorrectly. | Add `hold_end` at the end of the loop region. If no loop is needed, omit it so the plugin holds on `hold_start`. |
| Fonts or characters look wrong or missing. | Required glyphs were not embedded in the Lottie export. | Add hidden helper text layers for each font, enable Bodymovin glyph export, and re-export. |
| Background texture looks blotchy during a wipe. | The source animation or flattened background has keying/compression artifacts. | Adjust the background treatment in After Effects, or accept it if fast motion makes the artifact negligible. |
| Animation does not appear when shown. | The style path is wrong, the JSON did not load, or external assets are missing. | Check the style directory/custom file path, keep exported assets with the JSON, and restart OBS if needed. |

## Build

This project uses a CMake/buildspec-based OBS plugin layout and vendors ThorVG
for Lottie rendering.

Local build details depend on the target platform and OBS dependency setup. The
project metadata lives in `buildspec.json`.
