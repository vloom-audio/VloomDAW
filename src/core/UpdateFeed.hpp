// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/Version.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vloomdaw {

/// Reads the GitHub Releases list of VloomDAW and picks the update for this
/// computer. Pure logic: the app fetches the list (ui/update/UpdateService).
///
/// A release is offered only if it has, besides the installer for the
/// platform, SHA256SUMS.txt and its minisign signature
/// SHA256SUMS.txt.minisig: the app installs nothing it cannot check
/// (docs/releasing.md, "Signing the update").

enum class UpdatePlatform { Windows, MacOS, Linux };

/// The platform this build runs on.
[[nodiscard]] UpdatePlatform currentUpdatePlatform();

/// The repository the updates come from ("owner/name").
inline constexpr std::string_view kUpdateRepository = "vloom-audio/VloomDAW";
inline constexpr std::string_view kChecksumsFile = "SHA256SUMS.txt";
inline constexpr std::string_view kChecksumsSignatureFile = "SHA256SUMS.txt.minisig";

struct UpdateAsset {
    std::string name;
    std::string url; ///< https://github.com/<repository>/releases/download/...
    std::uint64_t size = 0;
};

struct UpdateRelease {
    Version version;
    std::string tag; ///< "v0.2.0"
    std::string title; ///< "VloomDAW v0.2.0"
    std::string notes; ///< Markdown, at most kMaxNotes bytes
    std::string pageUrl; ///< the release on github.com
    bool prerelease = false;
    UpdateAsset installer;
    UpdateAsset checksums;
    UpdateAsset signature;
};

inline constexpr std::size_t kMaxNotes = 64 * 1024;

/// The installer's file name for `version`, as release.yml names it:
/// VloomDAW-0.2.0-windows-x64-setup.exe and the like.
[[nodiscard]] std::string installerFileName(const Version& version, UpdatePlatform platform);

struct UpdateFeedResult {
    std::optional<UpdateRelease> release; ///< the newest update; none: up to date
    std::string error; ///< set when the list could not be read
};

/// The newest published release newer than `current` that has a checkable
/// installer for `platform`. Pre-releases only when `includePrereleases`.
/// `json` is the body of GET /repos/<repository>/releases.
[[nodiscard]] UpdateFeedResult newestUpdate(
    std::string_view json, const Version& current, bool includePrereleases, UpdatePlatform platform);

/// The SHA-256 (64 lowercase hex digits) that a sha256sum-style listing
/// gives for `fileName`. Nothing if it is not listed exactly once.
[[nodiscard]] std::optional<std::string> checksumFor(std::string_view sums, std::string_view fileName);

/// Whether `url` is a release download of kUpdateRepository on github.com.
[[nodiscard]] bool isReleaseDownloadUrl(std::string_view url);

} // namespace vloomdaw
