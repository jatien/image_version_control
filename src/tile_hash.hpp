#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "image.hpp"

// Tile hashes: the image is cut into a grid (8 x 8 by default) whose tiles
// scale with the image, so a resized copy lines up tile for tile. Each tile
// gets two values:
//
//   exact  64 bits of SHA-256 over the tile's pixels. Equal means the tile
//          is untouched; any change to any pixel makes it different.
//   thumb  the tile's brightness averaged down to 4 x 4 cells. Compared
//          numerically, it says how much the tile changed visually and
//          ignores invisible noise such as JPEG recompression.
//
// Comparing two grids tile by tile shows WHERE an image was edited, which
// a single whole-image hash cannot do.

constexpr int kTileThumb = 4;  // thumbnail is kTileThumb x kTileThumb

struct Tile {
    uint64_t exact = 0;
    std::array<uint8_t, kTileThumb * kTileThumb> thumb{};
};

struct TileGrid {
    int cols = 0;
    int rows = 0;
    int image_width = 0;
    int image_height = 0;
    float mean_brightness = 0;  // whole image, used to cancel global shifts
    std::vector<Tile> tiles;    // row-major: tiles[row * cols + col]
};

TileGrid compute_tiles(const Image& img, int cols = 8, int rows = 8);

enum class TileState {
    Unchanged,  // exact hash equal: not a single pixel differs
    Noise,      // pixels differ but it looks the same (e.g. JPEG, resize)
    Changed,    // visibly different
};

struct TileComparison {
    int cols = 0;
    int rows = 0;
    std::vector<float> distance;   // mean brightness difference per tile, 0..255
    std::vector<TileState> state;
    int changed = 0;               // number of tiles in state Changed
};

// Tiles count as Changed when their thumbnails differ by more than
// `threshold` brightness levels on average (after removing any global
// brightness shift between the two images). Both grids must have the same
// number of rows and columns.
TileComparison compare_tiles(const TileGrid& a, const TileGrid& b,
                             float threshold = 3.0f);
