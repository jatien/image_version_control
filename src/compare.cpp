#include "compare.hpp"

#include "gray.hpp"
#include "image_hash.hpp"
#include "ssim.hpp"

const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::Identical: return "IDENTICAL";
        case Verdict::Similar:   return "SIMILAR";
        case Verdict::Different: return "DIFFERENT";
    }
    return "?";
}

static GrayImage thumbnail(const Image& img) {
    return resize_area(to_gray(img), kThumbnailSize, kThumbnailSize);
}

Comparison compare_images(const Image& reference, const Image& image) {
    Comparison result;

    if (exact_hash(reference) == exact_hash(image)) {
        result.verdict = Verdict::Identical;
        result.similarity = 1.0;
        return result;
    }

    // Turn the image back in every possible way; keep the one whose
    // thumbnail is most similar to the reference's. Identity is tried first,
    // so it wins ties.
    const GrayImage ref_thumb = thumbnail(reference);
    result.similarity = -2.0;
    for (Orientation undo : kAllOrientations) {
        const double s =
            ssim(ref_thumb, thumbnail(apply_orientation(image, undo))).interior_mean;
        if (s > result.similarity) {
            result.similarity = s;
            result.orientation = inverse(undo);
        }
    }

    result.verdict = result.similarity >= kSimilarThreshold ? Verdict::Similar
                                                            : Verdict::Different;
    return result;
}
