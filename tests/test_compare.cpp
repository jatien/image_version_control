// Tests for orientations, tile hashes and the full comparison.
// Run with:   ctest --test-dir build --output-on-failure

#include <iostream>
#include <string>

#include "compare.hpp"
#include "image.hpp"
#include "image_hash.hpp"
#include "orientation.hpp"
#include "tile_hash.hpp"

static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << '\n';
    if (!ok) ++failures;
}

static const std::string dir = SAMPLES_DIR;
static Image sample(const std::string& f) { return load_image(dir + "/" + f); }

static int count_state(const TileComparison& t, TileState s) {
    int n = 0;
    for (TileState x : t.state) n += x == s;
    return n;
}

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

static void tiles() {
    std::cout << "Tile hashes\n";
    const TileGrid g0 = compute_tiles(sample("00_original.png"));
    check(g0.cols == 8 && g0.rows == 8 && g0.tiles.size() == 64, "8x8 grid");

    const TileComparison same = compare_tiles(g0, g0);
    check(count_state(same, TileState::Unchanged) == 64, "image vs itself: all unchanged");

    // 02: one pixel at (100, 100) -> tile column 3, row 3 (32 px per tile).
    const TileComparison one = compare_tiles(g0, compute_tiles(sample("02_one_pixel.png")));
    check(count_state(one, TileState::Unchanged) == 63 && one.state[3 * 8 + 3] == TileState::Noise,
          "02 one pixel: exactly tile (3,3) differs, as noise");

    // 03: window at x 164-180, y 160-176 -> tile column 5, row 5.
    const TileComparison edit = compare_tiles(g0, compute_tiles(sample("03_small_region_edit.png")));
    check(edit.changed == 1 && edit.state[5 * 8 + 5] == TileState::Changed,
          "03 window edit: exactly tile (5,5) changed");

    for (const char* f : {"04_brightness_plus5.png", "05_resized_half.png",
                          "06_recompressed_q85.jpg"}) {
        const TileComparison t = compare_tiles(g0, compute_tiles(sample(f)));
        check(t.changed == 0, std::string(f) + ": no tile visibly changed");
    }

    const TileComparison other = compare_tiles(g0, compute_tiles(sample("09_unrelated.png")));
    check(other.changed == 64, "09 unrelated: all 64 tiles changed");
}

static void full_comparison() {
    std::cout << "compare_images on samples/\n";
    const Image ref = sample("00_original.png");

    auto cmp = [&](const std::string& f) { return compare_images(ref, sample(f)); };

    check(cmp("01_identical_pixels.png").verdict == Verdict::Identical, "01 -> IDENTICAL");

    for (const char* f : {"02_one_pixel.png", "03_small_region_edit.png",
                          "04_brightness_plus5.png", "05_resized_half.png",
                          "06_recompressed_q85.jpg", "07_cropped_5pct.png"}) {
        const Comparison c = cmp(f);
        check(c.verdict == Verdict::Similar && c.orientation == Orientation::Identity,
              std::string(f) + " -> SIMILAR, not turned (pHash " +
                  std::to_string(c.phash_distance) + ")");
    }

    const Comparison m = cmp("08_mirrored.png");
    check(m.verdict == Verdict::Similar && m.orientation == Orientation::FlipH &&
              m.phash_distance == 0 && m.tiles.changed == 0,
          "08 -> SIMILAR, mirrored left-right, no tile changed");

    const Comparison r = cmp("10_rotated_90.png");
    check(r.verdict == Verdict::Similar && r.orientation == Orientation::Rot90 &&
              r.phash_distance == 0 && r.tiles.changed == 0,
          "10 -> SIMILAR, rotated 90, no tile changed");

    check(cmp("09_unrelated.png").verdict == Verdict::Different, "09 unrelated -> DIFFERENT");

    // Known limitation, documented in samples/README.md: arbitrary angles
    // are not handled yet. This records the current behaviour.
    check(cmp("11_rotated_5deg.png").verdict == Verdict::Different,
          "11 rotated 5 degrees -> DIFFERENT (known limitation)");
}

int main() {
    orientations();
    tiles();
    full_comparison();

    if (failures == 0) {
        std::cout << "\nAll tests passed.\n";
        return 0;
    }
    std::cout << '\n' << failures << " test(s) FAILED.\n";
    return 1;
}
