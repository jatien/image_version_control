// Tests for convolution, SSIM and the image difference.
// Run with:   ctest --test-dir build --output-on-failure

#include <cmath>
#include <iostream>
#include <string>

#include "convolution.hpp"
#include "diff.hpp"
#include "image.hpp"
#include "ssim.hpp"

static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << '\n';
    if (!ok) ++failures;
}

static const std::string dir = SAMPLES_DIR;
static Image sample(const std::string& f) { return load_image(dir + "/" + f); }

static GrayImage plane(int w, int h, float v) {
    GrayImage g;
    g.width = w;
    g.height = h;
    g.values.assign(static_cast<std::size_t>(w) * h, v);
    return g;
}

static void convolution() {
    std::cout << "Convolution\n";
    const auto k = gaussian_kernel(1.5f);
    double sum = 0;
    for (float v : k) sum += v;
    check(k.size() == 11, "sigma 1.5 -> 11 taps (radius 5, truncate 3.5)");
    check(std::fabs(sum - 1.0) < 1e-6, "kernel sums to 1");
    check(k[0] == k[10] && k[2] == k[8], "kernel is symmetric");

    const GrayImage flat = gaussian_blur(plane(20, 15, 42.0f), 1.5f);
    bool unchanged = true;
    for (float v : flat.values) unchanged = unchanged && std::fabs(v - 42.0f) < 1e-3f;
    check(unchanged, "blurring a constant image changes nothing, edges included");

    // Impulse response: blurring a single 1 reproduces the 2D kernel.
    GrayImage impulse = plane(21, 21, 0.0f);
    impulse.values[10 * 21 + 10] = 1.0f;
    const GrayImage r = gaussian_blur(impulse, 1.5f);
    check(std::fabs(r.at(10, 10) - k[5] * k[5]) < 1e-6f &&
              std::fabs(r.at(13, 8) - k[8] * k[3]) < 1e-6f,
          "impulse response = outer product of the 1D kernel");
}

static void ssim_values() {
    std::cout << "SSIM (reference: skimage.metrics.structural_similarity)\n";
    const Image a = sample("00_original.png");
    const SsimMap self = ssim(channel_plane(a, 0), channel_plane(a, 0));
    bool ones = true;
    for (float v : self.map.values) ones = ones && std::fabs(v - 1.0f) < 1e-4f;
    check(ones && std::fabs(self.mean - 1.0) < 1e-6, "image vs itself -> 1 everywhere");

    // skimage: gaussian_weights=True, sigma=1.5, use_sample_covariance=False,
    // data_range=255, full=True, on the red channel.
    const SsimMap j = ssim(channel_plane(a, 0), channel_plane(sample("06_recompressed_q85.jpg"), 0));
    check(std::fabs(j.mean - 0.799600) < 2e-3, "00 vs 06, mean of map = 0.7996");
    check(std::fabs(j.map.at(0, 0) - 0.786762) < 2e-3, "00 vs 06, corner (0,0) = 0.7868");
    check(std::fabs(j.map.at(100, 100) - 0.833628) < 2e-3, "00 vs 06, (100,100) = 0.8336");

    const SsimMap e = ssim(channel_plane(a, 0), channel_plane(sample("03_small_region_edit.png"), 0));
    check(std::fabs(e.map.at(168, 170) - 0.229047) < 2e-3, "00 vs 03, inside window = 0.2290");
    check(std::fabs(e.map.at(100, 100) - 1.0) < 1e-4, "00 vs 03, far from window = 1");
}

static bool inside(const Box& outer, const Box& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

static void differences() {
    std::cout << "diff_images on samples/\n";
    const Image ref = sample("00_original.png");
    auto diff = [&](const std::string& f) { return diff_images(ref, sample(f)); };

    check(diff("01_identical_pixels.png").comparison.verdict == Verdict::Identical,
          "01 -> identical");

    const ImageDiff one = diff("02_one_pixel.png");
    check(one.exact_available && one.exact_pixels == 1 && one.exact_box.x == 100 &&
              one.exact_box.y == 100,
          "02 -> exactly 1 pixel, at (100,100)");
    check(one.regions.empty(), "02 -> no visible change");

    const ImageDiff win = diff("03_small_region_edit.png");
    const Box window{164, 160, 16, 16};
    check(win.exact_pixels == 256 && win.exact_box.x == 164 && win.exact_box.y == 160 &&
              win.exact_box.width == 16 && win.exact_box.height == 16,
          "03 -> exactly 256 pixels in the 16x16 window box");
    check(win.regions.size() == 1 && inside(win.regions[0].box, window),
          "03 -> one visible region, containing the window");

    for (const char* f : {"04_brightness_plus5.png", "05_resized_half.png",
                          "06_recompressed_q85.jpg"}) {
        const ImageDiff d = diff(f);
        check(d.regions.empty(), std::string(f) + " -> no visible change");
    }

    for (const char* f : {"08_mirrored.png", "10_rotated_90.png"}) {
        const ImageDiff d = diff(f);
        check(d.exact_available && d.exact_pixels == 0 && d.regions.empty(),
              std::string(f) + " -> turned back, not a single pixel differs");
    }
}

static void png_round_trip() {
    std::cout << "PNG writing\n";
    const Image a = sample("03_small_region_edit.png");
    const std::string path = std::string(BINARY_DIR) + "/round_trip.png";
    write_png(path, a);
    const Image b = load_image(path);
    check(a.width == b.width && a.height == b.height && a.pixels == b.pixels,
          "write_png then load_image gives the same pixels");
}

int main() {
    convolution();
    ssim_values();
    differences();
    png_round_trip();

    if (failures == 0) {
        std::cout << "\nAll tests passed.\n";
        return 0;
    }
    std::cout << '\n' << failures << " test(s) FAILED.\n";
    return 1;
}
