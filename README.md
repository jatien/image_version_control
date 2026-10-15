# image_version_control
A version control system similar to git for images.

## Build

Requires CMake (3.14+) and a C++17 compiler. On Ubuntu/Debian:
`sudo apt install cmake g++`.

```bash
cmake -S . -B build        # configure (once, creates build/)
cmake --build build        # compile
ctest --test-dir build --output-on-failure   # run the tests
```

## Tools

```bash
# Pixel values of an image (summary, or every pixel as "x y R G B A")
./build/ivc_pixels samples/00_original.png
./build/ivc_pixels samples/00_original.png --dump > pixels.txt

# Exact hash of each image's pixels
./build/ivc_hash samples/*.png samples/*.jpg

# Is each image a version of the reference? Similarity, verdict, orientation
./build/ivc_compare samples/00_original.png samples/*.png samples/*.jpg

# Where two versions differ: IDs, exact and visible changes, difference image
./build/ivc_diff samples/00_original.png samples/03_small_region_edit.png -o diff.png
```

![Difference image for the window edit](docs/diff_window_edit.png)

*`ivc_diff` on `03_small_region_edit.png`: the painted-over window is the
only change, outlined in yellow.*

## Image ID

Every image is identified by the **SHA-256 of its decoded pixels**
(`exact_hash`). Same pixels → same ID, whatever the file format,
compression or metadata.

## Differences: SSIM

`diff_images` (`src/diff.hpp`) shows *where* two versions differ:

1. **Align**: find the orientation with `compare_images` and turn the image
   back; if the sizes differ, compare at the smaller one.
2. **Exact differences**: with equal sizes, every pixel is compared, so even a
   single changed value is counted and located.
3. **Visible differences**: SSIM (structural similarity, `src/ssim.hpp`), which
   compares local brightness, contrast and structure around every pixel. It is
   built from Gaussian convolutions (`src/convolution.hpp`) and matches
   scikit-image to about 0.001. It is computed at two scales and the worse
   value kept at each point:
   - brightness at ½ size: changes of shape, texture or brightness;
   - each colour channel at ¼ size: changes of colour.

   At these scales pixel noise (JPEG artifacts, sensor noise) disappears while
   real edits remain: on the samples, JPEG stays above SSIM 0.84 everywhere
   while the window edit drops to −0.01. Points below 0.6 are grouped into
   changed regions.

Known limitation: crops and arbitrary rotations are recognised as versions,
but their difference map lights up because the content is shifted; they
need an alignment step (phase correlation / Fourier–Mellin), not
implemented yet.

## How images are compared

Two tools, each doing one job:

- **SHA-256 → identity** (`src/image_hash.hpp`, SHA-256 implemented from
  scratch in `src/sha256.cpp`): hashes a version tag, the width, height and
  channel count, and the decoded RGBA pixels. Identical pixels give the same
  ID whatever the file format or metadata; changing a single pixel value
  gives a completely different ID.
- **SSIM → similarity and differences** (`src/ssim.hpp`):
  - *Is it a version?* `compare_images` (`src/compare.hpp`) compares 32×32
    grayscale thumbnails with SSIM, trying the image in all 8 rotations by
    90° and mirrorings (`src/orientation.hpp`) and keeping the best.
    Similarity ≥ 0.70 means SIMILAR. On the samples, every edit scores
    ≥ 0.85 and the unrelated image ≤ 0.52.
  - *Where does it differ?* `diff_images`, described above.

All of these are pure functions (image in, result out, no state); the
version tracker will be a separate layer that calls them.

Test images and their expected results are in [`samples/`](samples/README.md).
