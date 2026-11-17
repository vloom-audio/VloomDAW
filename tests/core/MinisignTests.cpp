// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Minisign.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace vloomdaw;

namespace {

// Made with an RFC 8032 reference Ed25519 in Python (not Monocypher), seed
// 00 01 .. 1f, key id 0123456789abcdef, in minisign's file format.
constexpr const char* kPublicKey = "untrusted comment: minisign public key 0123456789ABCDEF\n"
                                   "RWQBI0VniavN7wOhB7/zzhC+HXDdGOdLwJln5NYwm6UNXx3chmQSVTG4\n";

const std::string kMessage = "0f343b0931126a20f133d67c2b018a3b5e1d2c3a4f5b6c7d8e9f0a1b2c3d4e5f  "
                             "VloomDAW-0.2.0-windows-x64-setup.exe\n";

const std::string kTrusted = "trusted comment: timestamp:1763337600\tfile:SHA256SUMS.txt\thashed";

// Pre-hashed (BLAKE2b-512), minisign's default.
const std::string kHashedSignature
    = "untrusted comment: signature from minisign secret key\n"
      "RUQBI0VniavN75l5xaNgIQa/4Wx3NVPWYjFnBiTZaHxi/W6csxVfA93Y0GPMyKf7jI7aaSkshPnrMjcK6G8R"
      "HwpZb6lelf2gvAQ=\n"
    + kTrusted
    + "\n"
      "QFOsiAFCS4VDuv+JsVHQgXZ2OCKVOaxQaMkUKcEgC6RZrSKarumK2aDYQGa+9ptYCgxOilTEf4LpouOERHK8Dg==\n";

// Legacy: the message itself.
const std::string kLegacySignature
    = "untrusted comment: signature from minisign secret key\n"
      "RWQBI0VniavN7wcAZa4bHAhAJgNe97TH8NZxlbtYP0XAiM0ajbnoC2r7V0c9azCc4pFfd+d1k+FvJnowOO2e"
      "P0SQVbuA9Ar7GgA=\n"
    + kTrusted
    + "\n"
      "JeKWnU/QtmJOYRc9eSVBbc8Sz3kS7SmSiWLzcb/W5lyxRkqkBXSyQYq/tbFyuwobFWv+OvSjckAjjdWs0XzkCA==\n";

MinisignKey key()
{
    const auto k = parseMinisignKey(kPublicKey);
    REQUIRE(k);
    return *k;
}

std::string replaced(std::string text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

} // namespace

TEST_CASE("Base64 decodes only canonical, padded text", "[minisign]")
{
    CHECK(decodeBase64("") == std::string());
    CHECK(decodeBase64("TWFu") == std::string("Man"));
    CHECK(decodeBase64("TWE=") == std::string("Ma"));
    CHECK(decodeBase64("TQ==") == std::string("M"));
    CHECK_FALSE(decodeBase64("TQ")); // no padding
    CHECK_FALSE(decodeBase64("TR==")); // non-zero bits under the padding
    CHECK_FALSE(decodeBase64("T===")); // too much padding
    CHECK_FALSE(decodeBase64("TQ==TWFu")); // data after padding
    CHECK_FALSE(decodeBase64("TW-u")); // not the standard alphabet
}

TEST_CASE("A minisign public key is read from the file or its key line", "[minisign]")
{
    const MinisignKey fromFile = key();
    CHECK(fromFile.keyId[0] == 0x01);
    CHECK(fromFile.keyId[7] == 0xef);
    const auto fromLine = parseMinisignKey("RWQBI0VniavN7wOhB7/zzhC+HXDdGOdLwJln5NYwm6UNXx3chmQSVTG4");
    REQUIRE(fromLine);
    CHECK(fromLine->publicKey == fromFile.publicKey);

    CHECK_FALSE(parseMinisignKey(""));
    CHECK_FALSE(parseMinisignKey("untrusted comment: only a comment\n"));
    CHECK_FALSE(parseMinisignKey("RWQBI0VniavN7wOhB7/zzhC+HXDdGOdLwJln5NYwm6UNXx3chmQS")); // short
    CHECK_FALSE(parseMinisignKey(
        std::string(kPublicKey) + "RWQBI0VniavN7wOhB7/zzhC+HXDdGOdLwJln5NYwm6UNXx3chmQSVTG4\n"));
}

TEST_CASE("Minisign signatures from a reference implementation verify", "[minisign]")
{
    std::string error;
    CHECK(minisignVerify(key(), kMessage, kHashedSignature, &error));
    CHECK(error.empty());
    CHECK(minisignVerify(key(), kMessage, kLegacySignature, &error));

    // Windows line endings in the signature file.
    std::string crlf;
    for (const char c : kHashedSignature) {
        if (c == '\n')
            crlf += '\r';
        crlf += c;
    }
    CHECK(minisignVerify(key(), kMessage, crlf));
}

TEST_CASE("A changed file, comment or key is refused", "[minisign]")
{
    std::string error;
    // One hash changed in the checksums.
    CHECK_FALSE(minisignVerify(key(), replaced(kMessage, "0f343b", "1f343b"), kHashedSignature, &error));
    CHECK(error == "the signature does not match the file");
    CHECK_FALSE(minisignVerify(key(), kMessage + "\n", kLegacySignature));

    // The trusted comment is covered by the global signature.
    CHECK_FALSE(
        minisignVerify(key(), kMessage, replaced(kHashedSignature, "1763337600", "1763337601"), &error));
    CHECK(error == "the trusted comment of the signature was altered");

    // The algorithm bytes are part of what is read: "ED" read as "Ed" fails.
    CHECK_FALSE(minisignVerify(key(), kMessage, replaced(kHashedSignature, "RUQB", "RWQB")));

    // Another key id.
    MinisignKey other = key();
    other.keyId[0] ^= 1;
    CHECK_FALSE(minisignVerify(other, kMessage, kHashedSignature, &error));
    CHECK(error == "the file is signed with another key");

    // The same id with another public key.
    other = key();
    other.publicKey[5] ^= 1;
    CHECK_FALSE(minisignVerify(other, kMessage, kHashedSignature));
}

TEST_CASE("A malformed signature file is refused", "[minisign]")
{
    CHECK_FALSE(minisignVerify(key(), kMessage, ""));
    CHECK_FALSE(minisignVerify(key(), kMessage, "untrusted comment: x\nAAAA\n"));
    CHECK_FALSE(
        minisignVerify(key(), kMessage, replaced(kHashedSignature, "untrusted comment: ", "comment: ")));
    CHECK_FALSE(minisignVerify(
        key(), kMessage, replaced(kHashedSignature, "trusted comment: timestamp", "timestamp")));
    CHECK_FALSE(minisignVerify(key(), kMessage, kHashedSignature + "extra line\n"));
}
