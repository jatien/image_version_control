// Tests for Ed25519 signing (Monocypher) and key storage.
// Run with:   ctest --test-dir build --output-on-failure

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "signing.hpp"

namespace fs = std::filesystem;
static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << '\n';
    if (!ok) ++failures;
}

static std::array<uint8_t, 32> seed_from(const std::string& hex) {
    std::array<uint8_t, 32> s{};
    hex_to_bytes(hex, s.data(), 32);
    return s;
}

static void rfc8032() {
    std::cout << "RFC 8032 section 7.1 test vectors (also checked with Python cryptography)\n";
    const SigningKey k1 = key_from_seed(seed_from("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"));
    check(k1.public_hex() == "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
          "test 1: public key");
    const std::string sig1 = sign_message(k1, "");
    check(sig1 == "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
          "test 1: signature of the empty message");

    const SigningKey k2 = key_from_seed(seed_from("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb"));
    check(k2.public_hex() == "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
          "test 2: public key");
    check(sign_message(k2, std::string(1, '\x72')) ==
              "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
          "test 2: signature of the 1-byte message 0x72");

    check(verify_signature(k1.public_hex(), sig1, ""), "a valid signature verifies");
    check(!verify_signature(k1.public_hex(), sig1, "x"), "a different message fails");
    check(!verify_signature(k2.public_hex(), sig1, ""), "a different key fails");
    std::string flipped = sig1;
    flipped[10] = flipped[10] == '0' ? '1' : '0';
    check(!verify_signature(k1.public_hex(), flipped, ""), "a changed signature fails");
    check(!verify_signature("zz", sig1, "") && !verify_signature(k1.public_hex(), "abc", ""),
          "malformed hex is rejected, not a crash");
}

static void key_storage() {
    std::cout << "Key storage\n";
    const fs::path home = fs::path(BINARY_DIR) / "signing_test_home";
    fs::remove_all(home);
#ifdef _WIN32
    _putenv_s("IVC_HOME", home.string().c_str());
#else
    setenv("IVC_HOME", home.string().c_str(), 1);
#endif
    check(!load_signing_key(), "no key at first");
    const SigningKey k = generate_signing_key("Javi");
    const auto loaded = load_signing_key();
    check(loaded && loaded->public_hex() == k.public_hex() && loaded->name == "Javi",
          "a saved key loads back identically");
    check(verify_signature(k.public_hex(), sign_message(*loaded, "hello"), "hello"),
          "the loaded key signs verifiably");
    bool refused = false;
    try {
        generate_signing_key("Again");
    } catch (const std::exception&) {
        refused = true;
    }
    check(refused, "an existing key is never overwritten by accident");
#ifndef _WIN32
    const auto perms = fs::status(home / "signing_key").permissions();
    check((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none,
          "only the owner can read the key file");
#endif
    const SigningKey other = generate_signing_key("Javi", true);
    check(other.public_hex() != k.public_hex(), "two generated keys differ (random seed)");
}

int main() {
    rfc8032();
    key_storage();

    if (failures == 0) {
        std::cout << "\nAll tests passed.\n";
        return 0;
    }
    std::cout << '\n' << failures << " test(s) FAILED.\n";
    return 1;
}
