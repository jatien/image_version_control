#include "sha256.hpp"

#include <cstring>

namespace {

// Round constants: first 32 bits of the fractional parts of the cube roots
// of the first 64 primes (FIPS 180-4, section 4.2.2).
constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

// Initial hash value: first 32 bits of the fractional parts of the square
// roots of the first 8 primes (section 5.3.3).
constexpr uint32_t H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

// The six logical functions of section 4.1.2.
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t big_sigma0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
inline uint32_t big_sigma1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
inline uint32_t small_sigma0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
inline uint32_t small_sigma1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

// SHA-256 is defined on big-endian words, whatever the CPU's byte order.
inline uint32_t load_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

inline void store_be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

}  // namespace

Sha256::Sha256() { reset(); }

void Sha256::reset() {
    std::memcpy(state_.data(), H0, sizeof(H0));
    buffered_ = 0;
    total_bytes_ = 0;
}

void Sha256::compress(const uint8_t* block) {
    // 1. Message schedule: expand 16 words into 64.
    uint32_t w[64];
    for (int t = 0; t < 16; ++t) w[t] = load_be32(block + 4 * t);
    for (int t = 16; t < 64; ++t)
        w[t] = small_sigma1(w[t - 2]) + w[t - 7] + small_sigma0(w[t - 15]) + w[t - 16];

    // 2. Working variables start from the current hash value.
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    // 3. 64 rounds (all additions are mod 2^32, which uint32_t does for free).
    for (int t = 0; t < 64; ++t) {
        uint32_t t1 = h + big_sigma1(e) + ch(e, f, g) + K[t] + w[t];
        uint32_t t2 = big_sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    // 4. Add the result into the running hash value.
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(const uint8_t* data, std::size_t len) {
    total_bytes_ += len;

    // Top up a partially filled block first.
    if (buffered_ > 0) {
        std::size_t take = 64 - buffered_;
        if (take > len) take = len;
        std::memcpy(buffer_.data() + buffered_, data, take);
        buffered_ += take;
        data += take;
        len -= take;
        if (buffered_ < 64) return;
        compress(buffer_.data());
        buffered_ = 0;
    }

    // Whole blocks straight from the input, no copy.
    while (len >= 64) {
        compress(data);
        data += 64;
        len -= 64;
    }

    // Keep the rest for later.
    if (len > 0) {
        std::memcpy(buffer_.data(), data, len);
        buffered_ = len;
    }
}

void Sha256::update(const std::string& s) {
    update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

Sha256::Digest Sha256::finalize() {
    // Padding (section 5.1.1): a 1 bit, zeros, then the message length in
    // bits as a 64-bit big-endian number, so the total is a multiple of 512.
    const uint64_t bit_len = total_bytes_ * 8;

    buffer_[buffered_++] = 0x80;
    if (buffered_ > 56) {  // no room for the length: finish this block
        std::memset(buffer_.data() + buffered_, 0, 64 - buffered_);
        compress(buffer_.data());
        buffered_ = 0;
    }
    std::memset(buffer_.data() + buffered_, 0, 56 - buffered_);
    for (int i = 0; i < 8; ++i)
        buffer_[56 + i] = uint8_t(bit_len >> (56 - 8 * i));
    compress(buffer_.data());

    Digest out;
    for (int i = 0; i < 8; ++i) store_be32(out.data() + 4 * i, state_[i]);

    reset();
    return out;
}

Sha256::Digest Sha256::hash(const uint8_t* data, std::size_t len) {
    Sha256 h;
    h.update(data, len);
    return h.finalize();
}

Sha256::Digest Sha256::hash(const std::string& s) {
    Sha256 h;
    h.update(s);
    return h.finalize();
}

std::string to_hex(const Sha256::Digest& d) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (uint8_t byte : d) {
        s += digits[byte >> 4];
        s += digits[byte & 0x0f];
    }
    return s;
}
