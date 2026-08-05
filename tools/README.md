# Tools

Scripts that operate on the renderer from the outside. Nothing here is part of the build, and
nothing the renderer does at runtime depends on any of it.

| Script | What it does | Needs the venv |
|--------|--------------|----------------|
| [`record_flyby.py`](record_flyby.py) | Renders the fixed camera path and encodes the README's clip | Yes |
| [`check_abi_version.py`](check_abi_version.py) | Refuses a change to the Program ABI header that forgot to bump `TGL_ABI_VERSION` | No — standard library only |

## Keeping the Program ABI version honest

`TGL_ABI_VERSION` is what stops the Grid loading a Program built against a different memory layout,
and by itself it enforces nothing: a forgotten bump leaves both sides agreeing on the number while
disagreeing about the bytes, which is the silent corruption the number is kept for and which fails
nowhere at run time.

So the header is fingerprinted beside itself in `libs/program-abi/abi_fingerprint.txt`, and CI checks
it on every push. The fingerprint ignores the version line, all comments and all whitespace, so only
the declarations count — rewording a doc comment or reflowing a struct is free, and a member added,
removed, renamed or retyped is not.

```bash
python tools/check_abi_version.py check    # what CI runs
python tools/check_abi_version.py update   # after bumping the version, re-record
```

`update` refuses to record a changed header at an unchanged version, which is the whole point. If a
change genuinely cannot affect any Program — the escape exists and is deliberately visible in a diff
— delete the fingerprint file and re-record.

## Setting up

The tools are Python, and they expect a virtual environment beside them so that nothing is
installed into the system interpreter:

```bash
python -m venv tools/.venv

# Windows
source tools/.venv/Scripts/activate

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

Four things about it are deliberate:

- **The loop is seamless.** Every term of the camera path is periodic and every oscillation
  completes a whole number of cycles, so the pose at the end of the loop is the pose at its start.
  The last frame stops one step short of the first rather than repeating it.
- **A single global palette.** GIF allows 256 colours; computing them per frame makes a mostly
  black image shimmer, which is the one artefact nobody fails to notice. The palette is computed
  across the whole clip first, then the frames are quantised against it.
- **Rendering happens larger than the output.** The renderer traces one ray per pixel by design and
  has no antialiasing of its own, so the downscale at encode time is where the neon edges get
  cleaned up.
- **The frame rate is low on purpose.** Twelve a second is animation-on-twos, and the slight stutter
  suits a slow drift through an empty world.

Useful options:

| Option | Default | Purpose |
|--------|---------|---------|
| `--frames` | 84 | Length of the loop |
| `--fps` | 12 | Playback rate |
| `--render-width`, `--render-height` | 1280x720 | What the renderer traces |
| `--output-width` | 480 | Width of the encoded clip |
| `--max-colors` | 128 | Palette size |
| `--mp4 PATH` | off | Also write an MP4, which is far smaller and looks better |
| `--keep-frames` | off | Leave the rendered frames in `build/flyby-frames` |

If the resulting GIF is too large for a README, `--frames` is the biggest lever, then
`--output-width`, then `--max-colors`. Lowering `--fps` shrinks it too and lengthens the clip while
doing so, which is why the default is already low — but there is not much room left below twelve
before the motion stops reading as motion.
