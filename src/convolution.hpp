#pragma once

#include <vector>

#include "gray.hpp"

// 2D convolution with a separable kernel: k(i, j) = kx(i) * ky(j).
//
//   (f * k)(x, y) = sum_{i,j} f(x - i, y - j) k(i, j)
//
// A separable kernel of radius r costs 2(2r + 1) operations per pixel
// instead of (2r + 1)^2, because the 2D sum splits into a horizontal pass
// followed by a vertical pass.
//
// Kernels have odd length, centred on the middle element. Outside the image
// the values are mirrored, "d c b a | a b c d | d c b a" (the same as
// scipy.ndimage mode="reflect"), so edges are not darkened.
GrayImage convolve_separable(const GrayImage& img, const std::vector<float>& kx,
                             const std::vector<float>& ky);

// Normalised 1D Gaussian: exp(-i^2 / (2 sigma^2)), i in [-r, r],
// r = int(truncate * sigma + 0.5), divided by its sum so it adds up to 1.
std::vector<float> gaussian_kernel(float sigma, float truncate = 3.5f);

// Gaussian blur (convolution with a 2D Gaussian).
GrayImage gaussian_blur(const GrayImage& img, float sigma, float truncate = 3.5f);
