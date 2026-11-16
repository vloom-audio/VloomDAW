// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <vector>

class QMenu;
class QToolButton;

namespace vloomdaw {

/// The preset selector in the header of a built-in plugin's editor: back,
/// the name (a menu of them all, with "Save Preset As..." and "Delete") and
/// forward.
///
/// It holds the presets that come with the app (given by the editor) and the
/// user's own, kept in one file per plugin in the app's data folder. The
/// editor tells it when the user changes a value (markModified()); the name
/// then gets a star.
class PresetBar : public QWidget {
    Q_OBJECT

public:
    /// Every parameter's value, by id.
    using Values = std::vector<double>;

    /// A preset that comes with the app.
    struct Factory {
        QString name;
        Values values;
    };

    /// How the bar reaches the plugin's parameters.
    struct Source {
        std::function<Values()> read; ///< the values now
        std::function<void(const Values&)> apply; ///< sets them all
        std::function<double(std::uint32_t, double)> clamp; ///< a saved value, brought into range
    };

    /// `pluginId` names the user's preset file ("vloom.eq"). `factory` is not
    /// empty and its first preset holds the default values: a saved preset
    /// that lacks a value gets it from there.
    PresetBar(const QString& pluginId, std::vector<Factory> factory, Source source, QWidget* parent);

    /// The chosen preset's index (the app's first, then the user's); -1: none ("Custom").
    [[nodiscard]] int current() const { return preset_; }
    [[nodiscard]] bool isModified() const { return modified_; }
    /// Shows `preset` as chosen without setting its values (an A/B switch did that).
    void restore(int preset, bool modified);
    /// The user changed a value. Ignored while the bar itself sets a preset.
    void markModified();

signals:
    /// The user's preset `index` was deleted: the ones after it moved down by one.
    void removed(int index);

private:
    struct Preset {
        QString name;
        Values values;
        bool factory = false; ///< comes with the app: cannot be deleted
    };

    [[nodiscard]] bool known(int index) const;
    void choose(int index);
    void step(int by);
    void saveAs();
    void remove();
    void fillMenu();
    void showName();
    void load();
    void save();

    QString path_; ///< the user's preset file
    Source source_;
    std::vector<Preset> presets_; ///< the app's, then the user's
    int preset_ = -1;
    bool modified_ = false;
    bool applying_ = false; ///< a preset is being set: not the user turning a knob
    QToolButton* name_ = nullptr;
    QMenu* menu_ = nullptr;
};

} // namespace vloomdaw
