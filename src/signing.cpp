#ifdef _WIN32
#define _CRT_RAND_S  // enables rand_s, the secure random generator
#endif
#include "signing.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

extern "C" {
#include "monocypher.h"
#include "monocypher-ed25519.h"
}

namespace fs = std::filesystem;

std::string bytes_to_hex(const uint8_t* data, std::size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(2 * n);
    for (std::size_t i = 0; i < n; ++i) {
        s += digits[data[i] >> 4];
        s += digits[data[i] & 0xf];
    }
    return s;
}

void hex_to_bytes(const std::string& hex, uint8_t* out, std::size_t n) {
    if (hex.size() != 2 * n) throw std::invalid_argument("expected " + std::to_string(2 * n) + " hex digits");
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::invalid_argument("not a hex digit");
    };
    for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(val(hex[2 * i]) << 4 | val(hex[2 * i + 1]));
}

std::string SigningKey::public_hex() const { return bytes_to_hex(public_key.data(), 32); }

fs::path key_directory() {
    if (const char* h = std::getenv("IVC_HOME"); h && *h) return h;
#ifdef _WIN32
    if (const char* a = std::getenv("APPDATA"); a && *a) return fs::path(a) / "ivc";
#endif
    if (const char* home = std::getenv("HOME"); home && *home)
        return fs::path(home) / ".config" / "ivc";
    throw std::runtime_error("cannot find a home folder for the signing key; set IVC_HOME");
}

SigningKey key_from_seed(const std::array<uint8_t, 32>& seed, const std::string& name) {
    SigningKey k;
    k.name = name;
    std::array<uint8_t, 32> s = seed;  // crypto_ed25519_key_pair wipes its seed argument
    crypto_ed25519_key_pair(k.secret.data(), k.public_key.data(), s.data());
    return k;
}

namespace {

std::array<uint8_t, 32> random_seed() {
    std::array<uint8_t, 32> seed{};
#ifdef _WIN32
    for (std::size_t i = 0; i < seed.size(); i += sizeof(unsigned int)) {
        unsigned int r = 0;
        if (rand_s(&r) != 0) throw std::runtime_error("no secure random numbers available");
        for (std::size_t j = 0; j < sizeof r && i + j < seed.size(); ++j) seed[i + j] = uint8_t(r >> (8 * j));
    }
#else
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (!urandom.read(reinterpret_cast<char*>(seed.data()), seed.size()))
        throw std::runtime_error("cannot read /dev/urandom");
#endif
    return seed;
}

fs::path key_file() { return key_directory() / "signing_key"; }

}  // namespace

SigningKey generate_signing_key(const std::string& name, bool overwrite) {
    if (name.empty() || name.find('\n') != std::string::npos)
        throw std::invalid_argument("the key needs a one-line name");
    const fs::path path = key_file();
    if (fs::exists(path) && !overwrite)
        throw std::runtime_error("a signing key already exists at " + path.string());

    std::array<uint8_t, 32> seed = random_seed();
    const SigningKey key = key_from_seed(seed, name);

    fs::create_directories(path.parent_path());
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
        out << "ivc-signing-key 1\nseed " << bytes_to_hex(seed.data(), 32) << "\nname " << name << '\n';
    }
    // Only the owner may read the private key.
    fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
    fs::rename(tmp, path);
    crypto_wipe(seed.data(), seed.size());
    return key;
}

std::optional<SigningKey> load_signing_key() {
    const fs::path path = key_file();
    if (!fs::exists(path)) return std::nullopt;
    std::ifstream in(path);
    std::string line, seed_hex, name;
    if (!std::getline(in, line) || line != "ivc-signing-key 1")
        throw std::runtime_error(path.string() + " is not an ivc signing key");
    while (std::getline(in, line)) {
        if (line.rfind("seed ", 0) == 0) seed_hex = line.substr(5);
        else if (line.rfind("name ", 0) == 0) name = line.substr(5);
    }
    std::array<uint8_t, 32> seed{};
    hex_to_bytes(seed_hex, seed.data(), 32);
    SigningKey key = key_from_seed(seed, name);
    crypto_wipe(seed.data(), seed.size());
    return key;
}

std::string sign_message(const SigningKey& key, const std::string& message) {
    uint8_t sig[64];
    crypto_ed25519_sign(sig, key.secret.data(), reinterpret_cast<const uint8_t*>(message.data()),
                        message.size());
    return bytes_to_hex(sig, 64);
}

bool verify_signature(const std::string& public_hex, const std::string& signature_hex,
                      const std::string& message) {
    uint8_t pub[32], sig[64];
    try {
        hex_to_bytes(public_hex, pub, 32);
        hex_to_bytes(signature_hex, sig, 64);
    } catch (const std::invalid_argument&) {
        return false;
    }
    return crypto_ed25519_check(sig, pub, reinterpret_cast<const uint8_t*>(message.data()),
                                message.size()) == 0;
}
