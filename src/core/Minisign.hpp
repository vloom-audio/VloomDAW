// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vloomdaw {

/// A minisign public key (https://jedisct1.github.io/minisign/): an Ed25519
/// key and the 8-byte id that signatures name.
struct MinisignKey {
    std::array<std::uint8_t, 8> keyId{};
    std::array<std::uint8_t, 32> publicKey{};
};

/// Reads a key: the whole minisign.pub file, or only its base64 line
/// ("RW..."). Nothing if it is not one.
[[nodiscard]] std::optional<MinisignKey> parseMinisignKey(std::string_view text);

/// Whether `signature` (the text of a .minisig file) is `key`'s signature of
/// `message`. Both the signature and the global signature over the trusted
/// comment must hold. Accepts the legacy ("Ed") and the pre-hashed ("ED",
/// minisign's default) algorithm. On false, `error` (if given) says why.
[[nodiscard]] bool minisignVerify(const MinisignKey& key, std::string_view message,
    std::string_view signature, std::string* error = nullptr);

/// Strict base64 (RFC 4648, with padding). Nothing for anything else.
[[nodiscard]] std::optional<std::string> decodeBase64(std::string_view text);

} // namespace vloomdaw
