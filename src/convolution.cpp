#include "convolution.hpp"

#include <cmath>
#include <stdexcept>

namespace {

// Mirrors an out-of-range index back into [0, n): -1 -> 0, -2 -> 1, n -> n-1.
inline int reflect(int i, int n) {
    if (n == 1) return 0;
    const int period = 2 * n;
    i %= period;
    if (i < 0) i += period;
    return i < n ? i : period - 1 - i;
}

}  // namespace

GrayImage convolve_separable(const GrayImage& img, const std::vector<float>& kx,
                             const std::vector<float>& ky) {
    if (kx.size() % 2 == 0 || ky.size() % 2 == 0)
        throw std::invalid_argument("kernel length must be odd");

    const int w = img.width, h = img.height;
    const int rx = static_cast<int>(kx.size() / 2), ry = static_cast<int>(ky.size() / 2);

    // Horizontal pass. f(x - i) k(i): with i from -r to r and k stored at
    // index i + r. (For symmetric kernels such as the Gaussian, this is the
    // same as correlation.)
    GrayImage tmp = img;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double s = 0;
            for (int i = -rx; i <= rx; ++i)
                s += img.at(reflect(x - i, w), y) * kx[i + rx];
            tmp.values[static_cast<std::size_t>(y) * w + x] = static_cast<float>(s);
        }

    // Vertical pass.
    GrayImage out = img;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double s = 0;
            for (int j = -ry; j <= ry; ++j)
                s += tmp.at(x, reflect(y - j, h)) * ky[j + ry];
            out.values[static_cast<std::size_t>(y) * w + x] = static_cast<float>(s);
        }
    return out;
}

std::vector<float> gaussian_kernel(float sigma, float truncate) {
    if (sigma <= 0) throw std::invalid_argument("sigma must be positive");
    const int r = static_cast<int>(truncate * sigma + 0.5f);

    std::vector<float> k(2 * r + 1);
    double sum = 0;
    for (int i = -r; i <= r; ++i) {
        const double v = std::exp(-(double(i) * i) / (2.0 * sigma * sigma));
        k[i + r] = static_cast<float>(v);
        sum += v;
    }
    for (float& v : k) v = static_cast<float>(v / sum);
    return k;
}

GrayImage gaussian_blur(const GrayImage& img, float sigma, float truncate) {
    const std::vector<float> k = gaussian_kernel(sigma, truncate);
    return convolve_separable(img, k, k);
}
