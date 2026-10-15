#include "gray.hpp"

#include <algorithm>
#include <stdexcept>

GrayImage to_gray(const Image& img) {
    if (img.channels < 1) throw std::invalid_argument("image has no channels");

    GrayImage g;
    g.width = img.width;
    g.height = img.height;
    g.values.resize(static_cast<std::size_t>(img.width) * img.height);

    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            float v;
            if (img.channels >= 3) {
                v = 0.299f * img.at(x, y, 0) + 0.587f * img.at(x, y, 1) +
                    0.114f * img.at(x, y, 2);
            } else {
                v = img.at(x, y, 0);  // already grayscale (+ maybe alpha)
            }
            g.values[static_cast<std::size_t>(y) * g.width + x] = v;
        }
    }
    return g;
}

GrayImage resize_area(const GrayImage& src, int out_w, int out_h) {
    if (src.width <= 0 || src.height <= 0 || out_w <= 0 || out_h <= 0)
        throw std::invalid_argument("resize_area: empty image or size");

    GrayImage dst;
    dst.width = out_w;
    dst.height = out_h;
    dst.values.resize(static_cast<std::size_t>(out_w) * out_h);

    // Output pixel (ox, oy) covers source columns [x0, x1) and rows [y0, y1).
    // When enlarging, the box may be empty, so it is at least one pixel.
    for (int oy = 0; oy < out_h; ++oy) {
        int y0 = static_cast<int>(static_cast<long long>(oy) * src.height / out_h);
        int y1 = static_cast<int>(static_cast<long long>(oy + 1) * src.height / out_h);
        y1 = std::max(y1, y0 + 1);
        for (int ox = 0; ox < out_w; ++ox) {
            int x0 = static_cast<int>(static_cast<long long>(ox) * src.width / out_w);
            int x1 = static_cast<int>(static_cast<long long>(ox + 1) * src.width / out_w);
            x1 = std::max(x1, x0 + 1);

            double sum = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) sum += src.at(x, y);
            dst.values[static_cast<std::size_t>(oy) * out_w + ox] =
                static_cast<float>(sum / ((y1 - y0) * (x1 - x0)));
        }
    }
    return dst;
}
