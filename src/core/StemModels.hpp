// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vloomdaw {

/// One file of a stem model, as the app downloads it: its name, its exact
/// size and its SHA-256 (lower-case hex). A download that does not match
/// both is thrown away, and a file is checked again before it is used.
struct StemModelFile {
    std::string_view name; ///< plain ASCII
    std::uint64_t bytes = 0;
    std::string_view sha256;
};

/// A model the stem extractor can use (the Demucs family, in the format of
/// demucs.cpp). The user picks which to download in the model store.
struct StemModel {
    std::string_view id; ///< stable: kept in the settings and in folder names
    std::string_view name;
    std::string_view about; ///< one line for the store
    /// The stems it gives, in the order the separator writes them.
    std::vector<std::string_view> stems;
    /// One file: every stem comes from it. Several: file i is the model
    /// fine-tuned for stem i, and the song goes through each in turn.
    std::vector<StemModelFile> files;
    bool v3 = false; ///< Hybrid Demucs (v3) rather than the Hybrid Transformer (v4)

    [[nodiscard]] std::uint64_t bytes() const; ///< all its files
};

/// The models on offer, the recommended one first.
[[nodiscard]] const std::vector<StemModel>& stemModels();
/// The model with this id, or null.
[[nodiscard]] const StemModel* findStemModel(std::string_view id);
/// Where a model file is downloaded from: HTTPS, at a fixed revision of the
/// repository, so the file there cannot change under its name.
[[nodiscard]] std::string stemModelUrl(const StemModelFile& file);
/// Whether every file of `model` is in `folder` with its right size. (Their
/// hashes are checked when they are downloaded and before they are used.)
[[nodiscard]] bool stemModelInstalled(const StemModel& model, const std::filesystem::path& folder);

/// Whether this computer can run the stem separator: on x86-64 it is built
/// for processors with AVX2 and FMA (2013 and later).
[[nodiscard]] bool stemSeparationSupported();

} // namespace vloomdaw
