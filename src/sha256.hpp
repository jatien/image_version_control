#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// SHA-256 as specified in FIPS 180-4, written from scratch.
//
// Data can be fed in pieces with update(); the result is the same as hashing
// everything at once:
//
//   Sha256 h;
//   h.update(part1, n1);
//   h.update(part2, n2);
//   Sha256::Digest d = h.finalize();   // 32 bytes
//
// After finalize() the object is reset and can hash a new message.
class Sha256 {
public:
    using Digest = std::array<uint8_t, 32>;

    Sha256();

    void update(const uint8_t* data, std::size_t len);
    void update(const std::string& s);

    Digest finalize();

    // One-shot helpers.
    static Digest hash(const uint8_t* data, std::size_t len);
    static Digest hash(const std::string& s);

private:
    void reset();
    void compress(const uint8_t* block);  // processes exactly 64 bytes

    std::array<uint32_t, 8> state_;   // H0..H7, the running hash value
    std::array<uint8_t, 64> buffer_;  // bytes waiting to fill a 64-byte block
    std::size_t buffered_ = 0;        // how many bytes are in buffer_
    uint64_t total_bytes_ = 0;        // message length so far
};

// Lowercase hexadecimal string, e.g. "ba7816bf8f01cfea...".
std::string to_hex(const Sha256::Digest& d);
