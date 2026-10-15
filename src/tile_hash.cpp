#include "tile_hash.hpp"

#include <cmath>
#include <stdexcept>

#include "sha256.hpp"
#include "similarity_hash.hpp"

namespace {

// Pixel range of tile number k when `size` pixels are split into `parts`.
inline int tile_start(int k, int size, int parts) {
    return static_cast<int>(static_cast<long long>(k) * size / parts);
}

uint64_t tile_exact(const Image& img, int x0, int y0, int x1, int y1) {
    Sha256 h;
    const uint8_t dims[4] = {uint8_t((x1 - x0) >> 8), uint8_t(x1 - x0),
                             uint8_t((y1 - y0) >> 8), uint8_t(y1 - y0)};
    h.update(dims, 4);
    const std::size_t row_bytes = static_cast<std::size_t>(x1 - x0) * img.channels;
    for (int y = y0; y < y1; ++y) h.update(&img.pixels[img.index(x0, y)], row_bytes);

    const Sha256::Digest d = h.finalize();
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | d[i];
    return v;
}

}  // namespace

TileGrid compute_tiles(const Image& img, int cols, int rows) {
    if (cols <= 0 || rows <= 0) throw std::invalid_argument("grid must be at least 1x1");
    if (img.width < cols || img.height < rows)
        throw std::invalid_argument("image smaller than the tile grid");

    TileGrid g;
    g.cols = cols;
    g.rows = rows;
    g.image_width = img.width;
    g.image_height = img.height;
    g.tiles.resize(static_cast<std::size_t>(cols) * rows);

    const GrayImage gray = to_gray(img);
    double total = 0;
    for (float v : gray.values) total += v;
    g.mean_brightness = static_cast<float>(total / gray.values.size());

    for (int r = 0; r < rows; ++r) {
        const int y0 = tile_start(r, img.height, rows), y1 = tile_start(r + 1, img.height, rows);
        for (int c = 0; c < cols; ++c) {
            const int x0 = tile_start(c, img.width, cols), x1 = tile_start(c + 1, img.width, cols);
            Tile& t = g.tiles[static_cast<std::size_t>(r) * cols + c];

            t.exact = tile_exact(img, x0, y0, x1, y1);

            GrayImage patch;
            patch.width = x1 - x0;
            patch.height = y1 - y0;
            patch.values.reserve(static_cast<std::size_t>(patch.width) * patch.height);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) patch.values.push_back(gray.at(x, y));

            const GrayImage small = resize_area(patch, kTileThumb, kTileThumb);
            for (int i = 0; i < kTileThumb * kTileThumb; ++i)
                t.thumb[i] = static_cast<uint8_t>(std::lround(small.values[i]));
        }
    }
    return g;
}

TileComparison compare_tiles(const TileGrid& a, const TileGrid& b, float threshold) {
    if (a.cols != b.cols || a.rows != b.rows)
        throw std::invalid_argument("tile grids have different sizes");

    TileComparison cmp;
    cmp.cols = a.cols;
    cmp.rows = a.rows;
    const std::size_t n = a.tiles.size();
    cmp.distance.resize(n);
    cmp.state.resize(n);

    // Exact tile hashes are only comparable when the images have the same size.
    const bool same_size = a.image_width == b.image_width && a.image_height == b.image_height;
    const float shift = b.mean_brightness - a.mean_brightness;

    for (std::size_t i = 0; i < n; ++i) {
        float sum = 0;
        for (int k = 0; k < kTileThumb * kTileThumb; ++k)
            sum += std::fabs(float(b.tiles[i].thumb[k]) - float(a.tiles[i].thumb[k]) - shift);
        cmp.distance[i] = sum / (kTileThumb * kTileThumb);

        if (same_size && a.tiles[i].exact == b.tiles[i].exact) {
            cmp.state[i] = TileState::Unchanged;
            cmp.distance[i] = 0;
        } else if (cmp.distance[i] > threshold) {
            cmp.state[i] = TileState::Changed;
            ++cmp.changed;
        } else {
            cmp.state[i] = TileState::Noise;
        }
    }
    return cmp;
}
