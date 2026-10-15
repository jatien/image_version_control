#pragma once

#include "image.hpp"
#include "similarity_hash.hpp"  // GrayImage

// SSIM, the structural similarity index (Wang, Bovik, Sheikh, Simoncelli,
// "Image quality assessment: from error visibility to structural
// similarity", IEEE Trans. Image Processing, 2004).
//
// Around every pixel, a Gaussian window (sigma 1.5) measures the two
// images' local mean mu, variance sigma^2 and covariance sigma_xy, all as
// convolutions:
//
//   mu_x = G * x        sigma_x^2 = G * x^2 - mu_x^2    sigma_xy = G * (xy) - mu_x mu_y
//
//   SSIM = (2 mu_x mu_y + C1)(2 sigma_xy + C2)
//          / ((mu_x^2 + mu_y^2 + C1)(sigma_x^2 + sigma_y^2 + C2))
//
// with C1 = (0.01 L)^2, C2 = (0.03 L)^2, L = 255. The result is 1 where the
// neighbourhoods are identical and drops towards 0 (or below) where they
// differ in brightness, contrast or structure.
//
// Matches skimage.metrics.structural_similarity(gaussian_weights=True,
// sigma=1.5, use_sample_covariance=False, data_range=255) to about 1e-3.

struct SsimMap {
    GrayImage map;      // SSIM at every pixel
    double mean = 1.0;  // average over the image ("the" SSIM score)
};

// SSIM of two single-channel images of the same size.
SsimMap ssim(const GrayImage& x, const GrayImage& y, float sigma = 1.5f,
             double data_range = 255.0);

// One channel of an image as a float plane (0 = R, 1 = G, 2 = B, ...).
GrayImage channel_plane(const Image& img, int c);
