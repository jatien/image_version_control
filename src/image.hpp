#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A decoded image: a grid of pixels, stored row by row (row-major),
// with each pixel's channels next to each other (interleaved):
//
//   pixels = R G B A | R G B A | ...   (row 0, left to right)
//            R G B A | R G B A | ...   (row 1)
//            ...
//
// Each channel value is an 8-bit integer from 0 to 255.
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;              // 1 = gray, 2 = gray+alpha, 3 = RGB, 4 = RGBA
    std::vector<uint8_t> pixels;   // size = width * height * channels

    // Index of channel c of the pixel at column x, row y.
    std::size_t index(int x, int y, int c = 0) const {
        return (static_cast<std::size_t>(y) * width + x) * channels + c;
    }

    uint8_t at(int x, int y, int c) const { return pixels[index(x, y, c)]; }

    std::size_t size_bytes() const { return pixels.size(); }
};

// Loads an image file (PNG, JPEG, BMP, GIF, TGA, PSD, HDR, PNM) and returns
// its decoded pixels.
//
// desired_channels forces the output layout so every image is comparable:
//   4 (default) -> always RGBA, whatever the file had. JPEGs get alpha = 255.
//   3 -> RGB, 1 -> grayscale, 0 -> keep the file's own channel count.
//
// Throws std::runtime_error if the file can't be read or decoded.
Image load_image(const std::string& path, int desired_channels = 4);
