#pragma once

#include "image.hpp"
#include "orientation.hpp"

// Is an image a version of a reference, and how is it turned?
//
//   1. exact hash (SHA-256 of the pixels) equal -> IDENTICAL, stop
//   2. both are shrunk to 32 x 32 grayscale thumbnails and compared with
//      SSIM (interior mean, as scikit-image reports it), trying the image in
//      all 8 orientations (rotations by 90 degrees and mirrors); the highest
//      score wins
//   3. score >= kSimilarThreshold -> SIMILAR, otherwise DIFFERENT
//
// The thumbnails make the score ignore size, compression and small edits,
// and keep only the overall composition. Where exactly the images differ is
// diff_images()'s job (diff.hpp).
//
// Pure function: no state, nothing stored. The version tracker will call it.

enum class Verdict { Identical, Similar, Different };

// Thumbnail side used for the verdict.
constexpr int kThumbnailSize = 32;

// Thumbnail SSIM at or above which an image counts as a version of the
// reference. On samples/, every edit scores 0.85 or more (lowest: the 5%
// crop, 0.851; the 5-degree rotation scores 0.868); the unrelated image
// scores at most 0.52 over all 8 orientations.
constexpr double kSimilarThreshold = 0.70;

struct Comparison {
    Verdict verdict = Verdict::Different;
    // How the image is turned relative to the reference: image is (about)
    // apply_orientation(reference, orientation).
    Orientation orientation = Orientation::Identity;
    double similarity = 0.0;  // thumbnail SSIM after turning the image back, -1..1
};

// With search_orientations = false, only the image as it is is compared
// (used when checking declared edits, where an undeclared rotation must not
// be silently turned back).
Comparison compare_images(const Image& reference, const Image& image,
                          bool search_orientations = true);

const char* verdict_name(Verdict v);
