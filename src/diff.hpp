#pragma once

#include <string>
#include <vector>

#include "compare.hpp"
#include "image.hpp"
#include "sha256.hpp"
#include "gray.hpp"

// Image difference: WHERE two versions of an image differ.
//
// 1. Identity: SHA-256 of each image's pixels (exact_hash) is its ID.
// 2. Alignment: compare_images() finds the orientation (rotation/mirror);
//    the image is turned back and, if its size differs, both are brought to
//    the smaller of the two sizes.
// 3. Exact differences: when the sizes match, every pixel is compared, so
//    even a single changed value is counted and located.
// 4. Visible differences: SSIM at two scales, keeping the worse value at
//    each point:
//      - brightness (luma) at 1/2 size: changes of shape, texture, brightness
//      - each colour channel at 1/4 size: changes of colour
//    Pixel-level noise (JPEG artifacts, sensor noise) disappears at these
//    scales, while real edits remain. Points below the threshold are
//    grouped into connected regions.

struct Box {
    int x = 0, y = 0, width = 0, height = 0;  // in reference pixels
};

struct Region {
    Box box;
    int area = 0;             // reference pixels covered (approximately)
    float min_ssim = 1.0f;    // lowest SSIM inside the region
};

struct ImageDiff {
    Sha256::Digest reference_id{};
    Sha256::Digest image_id{};
    Comparison comparison;    // verdict, orientation, similarity score

    // Exact differences (only when the aligned image has the reference's size).
    bool exact_available = false;
    long exact_pixels = 0;    // pixels with at least one differing channel
    Box exact_box;            // smallest box containing all of them

    // Visible differences.
    GrayImage ssim_map;       // combined SSIM, at the comparison scale
    int map_scale = 2;        // reference pixels per map pixel
    double mean_ssim = 1.0;
    std::vector<Region> regions;  // largest first
};

// Changed when the combined SSIM is below `threshold`. On the samples,
// invisible changes (JPEG, resize, brightness +5) stay above 0.84 while the
// window edit drops to about -0.01.
ImageDiff diff_images(const Image& reference, const Image& image, float threshold = 0.6f,
                      bool search_orientations = true);

// A picture of the difference, at the reference's size: the reference in
// dimmed grey, changes tinted red by how much they differ, and each region
// outlined.
Image render_diff(const Image& reference, const ImageDiff& diff, float threshold = 0.6f);

// Writes an image as PNG. Throws std::runtime_error on failure.
void write_png(const std::string& path, const Image& img);
