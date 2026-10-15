#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "image.hpp"

// Similarity (perceptual) hashes: 64-bit fingerprints where images that LOOK
// alike get hashes that differ in few bits. Compare two of them with
// hamming_distance(): 0 means "same fingerprint", 64 means "every bit differs".
//
// Unlike exact_hash(), these are NOT unique and NOT secure: different images
// can share a fingerprint, and someone can craft such pairs on purpose. They
// answer "is this probably a version of that?", never "is this the same?".

// A grayscale image with float values in [0, 255], row-major.
struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<float> values;

    float at(int x, int y) const { return values[static_cast<std::size_t>(y) * width + x]; }
};

// Luminance (ITU-R BT.601): 0.299 R + 0.587 G + 0.114 B. Alpha is ignored.
GrayImage to_gray(const Image& img);

// Shrinks (or stretches) to out_w x out_h by averaging the source pixels
// that fall inside each output pixel's box ("area" resampling).
GrayImage resize_area(const GrayImage& src, int out_w, int out_h);

// dHash (difference hash): shrink to 9x8 gray, then for each row compare
// each pixel with its right neighbour. Bit = 1 when brightness increases.
// Captures gradients; robust to brightness, contrast and JPEG; very fast.
uint64_t dhash(const Image& img);

// pHash (DCT hash): shrink to 32x32 gray, take the 2D discrete cosine
// transform, keep the 8x8 lowest frequencies (overall structure, not
// detail) and set each bit by comparing with their median.
// More robust than dHash to resizing, blur and recompression.
uint64_t phash(const Image& img);

// Number of differing bits between two 64-bit hashes (0..64).
int hamming_distance(uint64_t a, uint64_t b);

// 16 hex digits, e.g. "f0e1d2c3b4a59687".
std::string to_hex64(uint64_t h);

// 2D DCT-II of an n x n block (orthonormal). Exposed for testing.
std::vector<double> dct2d(const std::vector<double>& block, int n);
