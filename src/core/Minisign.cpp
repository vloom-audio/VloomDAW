// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/Minisign.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <monocypher-ed25519.h>
#include <monocypher.h>
#include <vector>

namespace vloomdaw {

namespace {

constexpr std::string_view kUntrusted = "untrusted comment: ";
constexpr std::string_view kTrusted = "trusted comment: ";
constexpr std::size_t kMaxTrustedComment = 8192;

void fail(std::string* error, const char* why)
{
    if (error != nullptr)
        *error = why;
}

int base64Value(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

/// The text's lines, without "\n" or "\r\n".
std::vector<std::string_view> lines(std::string_view text)
{
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        out.push_back(line);
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
    return out;
}

const std::uint8_t* bytes(std::string_view s) { return reinterpret_cast<const std::uint8_t*>(s.data()); }

} // namespace

std::optional<std::string> decodeBase64(std::string_view text)
{
    if (text.size() % 4 != 0)
        return std::nullopt;
    std::string out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        int v[4];
        int pad = 0;
        for (int j = 0; j < 4; ++j) {
            const char c = text[i + static_cast<std::size_t>(j)];
            if (c == '=' && last && j >= 2) {
                v[j] = 0;
                ++pad;
                continue;
            }
            if (pad > 0) // data after padding
                return std::nullopt;
            v[j] = base64Value(c);
            if (v[j] < 0)
                return std::nullopt;
        }
        const unsigned triple = (static_cast<unsigned>(v[0]) << 18) | (static_cast<unsigned>(v[1]) << 12)
            | (static_cast<unsigned>(v[2]) << 6) | static_cast<unsigned>(v[3]);
        // Canonical only: the bits that padding drops must be zero.
        if ((pad == 1 && (triple & 0xFFu) != 0) || (pad == 2 && (triple & 0xFFFFu) != 0))
            return std::nullopt;
        out.push_back(static_cast<char>((triple >> 16) & 0xFFu));
        if (pad < 2)
            out.push_back(static_cast<char>((triple >> 8) & 0xFFu));
        if (pad < 1)
            out.push_back(static_cast<char>(triple & 0xFFu));
    }
    return out;
}

std::optional<MinisignKey> parseMinisignKey(std::string_view text)
{
    // The base64 line: the only line, or the one after the comment.
    std::string_view encoded;
    for (const std::string_view line : lines(text)) {
        if (line.empty() || line.substr(0, kUntrusted.size()) == kUntrusted)
            continue;
        if (!encoded.empty())
            return std::nullopt; // more than one key line
        encoded = line;
    }
    const auto raw = decodeBase64(encoded);
    if (!raw || raw->size() != 2 + 8 + 32 || raw->compare(0, 2, "Ed") != 0)
        return std::nullopt;
    MinisignKey key;
    std::memcpy(key.keyId.data(), raw->data() + 2, 8);
    std::memcpy(key.publicKey.data(), raw->data() + 10, 32);
    return key;
}

bool minisignVerify(
    const MinisignKey& key, std::string_view message, std::string_view signature, std::string* error)
{
    const auto parts = lines(signature);
    // Four lines; a trailing empty one is allowed.
    if (parts.size() < 4 || (parts.size() > 4 && !(parts.size() == 5 && parts[4].empty()))) {
        fail(error, "the signature file is not in minisign format");
        return false;
    }
    if (parts[0].substr(0, kUntrusted.size()) != kUntrusted
        || parts[2].substr(0, kTrusted.size()) != kTrusted) {
        fail(error, "the signature file is not in minisign format");
        return false;
    }
    const auto sig = decodeBase64(parts[1]);
    const auto global = decodeBase64(parts[3]);
    if (!sig || sig->size() != 2 + 8 + 64 || !global || global->size() != 64) {
        fail(error, "the signature file is not in minisign format");
        return false;
    }
    const std::string_view algorithm = std::string_view(*sig).substr(0, 2);
    if (algorithm != "Ed" && algorithm != "ED") {
        fail(error, "the signature uses an unknown algorithm");
        return false;
    }
    if (std::memcmp(sig->data() + 2, key.keyId.data(), key.keyId.size()) != 0) {
        fail(error, "the file is signed with another key");
        return false;
    }
    const auto* sigBytes = bytes(*sig) + 10;

    int bad = 0;
    if (algorithm == "ED") {
        std::uint8_t digest[64];
        crypto_blake2b(digest, sizeof digest, bytes(message), message.size());
        bad = crypto_ed25519_check(sigBytes, key.publicKey.data(), digest, sizeof digest);
    } else {
        bad = crypto_ed25519_check(sigBytes, key.publicKey.data(), bytes(message), message.size());
    }
    if (bad != 0) {
        fail(error, "the signature does not match the file");
        return false;
    }

    // The global signature binds the trusted comment to the signature.
    const std::string_view comment = parts[2].substr(kTrusted.size());
    if (comment.size() > kMaxTrustedComment) {
        fail(error, "the signature file is not in minisign format");
        return false;
    }
    std::string signedComment(reinterpret_cast<const char*>(sigBytes), 64);
    signedComment.append(comment);
    if (crypto_ed25519_check(bytes(*global), key.publicKey.data(), bytes(signedComment), signedComment.size())
        != 0) {
        fail(error, "the trusted comment of the signature was altered");
        return false;
    }
    return true;
}

} // namespace vloomdaw
