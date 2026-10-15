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
./build/ivc_hash samples/*.png
```

## Hashes

- **Exact hash** (`src/image_hash.hpp`): SHA-256 (implemented from scratch
  in `src/sha256.cpp`) over a version tag, the width, height and channel count,
  and the decoded RGBA pixels. Identical pixels give the same hash whatever the
  file format or metadata; changing a single pixel value gives a completely
  different hash.
- **Similarity hash**: planned.

Test images and their expected results are in [`samples/`](samples/README.md).
