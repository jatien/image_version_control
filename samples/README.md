# Sample images

Test images for the hashes. `00_original.png` is the reference; every other
file is a known edit of it (or an unrelated image), so each hash can be
checked against an expected result.

All images are synthetic (a 256×256 landscape with mild noise) and generated
by `generate_samples.py`, so they are reproducible and licence-free:

```bash
pip install pillow numpy
python samples/generate_samples.py
```

PNG is used because it is lossless: the pixels you save are exactly the
pixels you load back. The one JPEG is there on purpose, to test lossy
recompression.

## Expected results

| File | Edit | Pixels changed | Exact hash | Similarity hash |
|---|---|---|---|---|
| `00_original.png` | reference | — | — | — |
| `01_identical_pixels.png` | extra metadata only | 0 | **same** | same |
| `02_one_pixel.png` | one channel of one pixel +1 | 1 | different | same |
| `03_small_region_edit.png` | 16×16 window painted over (like AI inpainting) | 256 | different | same or very close |
| `04_brightness_plus5.png` | whole image +5 brightness | all | different | same or very close |
| `05_resized_half.png` | 128×128 | all (new size) | different | close |
| `06_recompressed_q85.jpg` | JPEG, quality 85 | almost all | different | close |
| `07_cropped_5pct.png` | 5% cut from each side | all (new size) | different | close-ish (hard case) |
| `08_mirrored.png` | horizontal flip | almost all | different | far (most hashes are not mirror-invariant) |
| `09_unrelated.png` | different scene | almost all | different | far |

`01` is the key check for the exact hash: the **file** bytes differ
(different SHA-256 of the file), but the **pixels** are identical, so the
pixel hash must match.

`03` is the key check for tile hashes: only the tiles covering the window
(around x 164–180, y 160–176) should change.
