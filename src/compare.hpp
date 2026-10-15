#pragma once

#include "image.hpp"
#include "orientation.hpp"
#include "tile_hash.hpp"

// Full comparison of an image against a reference, combining every hash:
//
//   1. exact hash      equal -> IDENTICAL, stop
//   2. pHash, tried in all 8 orientations of the image; the closest one wins,
//      so 90/180/270-degree rotations and mirrors are recognised
//   3. tile grid of the reference vs the image in that orientation, to say
//      WHERE it changed
//
// Pure function: no state, nothing stored. The version tracker will call it.

enum class Verdict { Identical, Similar, Different };

// pHash distance (0..64) at or below which an image counts as a version of
// the reference. Unrelated images score around 32.
constexpr int kSimilarThreshold = 10;

struct Comparison {
    Verdict verdict = Verdict::Different;
    // How the image is turned relative to the reference: image is (about)
    // apply_orientation(reference, orientation).
    Orientation orientation = Orientation::Identity;
    int phash_distance = 0;     // after turning the image back
    int dhash_distance = 0;     // after turning the image back
    TileComparison tiles;       // reference vs the image turned back
};

Comparison compare_images(const Image& reference, const Image& image);

const char* verdict_name(Verdict v);
