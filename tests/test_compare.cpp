// Tests for orientations and the SSIM-based comparison.
// Run with:   ctest --test-dir build --output-on-failure

#include <iostream>
#include <string>

#include "compare.hpp"
#include "image.hpp"
#include "image_hash.hpp"
#include "orientation.hpp"

static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << '\n';
    if (!ok) ++failures;
}

static const std::string dir = SAMPLES_DIR;
static Image sample(const std::string& f) { return load_image(dir + "/" + f); }

static void orientations() {
    std::cout << "Orientations\n";
    // 3x2 image with distinct pixel values, so any mix-up is visible.
    Image img;
    img.width = 3; img.height = 2; img.channels = 1;
    img.pixels = {1, 2, 3,
                  4, 5, 6};

    const Image r90 = apply_orientation(img, Orientation::Rot90);
    check(r90.width == 2 && r90.height == 3, "rot90 swaps width and height");
    check(r90.pixels == std::vector<uint8_t>({3, 6, 2, 5, 1, 4}),
          "rot90 is counter-clockwise (matches numpy.rot90)");
    check(apply_orientation(img, Orientation::FlipH).pixels ==
              std::vector<uint8_t>({3, 2, 1, 6, 5, 4}),
          "flipH mirrors each row");

    bool all_undo = true;
    for (Orientation o : kAllOrientations) {
        const Image back = apply_orientation(apply_orientation(img, o), inverse(o));
        all_undo = all_undo && back.pixels == img.pixels && back.width == img.width;
    }
    check(all_undo, "inverse(o) undoes o for all 8 orientations");

    const Image orig = sample("00_original.png");
    check(exact_hash(apply_orientation(orig, Orientation::Rot90)) ==
              exact_hash(sample("10_rotated_90.png")),
          "rot90 of 00 has exactly the pixels of 10_rotated_90");
    check(exact_hash(apply_orientation(orig, Orientation::FlipH)) ==
              exact_hash(sample("08_mirrored.png")),
          "flipH of 00 has exactly the pixels of 08_mirrored");
}

static std::string score(double s) {
    std::string t = std::to_string(s);
    return t.substr(0, t.find('.') + 4);
}

static void comparisons() {
    std::cout << "compare_images on samples/ (threshold "
              << score(kSimilarThreshold) << ")\n";
    const Image ref = sample("00_original.png");
    auto cmp = [&](const std::string& f) { return compare_images(ref, sample(f)); };

    const Comparison same = cmp("01_identical_pixels.png");
    check(same.verdict == Verdict::Identical && same.similarity == 1.0, "01 -> IDENTICAL");

    // Every real version must be SIMILAR, not turned, with a clear margin.
    for (const char* f : {"02_one_pixel.png", "03_small_region_edit.png",
                          "04_brightness_plus5.png", "05_resized_half.png",
                          "06_recompressed_q85.jpg", "07_cropped_5pct.png",
                          "11_rotated_5deg.png"}) {
        const Comparison c = cmp(f);
        check(c.verdict == Verdict::Similar && c.orientation == Orientation::Identity &&
                  c.similarity >= 0.80,
              std::string(f) + " -> SIMILAR, not turned (similarity " + score(c.similarity) +
                  " >= 0.80)");
    }

    const Comparison m = cmp("08_mirrored.png");
    check(m.verdict == Verdict::Similar && m.orientation == Orientation::FlipH &&
              m.similarity > 0.999,
          "08 -> SIMILAR, mirrored left-right");

    const Comparison r = cmp("10_rotated_90.png");
    check(r.verdict == Verdict::Similar && r.orientation == Orientation::Rot90 &&
              r.similarity > 0.999,
          "10 -> SIMILAR, rotated 90");

    const Comparison u = cmp("09_unrelated.png");
    check(u.verdict == Verdict::Different && u.similarity <= 0.60,
          "09 unrelated -> DIFFERENT (similarity " + score(u.similarity) +
              " <= 0.60 in its best orientation)");
}

int main() {
    orientations();
    comparisons();

    if (failures == 0) {
        std::cout << "\nAll tests passed.\n";
        return 0;
    }
    std::cout << '\n' << failures << " test(s) FAILED.\n";
    return 1;
}
