"""Generate the test images in this folder.

Run from the repo root:   python samples/generate_samples.py
Requires:                 pip install pillow numpy

The images are synthetic (drawn by this script), so they are reproducible
and free of any licence. A fixed random seed gives byte-identical pixels on
every run.
"""

from pathlib import Path

import numpy as np
from PIL import Image, PngImagePlugin

OUT = Path(__file__).resolve().parent
SIZE = 256


def scene(seed: int, sun_x: int, sky_top, sky_bottom, hill_color) -> np.ndarray:
    """Draw a simple landscape with mild noise so it behaves like a photo."""
    rng = np.random.default_rng(seed)
    h = w = SIZE
    y, x = np.mgrid[0:h, 0:w]

    # Sky: vertical gradient.
    t = (y / (h - 1))[..., None]
    img = (1 - t) * np.array(sky_top) + t * np.array(sky_bottom)

    # Sun.
    sun = (x - sun_x) ** 2 + (y - 60) ** 2 < 22 ** 2
    img[sun] = (250, 220, 90)

    # Two hills.
    hill1 = y > 170 + 25 * np.sin(x / 40)
    hill2 = y > 195 + 15 * np.cos(x / 25 + 1)
    img[hill1] = hill_color
    img[hill2] = np.array(hill_color) * 0.75

    # House: walls, roof, window.
    img[150:190, 150:200] = (200, 170, 140)
    for i in range(20):
        img[130 + i, 150 + 20 - i: 200 - 20 + i + 1] = (150, 50, 40)
    img[160:175, 165:180] = (90, 140, 200)

    # Sensor-like noise, so the image is not perfectly flat.
    img += rng.normal(0, 4, img.shape)
    return np.clip(img, 0, 255).round().astype(np.uint8)


def save(arr: np.ndarray, name: str, **kwargs) -> None:
    Image.fromarray(arr, "RGB").save(OUT / name, **kwargs)


def main() -> None:
    original = scene(1, 70, (70, 130, 210), (200, 225, 245), (70, 140, 60))
    save(original, "00_original.png")

    # 01: same pixels, different file bytes (extra metadata chunk).
    meta = PngImagePlugin.PngInfo()
    meta.add_text("Comment", "Same pixels as 00_original, different metadata")
    save(original, "01_identical_pixels.png", pnginfo=meta)

    # 02: a single channel of a single pixel changed by 1.
    a = original.copy()
    a[100, 100, 0] = (int(a[100, 100, 0]) + 1) % 256
    save(a, "02_one_pixel.png")

    # 03: small local edit (16x16 patch over the window painted as wall),
    #     similar to an AI inpainting edit of one object.
    a = original.copy()
    a[160:176, 164:180] = original[150:166, 184:200]
    save(a, "03_small_region_edit.png")

    # 04: global brightness +5.
    save(np.clip(original.astype(int) + 5, 0, 255).astype(np.uint8),
         "04_brightness_plus5.png")

    # 05: resized to half size.
    Image.fromarray(original).resize((SIZE // 2, SIZE // 2), Image.LANCZOS) \
        .save(OUT / "05_resized_half.png")

    # 06: JPEG recompression (lossy: every pixel shifts slightly).
    save(original, "06_recompressed_q85.jpg", quality=85)

    # 07: 5% cropped from each side.
    m = int(SIZE * 0.05)
    save(original[m:-m, m:-m].copy(), "07_cropped_5pct.png")

    # 08: horizontal mirror.
    save(original[:, ::-1].copy(), "08_mirrored.png")

    # 09: unrelated image (different scene, colours and layout).
    save(scene(2, 190, (30, 30, 70), (230, 120, 70), (120, 90, 50)),
         "09_unrelated.png")

    print(f"Wrote samples to {OUT}")


if __name__ == "__main__":
    main()
