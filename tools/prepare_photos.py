"""Shrinks photos to fit the 480x320 remote screen and copies them to its SD card.

Usage: python prepare_photos.py [photo folder] [SD card folder]

With no arguments it reads ~/Pictures/Photo Frame and writes to the first SD card
it finds under /Volumes. Photos go in a "photos" folder on the card.

Handles JPEG, PNG and iPhone HEIC photos, turns sideways photos upright, and
saves them as small plain JPEGs the ESP32 can decode (about 30-60 KB each, so a
32 GB card holds far more than you'll ever need). Photos already on the card are
skipped, so it's safe to run again after adding more.
"""

import sys
from pathlib import Path

from PIL import Image, ImageOps

try:
    from pillow_heif import register_heif_opener

    register_heif_opener()
except ImportError:
    pass

SCREEN = (480, 320)
EXTENSIONS = {".jpg", ".jpeg", ".png", ".heic", ".heif", ".tif", ".tiff", ".webp"}
DEFAULT_SOURCE = Path.home() / "Pictures" / "Photo Frame"


def find_sd_card():
    """The first removable, writable volume that isn't the Mac's own disk."""
    for vol in sorted(Path("/Volumes").iterdir()):
        if vol.name.startswith(".") or vol.name == "Macintosh HD" or vol.is_symlink():
            continue
        if vol.is_dir() and (vol / ".").stat() and _writable(vol):
            return vol
    return None


def _writable(path):
    test = path / ".write_test"
    try:
        test.write_text("x")
        test.unlink()
        return True
    except OSError:
        return False


def prepare(src: Path, dest: Path) -> bool:
    with Image.open(src) as im:
        im = ImageOps.exif_transpose(im)   # iPhone photos are often stored sideways
        im = im.convert("RGB")
        im.thumbnail(SCREEN, Image.LANCZOS)  # fit inside 480x320, keep the shape
        # Plain (baseline) JPEG: the ESP32's decoder can't read progressive ones.
        im.save(dest, "JPEG", quality=88, optimize=True, progressive=False)
    return True


def main():
    source = Path(sys.argv[1]).expanduser() if len(sys.argv) > 1 else DEFAULT_SOURCE
    card = Path(sys.argv[2]).expanduser() if len(sys.argv) > 2 else find_sd_card()

    if not source.is_dir():
        source.mkdir(parents=True, exist_ok=True)
        print(f"Made the folder {source}.")
        print("Export your photos from the Photos app into it, then run this again.")
        return 1
    if card is None:
        print("No SD card found. Put the card in your Mac (or a USB card reader) and try again.")
        return 1

    out = card / "photos"
    out.mkdir(exist_ok=True)
    files = sorted(p for p in source.rglob("*") if p.suffix.lower() in EXTENSIONS
                   and not p.name.startswith("."))
    print(f"{len(files)} photos in {source}")
    print(f"Copying to {out}\n")

    done = skipped = failed = 0
    for i, src in enumerate(files, 1):
        # Name by folder + file so two IMG_0001.jpg from different albums don't clash.
        rel = src.relative_to(source).with_suffix("")
        name = "_".join(rel.parts).replace(" ", "_")[:60] + ".jpg"
        dest = out / name
        if dest.exists():
            skipped += 1
            continue
        try:
            prepare(src, dest)
            done += 1
            print(f"  [{i}/{len(files)}] {src.name}")
        except Exception as e:  # a damaged or unsupported file shouldn't stop the rest
            failed += 1
            print(f"  [{i}/{len(files)}] skipped {src.name}: {e}")

    print(f"\nDone: {done} added, {skipped} already on the card, {failed} could not be read.")
    print("You can eject the card and put it in the back of the screen.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
