#!/usr/bin/env python3
"""Generate vector and raster icon assets for move_windows_to_mouse."""

from io import BytesIO
from io import BytesIO
from pathlib import Path
import sys

ASSETS = Path("Assets")
SVG_PATH = ASSETS / "MoveWindowsToMouse.svg"
ICO_PATH = ASSETS / "MoveWindowsToMouse.ico"
PNG_SIZES = {
    "Square44x44Logo.png": (44, 44),
    "Square150x150Logo.png": (150, 150),
}


def svg_square() -> str:
    return """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 150 150" width="150" height="150">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="150" y2="150">
      <stop offset="0%" stop-color="#0e2f46" />
      <stop offset="100%" stop-color="#2c5c8d" />
    </linearGradient>
    <linearGradient id="monitorBlue" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="#1a70bf" />
      <stop offset="100%" stop-color="#2d8cd8" />
    </linearGradient>
  </defs>
  <!-- Background rounded square -->
  <rect x="0" y="0" width="150" height="150" rx="30" fill="url(#bg)" />
  
  <!-- Old monitor (bottom-left) -->
  <rect x="28" y="110" width="18" height="4" rx="2" fill="#ffffff" />
  <rect x="32" y="116" width="10" height="10" rx="3" fill="#22537a" />
  <rect x="28" y="126" width="18" height="4" rx="2" fill="#7a8fa9" />
  <rect x="16" y="86" width="46" height="30" rx="8" fill="url(#monitorBlue)" stroke="#ffffff" stroke-width="3" />
  <rect x="22" y="92" width="34" height="18" rx="4" fill="#bfe8ff" />

  <!-- Destination monitor (top-right) -->
  <rect x="96" y="76" width="10" height="12" rx="3" fill="#22537a" />
  <rect x="89" y="88" width="24" height="4" rx="2" fill="#7a8fa9" />
  <rect x="64" y="18" width="76" height="58" rx="10" fill="#48a0e5" stroke="#ffffff" stroke-width="3" />
  <rect x="70" y="24" width="64" height="44" rx="8" fill="#dff6ff" />
  
  <!-- Motion lines from source to destination -->
  <path d="M36 92 L86 44" stroke="#ffffff" stroke-width="2" stroke-linecap="round" fill="none" />
  <path d="M42 98 L92 50" stroke="#ffffff" stroke-width="2" stroke-linecap="round" fill="none" />
  <path d="M48 104 L98 56" stroke="#ffffff" stroke-width="2" stroke-linecap="round" fill="none" />
  
  <!-- Windows on destination monitor -->
  <rect x="76" y="30" width="24" height="18" rx="4" fill="#ffffff" stroke="#7a8fa9" stroke-width="2" />
  <rect x="106" y="30" width="24" height="18" rx="4" fill="#ffffff" stroke="#7a8fa9" stroke-width="2" />
  <rect x="76" y="52" width="54" height="14" rx="4" fill="#ffffff" stroke="#7a8fa9" stroke-width="2" />
  <rect x="80" y="34" width="18" height="3" rx="1" fill="#e6f2ff" />
  <rect x="110" y="34" width="18" height="3" rx="1" fill="#e6f2ff" />
  
  <!-- Happy smile -->
  <path d="M86 58 Q100 63 114 55" stroke="#7a8fa9" stroke-width="3" fill="none" stroke-linecap="round" />
</svg>
"""


def write_svg(path: Path, svg: str) -> None:
    path.write_text(svg, encoding="utf-8")


def import_cairosvg() -> object | None:
    try:
        import cairosvg
    except ImportError:
        return None
    except OSError as err:
        print(f"Warning: Cairo runtime load failed: {err}", file=sys.stderr)
        return None
    return cairosvg


def svg_to_png(svg: str, size: tuple[int, int]) -> bytes | None:
    cairosvg = import_cairosvg()
    if cairosvg is None:
        return None
    width, height = size
    try:
        return cairosvg.svg2png(bytestring=svg.encode("utf-8"), output_width=width, output_height=height)
    except Exception as err:
        print(f"Warning: CairoSVG rendering failed: {err}", file=sys.stderr)
        return None


def render_image(size: tuple[int, int]) -> "Image.Image | None":
    try:
        from PIL import Image
    except ImportError:
        return None

    svg = svg_square()
    png_bytes = svg_to_png(svg, size)
    if png_bytes is None:
        return None

    image = Image.open(BytesIO(png_bytes))
    return image.convert("RGBA")


def render_png(size: tuple[int, int], output_path: Path) -> bool:
    image = render_image(size)
    if image is None:
        return False
    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path, format="PNG")
    return True


def render_ico(output_path: Path) -> bool:
    image = render_image((256, 256))
    if image is None:
        return False
    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path, format="ICO", sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (16, 16)])
    return True


def main() -> None:
    ASSETS.mkdir(exist_ok=True, parents=True)
    write_svg(SVG_PATH, svg_square())
    print(f"Created {SVG_PATH}")

    generated_any = False
    for file_name, size in PNG_SIZES.items():
        output_path = ASSETS / file_name
        if render_png(size, output_path):
            print(f"Created {output_path}")
            generated_any = True
        else:
            print(f"Skipped {output_path}: install Pillow to build PNG assets")

    if render_ico(ICO_PATH):
        print(f"Created {ICO_PATH}")
    else:
        print(f"Skipped {ICO_PATH}: install Pillow and CairoSVG with a working Cairo runtime to build ICO assets")

    if not generated_any:
        print("SVG source generated; install Pillow and CairoSVG with a working Cairo runtime to build PNG assets.")


if __name__ == "__main__":
    main()
