// Usage:
//   ivc_compare <reference> <image> [<image> ...]
//
// Compares each image with the reference and prints:
//   similarity  SSIM of 32x32 thumbnails (1 = same composition), after
//               turning the image back to the best of the 8 orientations
//   verdict     IDENTICAL / SIMILAR / DIFFERENT, plus the orientation
//
// For where two images differ, use ivc_diff.

#include <exception>
#include <iomanip>
#include <iostream>

#include "compare.hpp"
#include "image.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <reference> <image> [<image> ...]\n";
        return 1;
    }

    Image reference;
    try {
        reference = load_image(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    std::cout << "Reference: " << argv[1] << "\n\n"
              << std::left << std::setw(36) << "image" << std::right << std::setw(12)
              << "similarity" << "   verdict\n";

    int status = 0;
    for (int i = 2; i < argc; ++i) {
        try {
            const Comparison c = compare_images(reference, load_image(argv[i]));
            std::cout << std::left << std::setw(36) << argv[i] << std::right << std::setw(12)
                      << std::fixed << std::setprecision(3) << c.similarity << "   "
                      << verdict_name(c.verdict);
            if (c.verdict == Verdict::Similar && c.orientation != Orientation::Identity)
                std::cout << " (" << orientation_name(c.orientation) << ")";
            std::cout << '\n';
        } catch (const std::exception& e) {
            std::cerr << "Error: " << argv[i] << ": " << e.what() << '\n';
            status = 1;
        }
    }
    return status;
}
