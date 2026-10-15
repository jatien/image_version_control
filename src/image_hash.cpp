#include "image_hash.hpp"

namespace {

void update_be32(Sha256& h, uint32_t v) {
    const uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8),
                          uint8_t(v)};
    h.update(b, 4);
}

}  // namespace

Sha256::Digest exact_hash(const Image& img) {
    Sha256 h;
    h.update("IVC-EXACT-v1");
    update_be32(h, static_cast<uint32_t>(img.width));
    update_be32(h, static_cast<uint32_t>(img.height));
    update_be32(h, static_cast<uint32_t>(img.channels));
    h.update(img.pixels.data(), img.pixels.size());
    return h.finalize();
}
