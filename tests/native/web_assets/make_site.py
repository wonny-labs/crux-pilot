#!/usr/bin/env python3
"""Make the built folder that the packaged assets image serves, as a frontend tool would.

    make_site.py <folder>

Files: an index, a script and a style, which compress; an image of noise, which does not; a font of
noise just under the largest asset that the image's response deadline allows, which fills the
server's pieces and its socket; and a nested file.
"""
import pathlib
import shutil
import sys

LARGEST_FITS = 49152


def noise(length, state):
    data = bytearray()
    for _ in range(length):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        data.append(state >> 16 & 0xFF)
    return bytes(data)


def main(folder):
    root = pathlib.Path(folder)
    if root.exists():
        shutil.rmtree(root)
    (root / "assets").mkdir(parents=True)
    (root / "index.html").write_text(
        '<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Packaged</title>'
        '<link rel="stylesheet" href="/style.css"></head><body><div id="app"></div>'
        '<script src="/app-Ab12Cd34.js"></script></body></html>\n', encoding="utf-8")
    (root / "app-Ab12Cd34.js").write_text(
        "".join(f"export const step{n} = () => 'step number {n} of the packaged interface';\n"
                for n in range(120)), encoding="utf-8")
    (root / "style.css").write_text("body { margin: 0; font-family: sans-serif; }\n" * 20, encoding="utf-8")
    (root / "logo.png").write_bytes(b"\x89PNG\r\n\x1a\n" + noise(600, 5))
    (root / "assets" / "font-Zx98Wv76.woff2").write_bytes(noise(LARGEST_FITS - 3000, 11))
    (root / "assets" / "icon.svg").write_text("<svg xmlns='http://www.w3.org/2000/svg'><circle r='4'/></svg>\n",
                                             encoding="utf-8")


if __name__ == "__main__":
    main(sys.argv[1])
