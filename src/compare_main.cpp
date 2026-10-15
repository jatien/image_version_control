// Usage:
//   ivc_compare <reference> <image> [<image> ...]
//
// Compares each image with the reference using the exact hash and both
// similarity hashes, and prints a verdict:
//   IDENTICAL  exact hashes equal: same pixels
//   SIMILAR    pHash distance <= kSimilarThreshold: probably a version of it
//   DIFFERENT  otherwise

#include <exception>
#include <iomanip>
#include <iostream>

#include "image.hpp"
#include "image_hash.hpp"
#include "similarity_hash.hpp"

// pHash distance (out of 64) at or below which two images count as similar.
// Calibrated on samples/: edits of the original score 0-8, the mirrored and
// unrelated images 32 (what two random 64-bit hashes differ by on average).
constexpr int kSimilarThreshold = 10;

struct Hashes {
    Sha256::Digest exact;
    uint64_t d;
    uint64_t p;
};

static Hashes hash_file(const std::string& path) {
    Image img = load_image(path);
    return {exact_hash(img), dhash(img), phash(img)};
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <reference> <image> [<image> ...]\n";
        return 1;
    }

    try {
        const Hashes ref = hash_file(argv[1]);
        std::cout << "Reference: " << argv[1] << "\n"
                  << "  dHash " << to_hex64(ref.d) << "   pHash " << to_hex64(ref.p)
                  << "\n\n"
                  << std::left << std::setw(36) << "image" << std::right
                  << std::setw(7) << "dHash" << std::setw(7) << "pHash"
                  << "   verdict\n";

        int status = 0;
        for (int i = 2; i < argc; ++i) {
            try {
                const Hashes h = hash_file(argv[i]);
                const int dd = hamming_distance(ref.d, h.d);
                const int dp = hamming_distance(ref.p, h.p);

                const char* verdict = h.exact == ref.exact ? "IDENTICAL"
                                      : dp <= kSimilarThreshold ? "SIMILAR"
                                                                : "DIFFERENT";

                std::cout << std::left << std::setw(36) << argv[i] << std::right
                          << std::setw(7) << dd << std::setw(7) << dp << "   "
                          << verdict << '\n';
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << '\n';
                status = 1;
            }
        }
        return status;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
