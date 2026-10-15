#include "compare.hpp"

#include "image_hash.hpp"
#include "similarity_hash.hpp"

const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::Identical: return "IDENTICAL";
        case Verdict::Similar:   return "SIMILAR";
        case Verdict::Different: return "DIFFERENT";
    }
    return "?";
}

Comparison compare_images(const Image& reference, const Image& image) {
    Comparison result;
    const TileGrid ref_tiles = compute_tiles(reference);

    if (exact_hash(reference) == exact_hash(image)) {
        result.verdict = Verdict::Identical;
        result.tiles = compare_tiles(ref_tiles, ref_tiles);
        return result;
    }

    // Turn the image back in every possible way; keep the one whose pHash is
    // closest to the reference. Identity is tried first, so it wins ties.
    const uint64_t ref_p = phash(reference);
    Image best = image;
    result.phash_distance = 65;
    for (Orientation undo : kAllOrientations) {
        Image turned = apply_orientation(image, undo);
        const int d = hamming_distance(ref_p, phash(turned));
        if (d < result.phash_distance) {
            result.phash_distance = d;
            result.orientation = inverse(undo);
            best = std::move(turned);
        }
    }

    result.dhash_distance = hamming_distance(dhash(reference), dhash(best));
    result.tiles = compare_tiles(ref_tiles, compute_tiles(best));
    result.verdict = result.phash_distance <= kSimilarThreshold ? Verdict::Similar
                                                                : Verdict::Different;
    return result;
}
