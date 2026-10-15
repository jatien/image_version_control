#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

// Ed25519 signatures (RFC 8032) for commits, using the vendored Monocypher
// library (third_party/monocypher, audited, public domain). The signature
// algorithm itself is deliberately NOT written here: unlike a hash, a home-made
// signature scheme can be insecure in ways no test catches.
//
// What a signature proves: the commit was made by whoever holds the private
// key, and not one byte of it changed since. It does NOT prove the declared
// edits are true; ivc checks those separately against the pixels.
//
// Keys live outside the repository, in $IVC_HOME or ~/.config/ivc:
//   signing_key   32-byte seed (hex) and a name; readable only by you
// Never share or commit that file. The public key (64 hex digits) is what
// others need to verify your commits.

struct SigningKey {
    std::array<uint8_t, 64> secret{};  // Ed25519 secret key (seed + public key)
    std::array<uint8_t, 32> public_key{};
    std::string name;

    std::string public_hex() const;
};

// Folder for the user's key: $IVC_HOME, or ~/.config/ivc (%APPDATA%\ivc on Windows).
std::filesystem::path key_directory();

// Creates a new key from the operating system's random generator and saves
// it. Throws if a key exists, unless overwrite.
SigningKey generate_signing_key(const std::string& name, bool overwrite = false);

// The saved key, if there is one.
std::optional<SigningKey> load_signing_key();

// Signature of `message`, 128 hex digits.
std::string sign_message(const SigningKey& key, const std::string& message);

// True when `signature_hex` is a valid signature of `message` by `public_hex`.
bool verify_signature(const std::string& public_hex, const std::string& signature_hex,
                      const std::string& message);

// Ed25519 key pair from a 32-byte seed (exposed for the RFC 8032 test vectors).
SigningKey key_from_seed(const std::array<uint8_t, 32>& seed, const std::string& name = "");

std::string bytes_to_hex(const uint8_t* data, std::size_t n);
// Throws std::invalid_argument unless `hex` has exactly 2 * n hex digits.
void hex_to_bytes(const std::string& hex, uint8_t* out, std::size_t n);
