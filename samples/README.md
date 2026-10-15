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

`similarity` and `verdict` come from `ivc_compare samples/00_original.png
<file>` (SSIM of 32×32 thumbnails in the best of the 8 orientations;
SIMILAR at ≥ 0.70). `exact` and `visible` come from `ivc_diff`.

| File | Edit | Similarity | Verdict | Exact (pixels changed) | Visible changes |
|---|---|---|---|---|---|
| `00_original.png` | reference | — | — | — | — |
| `01_identical_pixels.png` | extra metadata only | 1.000 | **IDENTICAL** | 0 | none |
| `02_one_pixel.png` | one channel of one pixel +1 | 1.000 | SIMILAR | **1**, at (100,100) | none |
| `03_small_region_edit.png` | 16×16 window painted over (like AI inpainting) | 0.996 | SIMILAR | **256**, box 164–179 × 160–175 | **1 region**, around the window |
| `04_brightness_plus5.png` | whole image +5 brightness | 0.999 | SIMILAR | all | none |
| `05_resized_half.png` | 128×128 | 0.999 | SIMILAR | (sizes differ) | none |
| `06_recompressed_q85.jpg` | JPEG, quality 85 | 0.999 | SIMILAR | almost all | none |
| `07_cropped_5pct.png` | 5% cut from each side | 0.851 | SIMILAR | (sizes differ) | many — needs alignment |
| `08_mirrored.png` | horizontal flip | 1.000 | SIMILAR (mirrored left-right) | 0 after turning back | none |
| `09_unrelated.png` | different scene | 0.519 | DIFFERENT | — | — |
| `10_rotated_90.png` | 90° counter-clockwise | 1.000 | SIMILAR (rotated 90) | 0 after turning back | none |
| `11_rotated_5deg.png` | 5°, black corners | 0.868 | SIMILAR | almost all | many — needs alignment |

`01` is the key check for the ID: the **file** bytes differ (different
SHA-256 of the file), but the **pixels** are identical, so the pixel hash
must match.

`02` and `03` are the key checks for differences: the exact comparison
locates even a single changed pixel, and the window edit is the only
visible change.

`07` and `11` are recognised as versions but are not aligned with the
original, so their difference maps are not meaningful yet.
