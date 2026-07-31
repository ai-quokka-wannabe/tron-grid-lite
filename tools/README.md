# Tools

Scripts that operate on the renderer from the outside. Nothing here is part of the build, and
nothing the renderer does at runtime depends on any of it.

## Setting up

The tools are Python, and they expect a virtual environment beside them so that nothing is
installed into the system interpreter:

```bash
python -m venv tools/.venv

# Windows
tools/.venv/Scripts/activate

# Linux
source tools/.venv/bin/activate

pip install -r tools/requirements.txt
```

That last step currently installs nothing, because the scripts use only the standard library. The
file exists so the answer is written down where people look for it, and so that the next tool has
somewhere to declare itself.

What the tools do need is **ffmpeg**, which is a program rather than a package:

```bash
winget install Gyan.FFmpeg   # Windows
sudo apt install ffmpeg      # Debian or Ubuntu
```

## `record_flyby.py`

Records a looping flight through the Grid and encodes it as a GIF — the animation at the top of the
repository's README is its output.

```bash
python tools/record_flyby.py
```

It finds a built renderer on its own, or takes `--executable`. The renderer is run in its recording
mode, which flies a closed camera path and writes one image per frame; ffmpeg then encodes them.

Three things about it are deliberate:

- **The loop is seamless.** Every term of the camera path is periodic and every oscillation
  completes a whole number of cycles, so the pose at the end of the loop is the pose at its start.
  The last frame stops one step short of the first rather than repeating it.
- **A single global palette.** GIF allows 256 colours; computing them per frame makes a mostly
  black image shimmer, which is the one artefact nobody fails to notice. The palette is computed
  across the whole clip first, then the frames are quantised against it.
- **Rendering happens larger than the output.** The renderer traces one ray per pixel by design and
  has no antialiasing of its own, so the downscale at encode time is where the neon edges get
  cleaned up.

Useful options:

| Option | Default | Purpose |
|--------|---------|---------|
| `--frames` | 180 | Length of the loop |
| `--fps` | 30 | Playback rate |
| `--render-width`, `--render-height` | 1280x720 | What the renderer traces |
| `--output-width` | 640 | Width of the encoded clip |
| `--mp4 PATH` | off | Also write an MP4, which is far smaller and looks better |
| `--keep-frames` | off | Leave the rendered frames in `build/flyby-frames` |

If the resulting GIF is too large for a README, reduce `--output-width` or `--frames` before
reaching for anything else; both cost far less quality than lowering the frame rate does.
