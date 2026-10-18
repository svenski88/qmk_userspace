"""Compile real Link display/model/adapter sources with host QMK stubs; optionally preview."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
USERSPACE = HERE.parents[1]
KEYMAP = USERSPACE / "keyboards/cannonkeys/link/keymaps/svenge"
FIRMWARE = Path(os.environ.get("QMK_HOME", USERSPACE.parent / "qmk_firmware"))
GCC = os.environ.get("CC") or shutil.which("gcc") or "C:/UTIL/QMK_MSYS/mingw64/bin/gcc.exe"
ENV = os.environ.copy()
ENV["PATH"] = str(Path(GCC).resolve().parent) + os.pathsep + ENV["PATH"]


def preview(directory):
    from PIL import Image, ImageDraw

    def panel(raw):
        image = Image.new("RGB", (64, 128), "black")
        for y in range(128):
            for x in range(64):
                if raw[(y // 8) * 64 + x] & (1 << (y % 8)):
                    image.putpixel((x, y), (240, 250, 255))
        return image.resize((256, 512), Image.Resampling.NEAREST)

    def pair(raw, title):
        image = Image.new("RGB", (584, 564), (20, 22, 27))
        draw = ImageDraw.Draw(image)
        draw.text((24, 12), title, fill="white")
        image.paste(panel(raw[:1024]), (24, 38))
        image.paste(panel(raw[1024:2048]), (304, 38))
        return image

    output = USERSPACE / "previews/link"
    output.mkdir(parents=True, exist_ok=True)
    pair((directory / "dashboard.raw").read_bytes(), "LEFT: CTRL / CAPS XOR / ALT                 RIGHT: ACTIVE LAYER").save(output / "dashboard.png")
    pair((directory / "dashboard-live.raw").read_bytes(), "FIRMWARE RENDERER: SAMPLE PRESSES + LAST RESOLVED KEYS").save(output / "dashboard-live.png")
    data = (directory / "rain.raw").read_bytes()
    frames = [pair(data[i:i+2048], "MATRIX RAIN - BOTH PHYSICAL HALVES") for i in range(0, len(data), 2048)]
    frames[0].save(output / "rain.gif", save_all=True, append_images=frames[1:], duration=50, loop=0)
    print(f"Previews: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview", action="store_true", help="Generate PNG/GIF previews using Pillow")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="link-oled-tests-") as directory:
        tmp = Path(directory)
        for name in ("quantum.h", "transactions.h"):
            (tmp / name).write_text('#include "fake_qmk.h"\n', encoding="utf-8")
        executable = tmp / ("test.exe" if os.name == "nt" else "test")
        subprocess.run([
            GCC, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
            '-DQMK_KEYBOARD_H="quantum.h"', "-I", str(tmp), "-I", str(HERE), "-I", str(KEYMAP),
            "-I", str(FIRMWARE / "quantum"),
            str(KEYMAP / "display.c"), str(KEYMAP / "oled.c"), str(HERE / "test_screens.c"), "-o", str(executable),
        ], env=ENV, check=True)
        subprocess.run([str(executable), *([str(tmp)] if args.preview else [])], env=ENV, check=True)
        if args.preview:
            preview(tmp)


if __name__ == "__main__":
    main()
