// Usage:
//   ivc_compare <reference> <image> [<image> ...]
//   ivc_compare --map <reference> <image> [<image> ...]
//
// Compares each image with the reference and prints:
//   pHash / dHash  Hamming distance, in the best of the 8 orientations
//   tiles          how many of the 8x8 tiles visibly changed
//   verdict        IDENTICAL / SIMILAR / DIFFERENT, plus the orientation
//
// With --map, also draws the tile grid:
//   .  unchanged (identical pixels)
//   ~  pixels differ but it looks the same (noise, JPEG, resize)
//   X  visibly changed

#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>

#include "compare.hpp"
#include "image.hpp"

static void print_map(const TileComparison& t) {
    for (int r = 0; r < t.rows; ++r) {
        std::cout << "      ";
        for (int c = 0; c < t.cols; ++c) {
            const TileState s = t.state[static_cast<std::size_t>(r) * t.cols + c];
            std::cout << (s == TileState::Unchanged ? '.' : s == TileState::Noise ? '~' : 'X')
                      << ' ';
        }
        std::cout << '\n';
    }
}

int main(int argc, char** argv) {
    int first = 1;
    bool map = false;
    if (argc > 1 && std::strcmp(argv[1], "--map") == 0) {
        map = true;
        first = 2;
    }
    if (argc - first < 2) {
        std::cerr << "Usage: " << argv[0] << " [--map] <reference> <image> [<image> ...]\n";
        return 1;
    }

    Image reference;
    try {
        reference = load_image(argv[first]);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    std::cout << "Reference: " << argv[first] << "\n\n"
              << std::left << std::setw(36) << "image" << std::right << std::setw(7)
              << "pHash" << std::setw(7) << "dHash" << std::setw(9) << "tiles"
              << "   verdict\n";

    int status = 0;
    for (int i = first + 1; i < argc; ++i) {
        try {
            const Comparison c = compare_images(reference, load_image(argv[i]));
            const std::string tiles = std::to_string(c.tiles.changed) + "/" +
                                      std::to_string(c.tiles.cols * c.tiles.rows);

            std::cout << std::left << std::setw(36) << argv[i] << std::right
                      << std::setw(7) << c.phash_distance << std::setw(7)
                      << c.dhash_distance << std::setw(9) << tiles << "   "
                      << verdict_name(c.verdict);
            if (c.verdict == Verdict::Similar && c.orientation != Orientation::Identity)
                std::cout << " (" << orientation_name(c.orientation) << ")";
            std::cout << '\n';

            if (map) print_map(c.tiles);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << argv[i] << ": " << e.what() << '\n';
            status = 1;
        }
    }
    return status;
}
