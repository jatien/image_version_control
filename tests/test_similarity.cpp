// Tests for the similarity hashes (dHash, pHash) and Hamming distance.
// Run with:   ctest --test-dir build --output-on-failure

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "image.hpp"
#include "similarity_hash.hpp"

static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << '\n';
    if (!ok) ++failures;
}

static void hamming() {
    std::cout << "Hamming distance\n";
    check(hamming_distance(0, 0) == 0, "equal -> 0");
    check(hamming_distance(0, ~0ULL) == 64, "complement -> 64");
    check(hamming_distance(0b1011, 0b0010) == 2, "1011 vs 0010 -> 2");
    check(hamming_distance(1ULL << 63, 0) == 1, "top bit only -> 1");
    check(to_hex64(0x0123456789abcdefULL) == "0123456789abcdef", "to_hex64");
}

static void dct() {
    std::cout << "2D DCT (reference: scipy.fft.dctn, norm='ortho')\n";
    const int n = 8;
    std::vector<double> x(n * n);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) x[i * n + j] = (i * n + j) % 7 + i;

    const auto f = dct2d(x, n);
    const double want[2][4] = {{51.625, -1.064045, -1.143243, 0.790789},
                               {-19.285686, 1.816605, -5.729417, -1.427113}};
    bool ok = true;
    for (int v = 0; v < 2; ++v)
        for (int u = 0; u < 4; ++u) ok = ok && std::fabs(f[v * n + u] - want[v][u]) < 1e-5;
    check(ok, "matches scipy on an 8x8 block");

    std::vector<double> flat(n * n, 10.0);
    const auto g = dct2d(flat, n);
    bool only_dc = std::fabs(g[0] - 80.0) < 1e-9;  // 10 * n for orthonormal DCT
    for (int i = 1; i < n * n; ++i) only_dc = only_dc && std::fabs(g[i]) < 1e-9;
    check(only_dc, "constant block -> energy only in DC term");
}

static void resize() {
    std::cout << "Area resize\n";
    GrayImage g;
    g.width = 4; g.height = 2;
    g.values = {0, 2, 4, 6,
                8, 10, 12, 14};
    const GrayImage s = resize_area(g, 2, 1);
    check(s.width == 2 && s.height == 1, "4x2 -> 2x1 size");
    check(s.values[0] == 5.0f && s.values[1] == 9.0f, "each output = mean of its 2x2 box");
}

static void samples() {
    std::cout << "Samples (expected results in samples/README.md)\n";
    const std::string dir = SAMPLES_DIR;
    const Image original = load_image(dir + "/00_original.png");
    const uint64_t d0 = dhash(original), p0 = phash(original);

    auto dist = [&](const std::string& f) {
        const Image img = load_image(dir + "/" + f);
        return std::pair<int, int>{hamming_distance(d0, dhash(img)),
                                   hamming_distance(p0, phash(img))};
    };

    // Edits of the original: pHash must stay within the SIMILAR threshold (10).
    for (const char* f : {"01_identical_pixels.png", "02_one_pixel.png",
                          "03_small_region_edit.png", "04_brightness_plus5.png",
                          "05_resized_half.png", "06_recompressed_q85.jpg",
                          "07_cropped_5pct.png"}) {
        const auto [dd, dp] = dist(f);
        check(dp <= 10, std::string(f) + ": pHash " + std::to_string(dp) +
                            " <= 10 (dHash " + std::to_string(dd) + ")");
    }
    check(dist("01_identical_pixels.png").first == 0 &&
              dist("01_identical_pixels.png").second == 0,
          "01 identical pixels -> both distances 0");

    // Not versions of the original: pHash must be far.
    for (const char* f : {"08_mirrored.png", "09_unrelated.png"}) {
        const auto [dd, dp] = dist(f);
        check(dp >= 20, std::string(f) + ": pHash " + std::to_string(dp) +
                            " >= 20 (dHash " + std::to_string(dd) + ")");
    }
}

int main() {
    hamming();
    dct();
    resize();
    samples();

    if (failures == 0) {
        std::cout << "\nAll tests passed.\n";
        return 0;
    }
    std::cout << '\n' << failures << " test(s) FAILED.\n";
    return 1;
}
