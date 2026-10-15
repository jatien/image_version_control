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

# Compare images with a reference: Hamming distances and a verdict
./build/ivc_compare samples/00_original.png samples/*.png samples/*.jpg
```

## Hashes

- **Exact hash** (`src/image_hash.hpp`): SHA-256 (implemented from scratch
  in `src/sha256.cpp`) over a version tag, the width, height and channel count,
  and the decoded RGBA pixels. Identical pixels give the same hash whatever the
  file format or metadata; changing a single pixel value gives a completely
  different hash.
- **Similarity hashes** (`src/similarity_hash.hpp`): 64-bit fingerprints
  compared with the Hamming distance (number of differing bits, 0–64).
  - *dHash*: 9×8 grayscale, one bit per "is the right neighbour brighter?".
  - *pHash*: 32×32 grayscale, 2D DCT, the 8×8 lowest frequencies compared
    with their median. Used for the verdict: distance ≤ 10 means SIMILAR.

  These are not unique and can be fooled on purpose. They answer "is this
  probably a version of that?"; the exact hash answers "is it the same?".

Test images and their expected results are in [`samples/`](samples/README.md).
