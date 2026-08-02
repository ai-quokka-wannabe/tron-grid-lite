#!/usr/bin/env python3
"""Records a looping flight through the Grid and encodes it as a GIF.

Runs the renderer in its recording mode, which flies a closed camera path and writes one image per
frame, then hands the sequence to ffmpeg. The clip loops seamlessly because the camera path does:
every term of it is periodic, and the last frame stops one step short of the first.

The GIF is generated in two passes. A single global palette of 256 colours is computed from the
whole clip first, and only then are the frames quantised against it — a per-frame palette makes the
background shimmer, which on a mostly black image is the one artefact you cannot miss.

Rendering happens at a higher resolution than the output and is scaled down at encode time. That
downscale is the only antialiasing this renderer has: it traces one ray per pixel by design, so
supersampling in the recording is how the neon edges come out clean.

The frame rate is deliberately low. Twelve frames a second is animation-on-twos, and the faint
stutter it leaves suits the subject: this is a slow drift through an empty world, not action
footage. It is also the one setting that makes the clip longer and smaller at the same time, since
the duration is the frame count divided by the rate.

Usage:
    python tools/record_flyby.py --preset windows-msvc --config Release

Requires ffmpeg on PATH.
"""

from __future__ import annotations

import argparse
import contextlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent

# Every renderer the build system can produce, as literals. The build layout is fixed by
# CMakePresets.json, so this is an enumeration rather than a guess — and enumerating it is what lets
# --preset and --config be names checked against a constant set instead of a path taken on trust.
BUILD_PRESETS = (
    "windows-msvc",
    "windows-clang-cl",
    "windows-mingw",
    "linux-x11-gcc",
    "linux-x11-clang",
    "linux-x11-clang-asan",
    "linux-x11-clang-tsan",
)

BUILD_CONFIGS = ("Release", "Debug")


def candidate_paths(preset: str | None, config: str | None) -> list[Path]:
    """Returns the build outputs to try, most preferred first.

    Release before Debug, deliberately. The two produce byte-identical recordings — verified — and
    Debug runs several times slower with the validation layers instrumenting every dispatch, so
    preferring it merely wastes minutes. The previous ordering preferred Debug.
    """
    presets = (preset,) if preset else BUILD_PRESETS
    configs = (config,) if config else BUILD_CONFIGS

    paths: list[Path] = []
    for chosen_config in configs:
        for chosen_preset in presets:
            binary = "TronGridLite.exe" if chosen_preset.startswith("windows") else "TronGridLite"
            paths.append(REPOSITORY_ROOT / "build" / chosen_preset / "src" / chosen_config / binary)
    return paths


def find_executable(preset: str | None, config: str | None) -> Path:
    """Returns the renderer to run, or exits with an explanation.

    There is deliberately no way to name an arbitrary path. The renderer this script runs is always
    something this repository built, the build layout is fixed, so a preset and a configuration name
    say everything a path could — and neither of them is a path, which means nothing taken from the
    command line is ever resolved on the filesystem or handed to a subprocess. That was previously an
    --executable flag; it bought nothing that this does not, and it made the script capable of
    running any binary on the machine.
    """
    candidates = candidate_paths(preset, config)

    for candidate in candidates:
        if candidate.is_file():
            return candidate

    sys.exit(
        "Could not find a built renderer. Build one first.\n"
        "Looked in:\n  " + "\n  ".join(str(path) for path in candidates)
    )


def require_ffmpeg() -> str:
    """Returns the ffmpeg command, or exits with installation advice."""
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        sys.exit(
            "ffmpeg is not on PATH, and it does the encoding.\n"
            "  Windows: winget install Gyan.FFmpeg\n"
            "  Debian or Ubuntu: sudo apt install ffmpeg"
        )
    return ffmpeg


def render_frames(executable: Path, directory: Path, width: int, height: int, frames: int) -> None:
    """Runs the renderer in recording mode."""
    command = [
        str(executable),
        "--record",
        "--width", str(width),
        "--height", str(height),
        "--frames", str(frames),
        "--output", str(directory),
    ]
    print(f"Rendering {frames} frames at {width}x{height} ...")

    # No working directory is forced. The renderer resolves its shaders against its own executable
    # path rather than against the working directory, so it runs correctly from anywhere; this used
    # to pass cwd=executable.parent and the comment justifying it outlived the need by some months.
    # The output directory is absolute either way, so nothing here depends on where we stand.
    result = subprocess.run(command)
    if result.returncode != 0:
        sys.exit(f"The renderer exited with code {result.returncode}")

    produced = sorted(directory.glob("frame_*.ppm"))
    if len(produced) != frames:
        sys.exit(f"Expected {frames} frames but found {len(produced)} in {directory}")


def encode_gif(ffmpeg: str, directory: Path, output: Path, fps: int, output_width: int, max_colors: int) -> None:
    """Encodes the frame sequence into a looping GIF using a single global palette.

    Three settings decide the file size, in descending order of effect: the number of frames, the
    output width, and the palette size. Dithering matters too, but in the opposite direction to
    intuition — it *adds* high-frequency noise that the GIF's run-length compression cannot pack, so
    a heavier dither costs size rather than saving it. An ordered Bayer pattern at a coarse scale is
    the compromise: it keeps the glows from banding without shimmering between frames the way error
    diffusion does.

    How much the camera moves matters as well, though nothing here controls it. GIF stores each
    frame as only the pixels that changed since the last one, so a calm path over a mostly black
    world leaves most of the image untouched from frame to frame and costs very little to store.
    A frantic one repaints everything, every frame.
    """
    # ffmpeg expands % sequences across the whole input path rather than just the filename, so a
    # checkout or temporary directory containing one would break the encode. The directory is
    # passed as the working directory instead of being baked into the pattern.
    pattern = "frame_%05d.ppm"
    palette = directory / "palette.png"

    scale = f"scale={output_width}:-1:flags=lanczos"

    print("Computing a global palette ...")
    subprocess.run(
        [ffmpeg, "-y", "-loglevel", "error", "-framerate", str(fps), "-i", pattern,
         "-vf", f"{scale},palettegen=max_colors={max_colors}:stats_mode=full", str(palette)],
        cwd=directory,
        check=True,
    )

    print(f"Encoding {output} ...")
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [ffmpeg, "-y", "-loglevel", "error", "-framerate", str(fps), "-i", pattern, "-i", str(palette),
         "-lavfi", f"{scale}[frames];[frames][1:v]paletteuse=dither=bayer:bayer_scale=4",
         "-loop", "0", str(output)],
        cwd=directory,
        check=True,
    )


def encode_mp4(ffmpeg: str, directory: Path, output: Path, fps: int, output_width: int) -> None:
    """Encodes the same frames as an MP4, which is far smaller and much better looking."""
    # Same reason as encode_gif: the directory goes in cwd rather than into the pattern.
    pattern = "frame_%05d.ppm"
    print(f"Encoding {output} ...")
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [ffmpeg, "-y", "-loglevel", "error", "-framerate", str(fps), "-i", pattern,
         "-vf", f"scale={output_width}:-2:flags=lanczos", "-c:v", "libx264", "-pix_fmt", "yuv420p",
         "-crf", "20", "-movflags", "+faststart", str(output)],
        cwd=directory,
        check=True,
    )


def bounded_int(minimum: int, maximum: int):
    """Returns an argparse type accepting a whole number inside an inclusive range.

    Every numeric option here ends up on the renderer's command line or ffmpeg's, and until this
    existed none of them was checked. `--frames -5 --render-width 0` was accepted, announced as
    "Rendering -5 frames at 0x720", and then failed inside the renderer as a bare exit code — the
    complaint arriving from the wrong process, about a value this script had in its hand all along.

    Bounds are generous rather than tuned. They exist to catch a typo or a negative, not to express
    an opinion about how large a recording may be.
    """

    def parse(text: str) -> int:
        value = int(text)
        if (value < minimum) or (value > maximum):
            raise argparse.ArgumentTypeError(f"must be between {minimum} and {maximum}, not {value}")
        return value

    return parse


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preset", choices=BUILD_PRESETS, help="Build preset to take the renderer from. All are tried if omitted.")
    parser.add_argument("--config", choices=BUILD_CONFIGS, help="Build configuration. Release then Debug if omitted.")
    parser.add_argument("--frames", type=bounded_int(1, 100_000), default=84, help="Frames in the loop (default: 84).")
    parser.add_argument("--fps", type=bounded_int(1, 240), default=12, help="Playback rate (default: 12).")
    parser.add_argument("--render-width", type=bounded_int(16, 16_384), default=1280, help="Render width (default: 1280).")
    parser.add_argument("--render-height", type=bounded_int(16, 16_384), default=720, help="Render height (default: 720).")
    parser.add_argument("--output-width", type=bounded_int(16, 16_384), default=480, help="Width of the encoded clip (default: 480).")
    parser.add_argument("--max-colors", type=bounded_int(2, 256), default=128, help="Palette size, 2 to 256 (default: 128).")
    parser.add_argument("--gif", default="images/flyby.gif", help="GIF path, relative to the repository root.")
    parser.add_argument("--mp4", default=None, help="Also write an MP4 here, relative to the repository root.")
    parser.add_argument("--keep-frames", action="store_true", help="Do not delete the rendered frames afterwards.")
    arguments = parser.parse_args()

    executable = find_executable(arguments.preset, arguments.config)
    ffmpeg = require_ffmpeg()

    gif_path = REPOSITORY_ROOT / arguments.gif

    # --keep-frames wants a directory that outlives this block, so the two cases differ in whether
    # anything is cleaned up rather than in what is created.
    frames_context = (
        contextlib.nullcontext(REPOSITORY_ROOT / "build" / "flyby-frames")
        if arguments.keep_frames
        else tempfile.TemporaryDirectory(prefix="tron-grid-lite-frames-")
    )

    with frames_context as temporary:
        directory = Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)

        # --keep-frames reuses a fixed directory, so a shorter second run would still find the
        # previous run's surplus frames and the count check would blame the renderer for them.
        for stale in directory.glob("frame_*.ppm"):
            stale.unlink()

        render_frames(executable, directory, arguments.render_width, arguments.render_height, arguments.frames)
        encode_gif(ffmpeg, directory, gif_path, arguments.fps, arguments.output_width, arguments.max_colors)

        if arguments.mp4:
            encode_mp4(ffmpeg, directory, REPOSITORY_ROOT / arguments.mp4, arguments.fps, arguments.output_width)

        if arguments.keep_frames:
            print(f"Frames kept in {directory}")

    size_mb = gif_path.stat().st_size / (1024 * 1024)
    print(f"Done: {gif_path} ({size_mb:.1f} MiB)")
    if size_mb > 6.0:
        print("That is large for a README. Fewer --frames helps most, then --output-width, then --max-colors.")


if __name__ == "__main__":
    main()
