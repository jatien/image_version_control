// Tests for SHA-256 and the exact image hash.
// Run with:   ctest --test-dir build --output-on-failure
//        or:  ./build/test_hash

#include <iostream>
#include <string>
#include <vector>

#include "image.hpp"
#include "image_hash.hpp"
#include "sha256.hpp"

static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << '\n';
    if (!ok) ++failures;
}

static void check_eq(const std::string& got, const std::string& want,
                     const std::string& what) {
    check(got == want, what);
    if (got != want) std::cout << "        got  " << got << "\n        want " << want << '\n';
}

static void nist_vectors() {
    std::cout << "NIST FIPS 180-4 test vectors\n";
    check_eq(to_hex(Sha256::hash("")),
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
             "empty string");
    check_eq(to_hex(Sha256::hash("abc")),
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
             "\"abc\" (one block)");
    check_eq(to_hex(Sha256::hash(
                 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
             "448-bit message (two blocks)");
    check_eq(to_hex(Sha256::hash(std::string(1000000, 'a'))),
             "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
             "one million 'a'");
}

// Lengths around 55/56/64 bytes exercise both padding branches.
static void padding_edges() {
    std::cout << "Padding edge cases (reference: Python hashlib)\n";
    const std::vector<std::pair<int, std::string>> cases = {
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
        {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
        {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
    };
    for (const auto& [n, want] : cases)
        check_eq(to_hex(Sha256::hash(std::string(n, 'a'))), want,
                 std::to_string(n) + " x 'a'");
}

// Feeding data in pieces must give the same result as all at once.
static void streaming() {
    std::cout << "Streaming in chunks\n";
    std::string msg;
    for (int i = 0; i < 1000; ++i) msg += char(i * 31 + 7);
    const auto whole = Sha256::hash(msg);

    for (std::size_t chunk : {1, 3, 55, 63, 64, 65, 200}) {
        Sha256 h;
        for (std::size_t i = 0; i < msg.size(); i += chunk)
            h.update(msg.substr(i, chunk));
        check(h.finalize() == whole, "chunk size " + std::to_string(chunk));
    }

    Sha256 h;  // object is reusable after finalize()
    h.update("first message");
    h.finalize();
    h.update("abc");
    check(h.finalize() == Sha256::hash("abc"), "reuse after finalize()");
}

static void image_hashes() {
    std::cout << "Exact image hash on samples/\n";
    const std::string dir = SAMPLES_DIR;
    auto hash_of = [&](const std::string& f) { return exact_hash(load_image(dir + "/" + f)); };

    const auto original = hash_of("00_original.png");
    check(hash_of("01_identical_pixels.png") == original,
          "01 identical pixels, different metadata -> SAME hash");
    check(hash_of("02_one_pixel.png") != original, "02 one pixel +1 -> different hash");
    check(hash_of("03_small_region_edit.png") != original, "03 small edit -> different hash");
    check(hash_of("06_recompressed_q85.jpg") != original, "06 JPEG -> different hash");

    // Same pixel bytes, different shape: must not collide.
    Image a, b;
    a.width = 2; a.height = 8; a.channels = 1; a.pixels.assign(16, 7);
    b.width = 4; b.height = 4; b.channels = 1; b.pixels.assign(16, 7);
    check(exact_hash(a) != exact_hash(b), "2x8 vs 4x4 with same bytes -> different hash");
}

int main() {
    nist_vectors();
    padding_edges();
    streaming();
    image_hashes();

    if (failures == 0) {
        std::cout << "\nAll tests passed.\n";
        return 0;
    }
    std::cout << '\n' << failures << " test(s) FAILED.\n";
    return 1;
}
