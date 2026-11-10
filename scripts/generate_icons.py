"""Render the original SVG to multi-resolution Windows icons (developer tool only).

uv pip install --target build/icon-tools -r scripts/requirements-icons.txt
python scripts/generate_icons.py
Normal builds use the committed .ico and need neither Python nor resvg.
"""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'build' / 'icon-tools'))
import resvg_py

SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


def main():
    source = ROOT / 'assets' / 'tonelab.svg'
    frames = [resvg_py.svg_to_bytes(svg_path=str(source), width=size, height=size,
                                  skip_system_fonts=True) for size in SIZES]
    offset = 6 + 16 * len(frames)
    directory = bytearray(struct.pack('<HHH', 0, 1, len(frames)))
    for size, png in zip(SIZES, frames):
        directory.extend(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0,
                                     1, 32, len(png), offset))
        offset += len(png)
    (ROOT / 'assets' / 'tonelab.ico').write_bytes(directory + b''.join(frames))
    (ROOT / 'assets' / 'tonelab.png').write_bytes(
        resvg_py.svg_to_bytes(svg_path=str(source), width=512, height=512,
                              skip_system_fonts=True))
    print('Generated tonelab.ico (9 sizes: 16..256 px) and tonelab.png (512 px).')


if __name__ == '__main__':
    main()
