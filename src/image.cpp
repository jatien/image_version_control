#include "image.hpp"

#include <stdexcept>

#include "stb_image.h"

Image load_image(const std::string& path, int desired_channels) {
    if (desired_channels < 0 || desired_channels > 4) {
        throw std::invalid_argument("desired_channels must be between 0 and 4");
    }

    int w = 0, h = 0, file_channels = 0;
    unsigned char* data =
        stbi_load(path.c_str(), &w, &h, &file_channels, desired_channels);

    if (data == nullptr) {
        throw std::runtime_error("Could not load '" + path + "': " +
                                 stbi_failure_reason());
    }

    Image img;
    img.width = w;
    img.height = h;
    img.channels = desired_channels != 0 ? desired_channels : file_channels;

    const std::size_t n = static_cast<std::size_t>(w) * h * img.channels;
    img.pixels.assign(data, data + n);  // copy into memory we own

    stbi_image_free(data);              // free stb's buffer
    return img;
}
