#pragma once

#include <array>

#include "image.hpp"

// The 8 ways to rotate or flip an image without losing or inventing pixels
// (the symmetries of a square, the dihedral group D4).
enum class Orientation {
    Identity,
    Rot90,          // 90 degrees counter-clockwise
    Rot180,
    Rot270,         // 270 degrees counter-clockwise (= 90 clockwise)
    FlipH,          // mirror left <-> right
    FlipV,          // mirror top <-> bottom
    Transpose,      // mirror across the main diagonal
    AntiTranspose,  // mirror across the other diagonal
};

constexpr std::array<Orientation, 8> kAllOrientations = {
    Orientation::Identity, Orientation::Rot90,    Orientation::Rot180,
    Orientation::Rot270,   Orientation::FlipH,    Orientation::FlipV,
    Orientation::Transpose, Orientation::AntiTranspose};

const char* orientation_name(Orientation o);

// The orientation that undoes o: apply_orientation(apply_orientation(img, o),
// inverse(o)) == img. Only the 90 and 270-degree rotations differ from
// their inverse; every flip undoes itself.
Orientation inverse(Orientation o);

// Returns the image rotated/flipped. 90/270-degree rotations and the two
// diagonal mirrors swap width and height.
Image apply_orientation(const Image& img, Orientation o);
