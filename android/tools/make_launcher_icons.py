#!/usr/bin/env python3
"""Render the Android launcher and task-switcher icons from the Windows icon.

A Windows build takes its icon from `code/resources/app-icon/opents.ico` through
`Sun.rc`. Android needs packaged bitmaps at every density plus an adaptive icon,
so they are rendered from the same `opents.ico` that the desktop uses: the
largest frame it carries is the artwork every density is resampled from.

Run from the repository root with Pillow available:

  python android/tools/make_launcher_icons.py

The generated files are checked in, because Gradle cannot build an APK without
them and the release build does not run Python.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
RES = ROOT / "android/app/src/main/res"
SOURCE = ROOT / "code/resources/app-icon/opents.ico"

# Android's launcher icon densities, as a multiple of one dp.
DENSITIES = {
    "mdpi": 1.0,
    "hdpi": 1.5,
    "xhdpi": 2.0,
    "xxhdpi": 3.0,
    "xxxhdpi": 4.0,
}

# A launcher icon is 48 dp. An adaptive icon's foreground is 108 dp with the
# artwork kept inside the 72 dp safe zone, so any mask crop keeps it visible.
LEGACY_DP = 48
ADAPTIVE_DP = 108
SAFE_ZONE_DP = 72


def load_source() -> Image.Image:
    """The largest frame the Windows icon carries, trimmed to its artwork.

    The frame leaves transparent space around the emblems, and an adaptive icon is
    centred by its canvas, so the padding is cut away first or the mask crop would
    sit off centre.
    """
    icon = Image.open(SOURCE)
    sizes = icon.info.get("sizes") or {icon.size}
    width, height = max(sizes, key=lambda size: size[0] * size[1])
    icon.size = (width, height)
    image = icon.convert("RGBA")

    bounds = image.getchannel("A").point(lambda value: 255 if value > 8 else 0).getbbox()
    if bounds is not None:
        artwork = image.crop(bounds)
        side = max(artwork.size)
        square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        square.paste(
            artwork,
            ((side - artwork.width) // 2, (side - artwork.height) // 2),
            artwork,
        )
        image = square

    return image


def dominant_colour(image: Image.Image) -> tuple[int, int, int]:
    """The opaque pixels' median colour, used as the adaptive icon's background."""
    raw = image.tobytes()
    opaque = [
        (raw[i], raw[i + 1], raw[i + 2])
        for i in range(0, len(raw), 4)
        if raw[i + 3] > 200
    ]
    if not opaque:
        return (0, 0, 0)
    channels = list(zip(*opaque))
    return tuple(sorted(channel)[len(channel) // 2] for channel in channels)  # type: ignore


def render(source: Image.Image, size: int) -> Image.Image:
    return source.resize((size, size), Image.LANCZOS)


def adaptive_foreground(source: Image.Image, factor: float) -> Image.Image:
    canvas_px = round(ADAPTIVE_DP * factor)
    artwork = render(source, round(SAFE_ZONE_DP * factor))
    canvas = Image.new("RGBA", (canvas_px, canvas_px), (0, 0, 0, 0))
    offset = (canvas_px - artwork.width) // 2
    canvas.paste(artwork, (offset, offset), artwork)
    return canvas


def monochrome(source: Image.Image, factor: float) -> Image.Image:
    """The themed-icon layer: the artwork's own alpha, painted one flat colour."""
    artwork = render(source, round(SAFE_ZONE_DP * factor))
    canvas_px = round(ADAPTIVE_DP * factor)
    flat = Image.new("RGBA", artwork.size, (255, 255, 255, 0))
    flat.putalpha(artwork.getchannel("A"))
    canvas = Image.new("RGBA", (canvas_px, canvas_px), (0, 0, 0, 0))
    offset = (canvas_px - artwork.width) // 2
    canvas.paste(flat, (offset, offset), flat)
    return canvas


def main() -> None:
    source = load_source()
    red, green, blue = dominant_colour(source)
    background = f"#{red:02X}{green:02X}{blue:02X}"

    for density, factor in DENSITIES.items():
        folder = RES / f"mipmap-{density}"
        folder.mkdir(parents=True, exist_ok=True)

        legacy = render(source, round(LEGACY_DP * factor))
        legacy.save(folder / "ic_launcher.png")
        # The round icon is the same artwork: the launcher applies its own mask.
        legacy.save(folder / "ic_launcher_round.png")
        adaptive_foreground(source, factor).save(folder / "ic_launcher_foreground.png")
        monochrome(source, factor).save(folder / "ic_launcher_monochrome.png")

    anydpi = RES / "mipmap-anydpi-v26"
    anydpi.mkdir(parents=True, exist_ok=True)
    for name in ("ic_launcher", "ic_launcher_round"):
        (anydpi / f"{name}.xml").write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
            '    <background android:drawable="@color/ic_launcher_background" />\n'
            '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
            '    <monochrome android:drawable="@mipmap/ic_launcher_monochrome" />\n'
            "</adaptive-icon>\n",
            encoding="utf-8",
        )

    (RES / "values/ic_launcher_backgrounds.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<resources>\n"
        f'    <color name="ic_launcher_background">{background}</color>\n'
        "</resources>\n",
        encoding="utf-8",
    )

    print(f"Rendered the launcher icons over {background} from {SOURCE.name}")


if __name__ == "__main__":
    main()
