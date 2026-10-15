// Tests for edit operations: parsing, validation, replay, regions, and the
// check of declared operations against the pixels.
// Run with:   ctest --test-dir build --output-on-failure

#include <iostream>
#include <string>

#include "image.hpp"
#include "image_hash.hpp"
#include "operations.hpp"
#include "orientation.hpp"

static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << '\n';
    if (!ok) ++failures;
}

template <typename F>
static void check_throws(F f, const std::string& what) {
    bool threw = false;
    try {
        f();
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, what);
}

static const std::string dir = SAMPLES_DIR;
static Image sample(const std::string& f) { return load_image(dir + "/" + f); }

static std::vector<Operation> ops(std::initializer_list<const char*> texts) {
    std::vector<Operation> out;
    for (const char* t : texts) out.push_back(parse_operation(t, "photo.png"));
    return out;
}

static void parsing() {
    std::cout << "Parsing and identity\n";
    const Operation op = parse_operation("filter region=1,2,3,4 name=\"soft glow\" file=\"my photo.png\"");
    check(op.action == "filter" && op.file == "my photo.png" && op.params.size() == 2 &&
              op.params.at("name") == "soft glow",
          "action, quoted values and file= are read");
    check(op.canonical() == "filter name=\"soft glow\" region=1,2,3,4",
          "canonical form sorts keys and quotes values with spaces");

    const Operation same = parse_operation("filter name=\"soft glow\" region=1,2,3,4 file=\"my photo.png\"");
    check(op.id() == same.id() && op.id().size() == 64, "parameter order does not change the ID");
    Operation other = same;
    other.tool = "GIMP 2.10";
    check(other.id() != op.id(), "the tool is part of the ID");

    check(parse_operation("rotate degrees=90").file.empty(), "file= may be omitted");
    check_throws([] { parse_operation("rotate 90"); }, "a parameter without '=' is rejected");
    check_throws([] { parse_operation("rotate degrees=90 degrees=180"); }, "a repeated parameter is rejected");
    check_throws([] { parse_operation("filter name=\"open"); }, "an unclosed quote is rejected");
}

static void validation() {
    std::cout << "Validation\n";
    check_throws([] { validate_operation(parse_operation("rotate degrees=45")); }, "rotate 45 rejected");
    check_throws([] { validate_operation(parse_operation("flip axis=diagonal")); }, "flip diagonal rejected");
    check_throws([] { validate_operation(parse_operation("crop x=0 y=0 width=300 height=10"), 256, 256); },
                 "crop outside the image rejected");
    check_throws([] { validate_operation(parse_operation("brightness delta=999")); }, "brightness 999 rejected");
    check_throws([] { validate_operation(parse_operation("filter")); }, "filter without name= rejected");
    check_throws([] { validate_operation(parse_operation("drawing region=1,2,3")); }, "malformed region rejected");
    validate_operation(parse_operation("my_plugin strength=3"));
    check(true, "unknown actions are allowed");
    check(find_action("rotate")->c2pa == "c2pa.orientation" && find_action("ai_edit") &&
              !find_action("ai_edit")->replayable,
          "registry: C2PA names, AI actions declared only");
}

static void replay_and_regions() {
    std::cout << "Replay and regions\n";
    const Image o = sample("00_original.png");
    check(exact_hash(replay_operation(o, parse_operation("rotate degrees=90"))) ==
              exact_hash(sample("10_rotated_90.png")),
          "rotate 90 reproduces 10_rotated_90 exactly");
    check(exact_hash(replay_operation(o, parse_operation("flip axis=horizontal"))) ==
              exact_hash(sample("08_mirrored.png")),
          "flip horizontal reproduces 08_mirrored exactly");
    check(exact_hash(replay_operation(o, parse_operation("brightness delta=5"))) ==
              exact_hash(sample("04_brightness_plus5.png")),
          "brightness +5 reproduces 04 exactly");
    check(exact_hash(replay_operation(o, parse_operation("crop x=12 y=12 width=232 height=232"))) ==
              exact_hash(sample("07_cropped_5pct.png")),
          "crop reproduces 07 exactly");
    check_throws([&] { replay_operation(o, parse_operation("filter name=blur")); },
                 "a declared-only action cannot be replayed");

    // A box through each geometric operation, on a 256 x 128 image.
    const Box b{10, 20, 30, 40};
    auto t = [&](const char* op) { return *transform_box(b, parse_operation(op), 256, 128); };
    const Box r90 = t("rotate degrees=90"), r180 = t("rotate degrees=180"), r270 = t("rotate degrees=270");
    check(r90.x == 20 && r90.y == 216 && r90.width == 40 && r90.height == 30, "box through rotate 90");
    check(r180.x == 216 && r180.y == 68 && r180.width == 30, "box through rotate 180");
    check(r270.x == 68 && r270.y == 10 && r270.width == 40 && r270.height == 30, "box through rotate 270");
    const Box c = t("crop x=20 y=0 width=100 height=100");
    check(c.x == 0 && c.y == 20 && c.width == 20 && c.height == 40, "box through crop is clipped and shifted");
    check(!transform_box(b, parse_operation("crop x=100 y=100 width=10 height=10"), 256, 128),
          "box cropped away disappears");

    // The rotated box must cover exactly where the pixels went.
    Image img;
    img.width = 256; img.height = 128; img.channels = 1;
    img.pixels.assign(256 * 128, 0);
    for (int y = b.y; y < b.y + b.height; ++y)
        for (int x = b.x; x < b.x + b.width; ++x) img.pixels[img.index(x, y)] = 255;
    const Image rot = apply_orientation(img, Orientation::Rot90);
    bool exact = true;
    for (int y = 0; y < rot.height; ++y)
        for (int x = 0; x < rot.width; ++x) {
            const bool in = x >= r90.x && x < r90.x + r90.width && y >= r90.y && y < r90.y + r90.height;
            exact = exact && ((rot.at(x, y, 0) == 255) == in);
        }
    check(exact, "rotated box matches the rotated pixels exactly");
}

static void checks() {
    std::cout << "Declared operations vs pixels\n";
    const Image o = sample("00_original.png");
    const Image win = sample("03_small_region_edit.png");
    auto result = [&](const Image& before, const Image& after, std::vector<Operation> list) {
        return check_operations(before, after, list).result;
    };

    check(result(o, sample("10_rotated_90.png"), ops({"rotate degrees=90"})) == CheckResult::Verified,
          "honest rotation -> VERIFIED");
    check(result(o, sample("05_resized_half.png"), ops({"resize width=128 height=128"})) ==
              CheckResult::Consistent,
          "resize by another program -> CONSISTENT");
    check(result(o, win, ops({"drawing region=164,160,16,16"})) == CheckResult::Declared,
          "drawing inside its declared region -> DECLARED");
    check(result(o, win, {}) == CheckResult::Undeclared, "nothing declared -> UNDECLARED");

    const OperationCheck lie = check_operations(o, win, ops({"brightness delta=0"}));
    check(lie.result == CheckResult::Mismatch && lie.details.size() == 1,
          "false claim -> MISMATCH, with the undeclared region");
    check(result(o, win, ops({"drawing region=20,20,30,30"})) == CheckResult::Mismatch,
          "region declared in the wrong place -> MISMATCH");
    check(result(o, sample("10_rotated_90.png"), ops({"rotate degrees=270"})) == CheckResult::Mismatch,
          "wrong rotation angle -> MISMATCH");
    check(result(o, sample("10_rotated_90.png"), ops({"drawing region=0,0,8,8"})) == CheckResult::Mismatch,
          "rotation hidden behind another operation -> MISMATCH");
    check(result(o, sample("05_resized_half.png"), ops({"brightness delta=0"})) == CheckResult::Mismatch,
          "undeclared resize -> MISMATCH (size)");
    check(result(o, apply_orientation(win, Orientation::Rot90),
                 ops({"drawing region=164,160,16,16", "rotate degrees=90"})) == CheckResult::Declared,
          "region carried through a later rotation -> DECLARED");
    check(result(o, win, ops({"ai_edit model=inpaint"})) == CheckResult::Declared,
          "AI edit without region covers the whole image -> DECLARED");
}

int main() {
    parsing();
    validation();
    replay_and_regions();
    checks();

    if (failures == 0) {
        std::cout << "\nAll tests passed.\n";
        return 0;
    }
    std::cout << '\n' << failures << " test(s) FAILED.\n";
    return 1;
}
