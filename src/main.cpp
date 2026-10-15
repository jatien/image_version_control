// Usage:
//   ivc_pixels <image>          print size, layout and the first pixels
//   ivc_pixels <image> --dump   print every pixel as "x y R G B A"

#include <cstring>
#include <exception>
#include <iostream>

#include "image.hpp"

static void print_pixel(const Image& img, int x, int y) {
    std::cout << x << ' ' << y;
    for (int c = 0; c < img.channels; ++c) {
        std::cout << ' ' << static_cast<int>(img.at(x, y, c));
    }
    std::cout << '\n';
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image> [--dump]\n";
        return 1;
    }
    const bool dump = argc >= 3 && std::strcmp(argv[2], "--dump") == 0;

    try {
        Image img = load_image(argv[1]);  // always RGBA

        if (dump) {
            for (int y = 0; y < img.height; ++y)
                for (int x = 0; x < img.width; ++x) print_pixel(img, x, y);
            return 0;
        }

        std::cout << "File:     " << argv[1] << '\n'
                  << "Size:     " << img.width << " x " << img.height << '\n'
                  << "Channels: " << img.channels << " (RGBA)\n"
                  << "Bytes:    " << img.size_bytes() << '\n'
                  << "First pixels (x y R G B A):\n";

        const int shown = img.width < 5 ? img.width : 5;
        for (int x = 0; x < shown; ++x) print_pixel(img, x, 0);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
