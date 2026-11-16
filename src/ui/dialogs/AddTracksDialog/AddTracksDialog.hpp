// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QDialog>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QToolButton;

namespace vloomdaw {

class MixerModel;
class Project;
struct PluginStore;

/// Specification returned by `AddTracksDialog` when the user confirms `OK`.
struct AddTracksSpec {
    enum class Kind { Audio = 0, Instrument = 1 };
    enum class FxPreset { None = 0, BuiltinEq = 1, BuiltinGain = 2, BuiltinEqAndGain = 3 };

    Kind kind = Kind::Audio;
    QString baseName{};
    int count = 1;
    bool packFolder = false;
    bool autoColor = true;
    std::uint32_t customColor = 0x3b82f6U;

    // Audio mode options
    bool mono = false; ///< false: Stereo, true: Mono
    FxPreset fxPreset = FxPreset::None;
    /// `-1` = New Dedicated Insert per track, `0` = Master (Direct), `1..N` = existing Insert.
    int audioOutputInsert = -1;
    bool audioOutputAscending = false;

    // Instrument mode options
    bool useExistingInstrument = false;
    /// Index into `PluginStore::plugins`, or `nullopt` for an empty MIDI channel.
    std::optional<std::size_t> newInstrumentPlugin{};
    bool openInstrumentEditor = true;
    /// Index into `Project::channels()` when `useExistingInstrument` is true.
    std::optional<std::size_t> existingChannel{};
    bool instrumentAscending = false;
};

/// Studio One-style `Add Tracks` dialog (`Ctrl+T` or right-click in Playlist
/// track headers): top mode cards (`Audio` | `Instrument`), `Name`, `Count`,
/// `Pack Folder`, `Color` swatch + `Auto-Color`, and mode-specific routing /
/// FX chain / instrument selectors.
class AddTracksDialog : public QDialog {
    Q_OBJECT

public:
    AddTracksDialog(const Project& project, const MixerModel& mixer, const PluginStore& plugins,
        AddTracksSpec::Kind initialKind = AddTracksSpec::Kind::Audio, QWidget* parent = nullptr);

    [[nodiscard]] AddTracksSpec spec() const;

private:
    void setKind(AddTracksSpec::Kind kind);
    void updateColorSwatch();
    void updateControlStates();
    [[nodiscard]] QString defaultBaseName(AddTracksSpec::Kind kind) const;

    const Project& project_;
    const PluginStore& plugins_;
    AddTracksSpec::Kind kind_ = AddTracksSpec::Kind::Audio;
    std::size_t nextColorIndex_ = 0;
    std::uint32_t customColor_ = 0x3b82f6U;
    bool nameEditedByUser_ = false;

    QToolButton* audioCardBtn_ = nullptr;
    QToolButton* instCardBtn_ = nullptr;

    QLineEdit* nameEdit_ = nullptr;
    QSpinBox* countSpin_ = nullptr;
    QCheckBox* packFolderCheck_ = nullptr;
    QPushButton* colorSwatchBtn_ = nullptr;
    QCheckBox* autoColorCheck_ = nullptr;

    QStackedWidget* modeStack_ = nullptr;

    // Audio page controls
    QComboBox* formatCombo_ = nullptr;
    QComboBox* fxChainCombo_ = nullptr;
    QComboBox* audioOutputCombo_ = nullptr;
    QCheckBox* audioAscendingCheck_ = nullptr;

    // Instrument page controls
    QRadioButton* newInstRadio_ = nullptr;
    QComboBox* newInstCombo_ = nullptr;
    QCheckBox* openEditorCheck_ = nullptr;
    QRadioButton* existingInstRadio_ = nullptr;
    QComboBox* existingInstCombo_ = nullptr;
    QCheckBox* instAscendingCheck_ = nullptr;
};

} // namespace vloomdaw
