#include "similarity_hash.hpp"

#include <algorithm>
#include <cmath>
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

uint64_t dhash(const Image& img) {
    const GrayImage small = resize_area(to_gray(img), 9, 8);

    uint64_t h = 0;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            h <<= 1;
            if (small.at(x, y) < small.at(x + 1, y)) h |= 1;
        }
    }
    return h;
}

std::vector<double> dct2d(const std::vector<double>& block, int n) {
    const double pi = 3.14159265358979323846;

    // Basis c[k][i] = a(k) * cos(pi * (2i + 1) * k / (2n)),
    // with a(0) = sqrt(1/n) and a(k) = sqrt(2/n), so the transform is orthonormal.
    std::vector<double> c(static_cast<std::size_t>(n) * n);
    for (int k = 0; k < n; ++k) {
        const double a = k == 0 ? std::sqrt(1.0 / n) : std::sqrt(2.0 / n);
        for (int i = 0; i < n; ++i)
            c[k * n + i] = a * std::cos(pi * (2 * i + 1) * k / (2.0 * n));
    }

    // The 2D DCT is separable: transform the rows, then the columns.
    std::vector<double> tmp(static_cast<std::size_t>(n) * n, 0.0);
    for (int y = 0; y < n; ++y)
        for (int u = 0; u < n; ++u) {
            double s = 0;
            for (int x = 0; x < n; ++x) s += c[u * n + x] * block[y * n + x];
            tmp[y * n + u] = s;
        }

    std::vector<double> out(static_cast<std::size_t>(n) * n, 0.0);
    for (int v = 0; v < n; ++v)
        for (int u = 0; u < n; ++u) {
            double s = 0;
            for (int y = 0; y < n; ++y) s += c[v * n + y] * tmp[y * n + u];
            out[v * n + u] = s;
        }
    return out;
}

uint64_t phash(const Image& img) {
    constexpr int N = 32;   // working size
    constexpr int K = 8;    // low-frequency block kept

    const GrayImage small = resize_area(to_gray(img), N, N);
    const std::vector<double> block(small.values.begin(), small.values.end());
    const std::vector<double> freq = dct2d(block, N);

    // Top-left 8x8 = lowest frequencies. Entry (0,0) is the average
    // brightness (the "DC" term); it is left out of the median so a global
    // brightness change cannot shift the threshold.
    double low[K * K];
    for (int v = 0; v < K; ++v)
        for (int u = 0; u < K; ++u) low[v * K + u] = freq[v * N + u];

    std::vector<double> ac(low + 1, low + K * K);
    std::nth_element(ac.begin(), ac.begin() + ac.size() / 2, ac.end());
    const double median = ac[ac.size() / 2];

    uint64_t h = 0;
    for (int i = 0; i < K * K; ++i) {
        h <<= 1;
        if (low[i] > median) h |= 1;
    }
    return h;
}

int hamming_distance(uint64_t a, uint64_t b) {
    uint64_t x = a ^ b;  // 1 where the bits differ
    int count = 0;
    while (x) {
        x &= x - 1;      // clears the lowest set bit
        ++count;
    }
    return count;
}

std::string to_hex64(uint64_t h) {
    static const char* digits = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i) {
        s[i] = digits[h & 0xf];
        h >>= 4;
    }
    return s;
}
