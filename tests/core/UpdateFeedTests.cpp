// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/UpdateFeed.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace vloomdaw;

namespace {

const std::string kDownload = "https://github.com/vloom-audio/VloomDAW/releases/download/";

std::string asset(const std::string& tag, const std::string& name, int size = 1000)
{
    return R"({"name":")" + name + R"(","size":)" + std::to_string(size) + R"(,"browser_download_url":")"
        + kDownload + tag + "/" + name + R"("})";
}

/// A release with the installers for every platform; `signedSums`: with
/// SHA256SUMS.txt and its signature.
std::string release(
    const std::string& tag, bool prerelease = false, bool draft = false, bool signedSums = true)
{
    const std::string v = tag.substr(1);
    std::vector<std::string> assets = {asset(tag, "VloomDAW-" + v + "-windows-x64-setup.exe"),
        asset(tag, "VloomDAW-" + v + "-macos-universal.dmg"),
        asset(tag, "VloomDAW-" + v + "-linux-x86_64.AppImage"), asset(tag, "SHA256SUMS.txt", 300)};
    if (signedSums)
        assets.push_back(asset(tag, "SHA256SUMS.txt.minisig", 300));
    std::string list;
    for (const std::string& a : assets)
        list += (list.empty() ? "" : ",") + a;
    return R"({"tag_name":")" + tag + R"(","name":"VloomDAW )" + tag + R"(","body":"## Changes\n- faster",)"
        + R"("draft":)" + (draft ? "true" : "false") + R"(,"prerelease":)" + (prerelease ? "true" : "false")
        + R"(,"html_url":"https://github.com/vloom-audio/VloomDAW/releases/tag/)" + tag + R"(","assets":[)"
        + list + "]}";
}

std::string list(const std::vector<std::string>& releases)
{
    std::string out = "[";
    for (const std::string& r : releases)
        out += (out.size() > 1 ? "," : "") + r;
    return out + "]";
}

const Version kCurrent{0, 2, 0, {}};

} // namespace

TEST_CASE("The newest newer release is offered", "[update]")
{
    const auto r = newestUpdate(list({release("v0.2.1"), release("v0.3.0"), release("v0.1.0")}), kCurrent,
        false, UpdatePlatform::Windows);
    REQUIRE(r.error.empty());
    REQUIRE(r.release);
    CHECK(r.release->version == Version{0, 3, 0, {}});
    CHECK(r.release->tag == "v0.3.0");
    CHECK(r.release->title == "VloomDAW v0.3.0");
    CHECK(r.release->notes == "## Changes\n- faster");
    CHECK(r.release->pageUrl == "https://github.com/vloom-audio/VloomDAW/releases/tag/v0.3.0");
    CHECK(r.release->installer.name == "VloomDAW-0.3.0-windows-x64-setup.exe");
    CHECK(r.release->installer.url == kDownload + "v0.3.0/VloomDAW-0.3.0-windows-x64-setup.exe");
    CHECK(r.release->installer.size == 1000);
    CHECK(r.release->checksums.name == "SHA256SUMS.txt");
    CHECK(r.release->signature.name == "SHA256SUMS.txt.minisig");
}

TEST_CASE("Each platform gets its own installer", "[update]")
{
    const std::string feed = list({release("v0.3.0")});
    CHECK(newestUpdate(feed, kCurrent, false, UpdatePlatform::MacOS).release->installer.name
        == "VloomDAW-0.3.0-macos-universal.dmg");
    CHECK(newestUpdate(feed, kCurrent, false, UpdatePlatform::Linux).release->installer.name
        == "VloomDAW-0.3.0-linux-x86_64.AppImage");
}

TEST_CASE("Up to date: nothing newer, or the same version", "[update]")
{
    const auto r = newestUpdate(
        list({release("v0.2.0"), release("v0.1.0")}), kCurrent, false, UpdatePlatform::Windows);
    CHECK(r.error.empty());
    CHECK_FALSE(r.release);
    CHECK_FALSE(newestUpdate("[]", kCurrent, false, UpdatePlatform::Windows).release);
}

TEST_CASE("Pre-releases only on the beta channel", "[update]")
{
    const std::string feed = list({release("v0.3.0-rc.1", true), release("v0.2.1")});
    CHECK(newestUpdate(feed, kCurrent, false, UpdatePlatform::Windows).release->tag == "v0.2.1");
    CHECK(newestUpdate(feed, kCurrent, true, UpdatePlatform::Windows).release->tag == "v0.3.0-rc.1");
    // A pre-release version is one even if the release is not marked so.
    CHECK(newestUpdate(list({release("v0.3.0-rc.2")}), kCurrent, false, UpdatePlatform::Windows).release
        == std::nullopt);
    // The final release beats its release candidates.
    const std::string withFinal = list({release("v0.3.0-rc.1", true), release("v0.3.0")});
    CHECK(newestUpdate(withFinal, kCurrent, true, UpdatePlatform::Windows).release->tag == "v0.3.0");
}

TEST_CASE("Drafts, unsigned releases and odd tags are passed over", "[update]")
{
    const std::string feed = list({release("v0.5.0", false, true), release("v0.4.0", false, false, false),
        release("vnext"), release("v0.3.0")});
    const auto r = newestUpdate(feed, kCurrent, false, UpdatePlatform::Windows);
    REQUIRE(r.release);
    CHECK(r.release->tag == "v0.3.0");
}

TEST_CASE("Assets from anywhere but this repository's releases are refused", "[update]")
{
    std::string other = release("v0.3.0");
    const std::string from = kDownload + "v0.3.0/VloomDAW-0.3.0-windows-x64-setup.exe";
    other.replace(other.find(from), from.size(), "https://example.com/VloomDAW-0.3.0-windows-x64-setup.exe");
    CHECK_FALSE(newestUpdate(list({other}), kCurrent, false, UpdatePlatform::Windows).release);

    CHECK(isReleaseDownloadUrl(kDownload + "v0.3.0/SHA256SUMS.txt"));
    CHECK_FALSE(isReleaseDownloadUrl(kDownload));
    CHECK_FALSE(isReleaseDownloadUrl("http://github.com/vloom-audio/VloomDAW/releases/download/v1/x"));
    CHECK_FALSE(isReleaseDownloadUrl("https://github.com/vloom-audio/VloomDAW.evil/releases/download/v1/x"));
    CHECK_FALSE(isReleaseDownloadUrl(kDownload + "../../../other/repo/x"));
    CHECK_FALSE(isReleaseDownloadUrl(kDownload + "v1/x?y=z"));
    CHECK_FALSE(isReleaseDownloadUrl(kDownload + "v1/%2e%2e/x"));
}

TEST_CASE("A list that is not JSON is an error", "[update]")
{
    CHECK_FALSE(
        newestUpdate("<html>rate limited</html>", kCurrent, false, UpdatePlatform::Windows).error.empty());
    CHECK_FALSE(
        newestUpdate(R"({"message":"Not Found"})", kCurrent, false, UpdatePlatform::Windows).error.empty());
    // Wrong types inside are skipped, not fatal.
    const auto r = newestUpdate(R"([1, "x", {"tag_name": 5}, {"tag_name":"v0.3.0","assets":"none"}])",
        kCurrent, false, UpdatePlatform::Windows);
    CHECK(r.error.empty());
    CHECK_FALSE(r.release);
}

TEST_CASE("Checksums are looked up by exact file name", "[update]")
{
    const std::string a(64, 'a');
    const std::string b = std::string(63, 'B') + "C";
    const std::string sums = a + "  VloomDAW-0.3.0-windows-x64-setup.exe\r\n" + b
        + " *VloomDAW-0.3.0-macos-universal.dmg\n" + "short  VloomDAW-0.3.0-linux-x86_64.AppImage\n";
    CHECK(checksumFor(sums, "VloomDAW-0.3.0-windows-x64-setup.exe") == a);
    CHECK(checksumFor(sums, "VloomDAW-0.3.0-macos-universal.dmg") == std::string(63, 'b') + "c");
    CHECK_FALSE(checksumFor(sums, "VloomDAW-0.3.0-linux-x86_64.AppImage")); // malformed line
    CHECK_FALSE(checksumFor(sums, "VloomDAW-0.3.0-windows-x64-setup")); // prefix only
    CHECK_FALSE(checksumFor(sums + a + "  VloomDAW-0.3.0-windows-x64-setup.exe\n", // listed twice
        "VloomDAW-0.3.0-windows-x64-setup.exe"));
    const std::string notHex = std::string(63, 'a') + "g";
    CHECK_FALSE(checksumFor(notHex + "  x.exe\n", "x.exe"));
}
