#pragma once

#include <vector>

#include "image.hpp"

// A single-channel image with float values (usually 0..255), row-major.
// Used for grayscale images and for individual colour channels.
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
