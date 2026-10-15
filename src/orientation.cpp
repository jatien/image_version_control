#include "orientation.hpp"

#include <cstring>

const char* orientation_name(Orientation o) {
    switch (o) {
        case Orientation::Identity:      return "as is";
        case Orientation::Rot90:         return "rotated 90";
        case Orientation::Rot180:        return "rotated 180";
        case Orientation::Rot270:        return "rotated 270";
        case Orientation::FlipH:         return "mirrored left-right";
        case Orientation::FlipV:         return "mirrored top-bottom";
        case Orientation::Transpose:     return "mirrored on diagonal";
        case Orientation::AntiTranspose: return "mirrored on anti-diagonal";
    }
    return "?";
}

Orientation inverse(Orientation o) {
    if (o == Orientation::Rot90) return Orientation::Rot270;
    if (o == Orientation::Rot270) return Orientation::Rot90;
    return o;
}

Image apply_orientation(const Image& img, Orientation o) {
    const int w = img.width, h = img.height, c = img.channels;
    const bool swap = o == Orientation::Rot90 || o == Orientation::Rot270 ||
                      o == Orientation::Transpose || o == Orientation::AntiTranspose;

    Image out;
    out.width = swap ? h : w;
    out.height = swap ? w : h;
    out.channels = c;
    out.pixels.resize(img.pixels.size());

    // For every output pixel (x, y), find which source pixel (sx, sy) lands there.
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            int sx = x, sy = y;
            switch (o) {
                case Orientation::Identity:                                  break;
                case Orientation::Rot90:         sx = w - 1 - y; sy = x;         break;
                case Orientation::Rot180:        sx = w - 1 - x; sy = h - 1 - y; break;
                case Orientation::Rot270:        sx = y;         sy = h - 1 - x; break;
                case Orientation::FlipH:         sx = w - 1 - x;                 break;
                case Orientation::FlipV:                         sy = h - 1 - y; break;
                case Orientation::Transpose:     sx = y;         sy = x;         break;
                case Orientation::AntiTranspose: sx = w - 1 - y; sy = h - 1 - x; break;
            }
            std::memcpy(&out.pixels[out.index(x, y)], &img.pixels[img.index(sx, sy)], c);
        }
    }
    return out;
}
