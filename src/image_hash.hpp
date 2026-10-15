#pragma once

#include "image.hpp"
#include "sha256.hpp"

// Exact hash of an image's decoded pixels.
//
// Two images get the same exact hash if and only if (up to SHA-256
// collisions, which are infeasible to find) they have the same width,
// height, channel count and every pixel value. File format, compression and
// metadata do not matter.
//
// What is hashed, in order:
//   "IVC-EXACT-v1"  12-byte tag, so this hash can never be confused with a
//                   hash of something else (or a future v2 layout)
//   width           uint32, big-endian
//   height          uint32, big-endian
//   channels        uint32, big-endian
//   pixels          width * height * channels bytes, row-major, interleaved
//
// The dimensions are needed: a 2x8 and a 4x4 image can have exactly the same
// pixel bytes, but they are different images.
Sha256::Digest exact_hash(const Image& img);
