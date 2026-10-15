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

# Compare images with a reference: distances, changed tiles and a verdict
./build/ivc_compare samples/00_original.png samples/*.png samples/*.jpg

# Same, drawing the 8x8 tile map (. unchanged, ~ noise only, X changed)
./build/ivc_compare --map samples/00_original.png samples/03_small_region_edit.png
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
- **Orientations** (`src/orientation.hpp`): the comparison tries the image in
  all 8 rotations by 90° and mirrorings, so rotated or mirrored versions are
  recognised. Arbitrary angles are not handled yet.
- **Tile hashes** (`src/tile_hash.hpp`): an 8×8 grid that scales with the
  image. Each tile has an exact 64-bit hash (any pixel changed?) and a 4×4
  brightness thumbnail (visibly changed?), so a comparison shows *where* an
  image was edited. `ivc_compare --map` draws the grid.
- **`compare_images`** (`src/compare.hpp`) combines them: exact hash, then
  pHash in the best orientation, then tiles. All hashes are pure functions;
  the version tracker will be a separate layer that calls them.

Test images and their expected results are in [`samples/`](samples/README.md).
