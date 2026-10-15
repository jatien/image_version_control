// Usage:
//   ivc_hash <image> [<image> ...]
//
// Prints the exact hash of each image's pixels, like sha256sum does for
// files:   <64 hex digits>  <path>

#include <exception>
#include <iostream>

#include "image.hpp"
#include "image_hash.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image> [<image> ...]\n";
        return 1;
    }

    int status = 0;
    for (int i = 1; i < argc; ++i) {
        try {
            Image img = load_image(argv[i]);
            std::cout << to_hex(exact_hash(img)) << "  " << argv[i] << '\n';
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << '\n';
            status = 1;
        }
    }
    return status;
}
