// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/StemModels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace vloomdaw;

TEST_CASE("Every stem model can be downloaded and checked", "[stems]")
{
    std::set<std::string_view> ids;
    std::set<std::string_view> names;
    REQUIRE_FALSE(stemModels().empty());
    for (const StemModel& model : stemModels()) {
        INFO(model.id);
        CHECK(ids.insert(model.id).second);
        CHECK_FALSE(model.name.empty());
        CHECK_FALSE(model.about.empty());
        CHECK(findStemModel(model.id) == &model);
        // One model for all the stems, or one per stem.
        REQUIRE_FALSE(model.files.empty());
        CHECK((model.files.size() == 1 || model.files.size() == model.stems.size()));
        CHECK(model.stems.size() >= 4);
        std::uint64_t total = 0;
        for (const StemModelFile& file : model.files) {
            CHECK(names.insert(file.name).second);
            CHECK(file.bytes > 1'000'000);
            total += file.bytes;
            // A plain file name: it goes into a path and a URL.
            for (const char c : file.name)
                CHECK((std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == '.'));
            CHECK(file.name.find("..") == std::string_view::npos);
            REQUIRE(file.sha256.size() == 64);
            for (const char c : file.sha256)
                CHECK(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
            const std::string url = stemModelUrl(file);
            CHECK(url.starts_with("https://huggingface.co/"));
            CHECK(url.ends_with(std::string("/") + std::string(file.name)));
        }
        CHECK(model.bytes() == total);
    }
    CHECK(findStemModel("no-such-model") == nullptr);
    CHECK(findStemModel("") == nullptr);
}

TEST_CASE("A stem model is installed when all its files are there with their size", "[stems]")
{
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "vloomdaw-stem-models-test";
    fs::remove_all(folder);
    fs::create_directories(folder);
    const StemModel model{
        "test", "Test", "For the test.", {"a", "b"}, {{"one.bin", 5, ""}, {"two.bin", 3, ""}}};
    const auto write = [&](const char* name, const std::string& bytes) {
        std::ofstream(folder / name, std::ios::binary) << bytes;
    };
    CHECK_FALSE(stemModelInstalled(model, folder));
    write("one.bin", "12345");
    CHECK_FALSE(stemModelInstalled(model, folder)); // the second is missing
    write("two.bin", "12");
    CHECK_FALSE(stemModelInstalled(model, folder)); // cut short
    write("two.bin", "123");
    CHECK(stemModelInstalled(model, folder));
    CHECK_FALSE(stemModelInstalled(model, folder / "elsewhere"));
    CHECK_FALSE(stemModelInstalled(StemModel{"none", "None", "No files.", {}, {}}, folder));
    fs::remove_all(folder);
}

TEST_CASE("Asking whether the processor can separate stems does not crash", "[stems]")
{
    CHECK(stemSeparationSupported() == stemSeparationSupported());
}
