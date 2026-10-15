// Usage:
//   ivc_diff <reference> <image> [-o diff.png]
//
// Shows how <image> differs from <reference>:
//   - the SHA-256 ID of each image's pixels
//   - the verdict and orientation (IDENTICAL / SIMILAR / DIFFERENT)
//   - exact differences: how many pixels changed and where (same size only)
//   - visible differences: SSIM score and the changed regions
// With -o, writes a picture of the differences as PNG.

#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>

#include "diff.hpp"
#include "image.hpp"
#include "orientation.hpp"

static std::string box_text(const Box& b) {
    return "x " + std::to_string(b.x) + "-" + std::to_string(b.x + b.width - 1) + ", y " +
           std::to_string(b.y) + "-" + std::to_string(b.y + b.height - 1) + "  (" +
           std::to_string(b.width) + "x" + std::to_string(b.height) + ")";
}

int main(int argc, char** argv) {
    std::string out_path;
    std::string paths[2];
    int n = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (n < 2) {
            paths[n++] = argv[i];
        } else {
            n = 3;
        }
    }
    if (n != 2) {
        std::cerr << "Usage: " << argv[0] << " <reference> <image> [-o diff.png]\n";
        return 1;
    }

    try {
        const Image ref = load_image(paths[0]);
        const Image img = load_image(paths[1]);
        const ImageDiff d = diff_images(ref, img);
        const Comparison& c = d.comparison;

        std::cout << "reference  " << to_hex(d.reference_id) << "  " << paths[0] << '\n'
                  << "image      " << to_hex(d.image_id) << "  " << paths[1] << "\n\n";

        std::cout << "verdict    " << verdict_name(c.verdict);
        if (c.verdict == Verdict::Similar && c.orientation != Orientation::Identity)
            std::cout << " (" << orientation_name(c.orientation) << ")";
        std::cout << "   similarity " << std::fixed << std::setprecision(3) << c.similarity << '\n';

        if (c.verdict == Verdict::Identical) {
            std::cout << "\nSame pixels: no differences.\n";
        } else {
            if (c.verdict == Verdict::Different)
                std::cout << "\nWarning: the images do not look like versions of each other;\n"
                             "the differences below are not meaningful.\n";

            std::cout << "\nexact      ";
            if (!d.exact_available)
                std::cout << "sizes differ, compared at the smaller size\n";
            else if (d.exact_pixels == 0)
                std::cout << "no pixel differs\n";
            else
                std::cout << d.exact_pixels << " pixel(s) changed in " << box_text(d.exact_box)
                          << '\n';

            std::cout << "visible    mean SSIM " << std::fixed << std::setprecision(3)
                      << d.mean_ssim << ", ";
            if (d.regions.empty()) {
                std::cout << "no visible change\n";
            } else {
                std::cout << d.regions.size() << " changed region(s)\n";
                int k = 1;
                for (const Region& r : d.regions) {
                    std::cout << "  #" << k++ << "  " << box_text(r.box) << "  min SSIM "
                              << std::setprecision(2) << r.min_ssim << '\n';
                    if (k > 10 && d.regions.size() > 10) {
                        std::cout << "  ... and " << d.regions.size() - 10 << " more\n";
                        break;
                    }
                }
            }
        }

        if (!out_path.empty()) {
            write_png(out_path, render_diff(ref, d));
            std::cout << "\ndifference image written to " << out_path << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
