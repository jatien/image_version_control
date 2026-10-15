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

Results of `ivc_compare samples/00_original.png <file>`. pHash is the
Hamming distance after turning the image back to the best of the 8
orientations; tiles is how many of the 8×8 tiles visibly changed.

| File | Edit | Pixels changed | pHash | Tiles changed | Verdict |
|---|---|---|---|---|---|
| `00_original.png` | reference | — | — | — | — |
| `01_identical_pixels.png` | extra metadata only | 0 | 0 | 0 | **IDENTICAL** |
| `02_one_pixel.png` | one channel of one pixel +1 | 1 | 0 | 0 (one tile `~`) | SIMILAR |
| `03_small_region_edit.png` | 16×16 window painted over (like AI inpainting) | 256 | 2 | **1**, at (5,5) | SIMILAR |
| `04_brightness_plus5.png` | whole image +5 brightness | all | 0 | 0 | SIMILAR |
| `05_resized_half.png` | 128×128 | all (new size) | 0 | 0 | SIMILAR |
| `06_recompressed_q85.jpg` | JPEG, quality 85 | almost all | 0 | 0 | SIMILAR |
| `07_cropped_5pct.png` | 5% cut from each side | all (new size) | 8 | 40 | SIMILAR |
| `08_mirrored.png` | horizontal flip | almost all | 0 | 0 | SIMILAR (mirrored left-right) |
| `09_unrelated.png` | different scene | almost all | 16 | 64 | DIFFERENT |
| `10_rotated_90.png` | 90° counter-clockwise | all (size swapped) | 0 | 0 | SIMILAR (rotated 90) |
| `11_rotated_5deg.png` | 5°, black corners | almost all | 12 | 57 | DIFFERENT — **known limitation** |

`01` is the key check for the exact hash: the **file** bytes differ
(different SHA-256 of the file), but the **pixels** are identical, so the
pixel hash must match.

`02` and `03` are the key checks for tile hashes: the exact tile hashes
locate even a single changed pixel, and the window edit is flagged in
exactly one tile.

`07` shows the limit of tiles: a crop shifts every tile's content, so the
grid no longer lines up. `11` shows the limit of the orientation search:
only the 8 exact rotations and flips are tried, not arbitrary angles.
