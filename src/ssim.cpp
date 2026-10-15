#include "ssim.hpp"

#include <stdexcept>

#include "convolution.hpp"

namespace {

GrayImage multiply(const GrayImage& a, const GrayImage& b) {
    GrayImage out = a;
    for (std::size_t i = 0; i < out.values.size(); ++i) out.values[i] *= b.values[i];
    return out;
}

}  // namespace

SsimMap ssim(const GrayImage& x, const GrayImage& y, float sigma, double data_range) {
    if (x.width != y.width || x.height != y.height)
        throw std::invalid_argument("ssim: images must have the same size");

    const double c1 = (0.01 * data_range) * (0.01 * data_range);
    const double c2 = (0.03 * data_range) * (0.03 * data_range);

    // The five convolutions.
    const GrayImage mu_x = gaussian_blur(x, sigma);
    const GrayImage mu_y = gaussian_blur(y, sigma);
    const GrayImage xx = gaussian_blur(multiply(x, x), sigma);
    const GrayImage yy = gaussian_blur(multiply(y, y), sigma);
    const GrayImage xy = gaussian_blur(multiply(x, y), sigma);

    SsimMap result;
    result.map = x;
    double total = 0;
    for (std::size_t i = 0; i < x.values.size(); ++i) {
        const double mx = mu_x.values[i], my = mu_y.values[i];
        const double vx = xx.values[i] - mx * mx;   // sigma_x^2
        const double vy = yy.values[i] - my * my;   // sigma_y^2
        const double cov = xy.values[i] - mx * my;  // sigma_xy

        const double s = ((2 * mx * my + c1) * (2 * cov + c2)) /
                         ((mx * mx + my * my + c1) * (vx + vy + c2));
        result.map.values[i] = static_cast<float>(s);
        total += s;
    }
    result.mean = total / x.values.size();
    return result;
}

GrayImage channel_plane(const Image& img, int c) {
    if (c < 0 || c >= img.channels) throw std::out_of_range("channel_plane: no such channel");
    GrayImage g;
    g.width = img.width;
    g.height = img.height;
    g.values.resize(static_cast<std::size_t>(img.width) * img.height);
    for (std::size_t i = 0; i < g.values.size(); ++i)
        g.values[i] = img.pixels[i * img.channels + c];
    return g;
}
