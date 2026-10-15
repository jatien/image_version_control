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

## Version control: `ivc`

`ivc` works like git, on a folder of images. Add `build/` to your `PATH`
(or call `./build/ivc`):

```bash
cd my-photos
ivc init                                  # create the repository (.ivc/)
ivc add .                                 # stage every image in the folder
ivc commit -m "First version"

# ...edit photo.png in any editor...
ivc status                                # modified  photo.png
ivc diff photo.png -o diff.png            # what changed, and where
ivc add photo.png
ivc commit -m "Remove the window" --edit "brush x=164 y=160 w=16 h=16"

ivc log                                   # history, newest first
ivc show HEAD~1                           # one commit in full

ivc checkout HEAD~1                       # go back to the previous version
ivc checkout main                         # and forward again
ivc checkout -b black-white               # start a branch from here
ivc branch                                # list branches
ivc restore photo.png --from HEAD~2       # bring back one file
```

`ivc help` lists every command. A revision is a branch name, a commit ID or
its first 4+ characters, `HEAD` (the current commit), or `<revision>~<n>`
for *n* commits back.

Example `ivc log` entry, for a photo rotated in an editor:

```
commit 76573e3d (HEAD -> main)
author  Javi
date    2026-10-15T21:35:35Z

    Rotate

    edit  rotate 90
    modified  photo.png   (similarity 1.000, detected rotated 90, pixels 0 changed, visible 0 region(s))
```

The change was *identified* as a pure rotation: after turning it back, not a
single pixel differs.

### What is stored

Everything lives in `.ivc/` at the root of the folder:

| Path | Content |
|---|---|
| `HEAD` | `ref: refs/heads/<branch>`, or a commit ID when looking at an older version |
| `refs/heads/<branch>` | the latest commit of each branch |
| `index` | the staging area: the files the next commit will contain |
| `objects/files/<id>` | the original bytes of every version of every file, named by their SHA-256 |
| `objects/commits/<id>` | the commits, as text, named by the SHA-256 of that text |

Each file version has two IDs: the **file ID** (SHA-256 of its bytes), used
to restore it exactly, and the **image ID** (SHA-256 of its pixels), its
identity whatever the format or metadata. A commit is a readable text file:

```
ivc-commit 1
parent 40bd5a83eecb60ca2a1256723891681073234b61aa49c602d65f65273feeaf29
author Javi
date 2026-10-15T21:35:35Z
edit rotate 90
file f656ef81…c182074e 47e2d2c8…7127c257 holidays/sunset.png
file f2d31b4a…bfae5251 1ad8892e…2c6d2be8 photo.png
change modified photo.png
  similarity 1.000
  detected rotated 90
  pixels 0 changed
  visible 0 region(s)

Rotate
```

- `file <image id> <file id> <path>`: every file in this version.
- `change <kind> <path>`: what happened, with kind `added`, `modified`,
  `replaced` (no longer similar), `metadata` (same pixels, different bytes)
  or `deleted`; the indented lines are what the comparison measured:
  similarity, detected rotation, mirror or resize, changed pixels and regions.
- `edit <tool key> <parameters>`: written by an editor (`--edit`) to record
  which tool made the change. Unknown lines are ignored, so new fields can be
  added later without breaking old repositories.

Not yet: merging branches, and storing only the changed parts of an image
(each version currently stores the full file).

## Inspection tools

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

All of these are pure functions (image in, result out, no state); `ivc`
(`src/repository.hpp`) is a separate layer that calls them.

Test images and their expected results are in [`samples/`](samples/README.md).
