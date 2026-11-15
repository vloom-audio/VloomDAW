// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/PresetBar.hpp"

#include "core/PluginPresets.hpp"
#include "ui/Theme.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
#include <QStandardPaths>
#include <QToolButton>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace vloomdaw {

PresetBar::PresetBar(const QString& pluginId, std::vector<Factory> factory, Source source, QWidget* parent)
    : QWidget(parent)
    , path_(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/presets/")
          + pluginId + QStringLiteral(".json"))
    , source_(std::move(source))
{
    Q_ASSERT(!factory.empty());
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    const auto stepButton = [this, row](const QString& icon, const QString& what, int by) {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("presetStep"));
        button->setIcon(Theme::icon(icon));
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(what);
        button->setAccessibleName(what);
        connect(button, &QToolButton::clicked, this, [this, by] { step(by); });
        row->addWidget(button);
    };
    stepButton(QStringLiteral("chevron-left"), tr("Previous preset"), -1);
    name_ = new QToolButton(this);
    name_->setObjectName(QStringLiteral("presetName"));
    name_->setFocusPolicy(Qt::NoFocus);
    name_->setPopupMode(QToolButton::InstantPopup);
    name_->setToolTip(tr("Presets: choose one, or save this setting as your own"));
    name_->setAccessibleName(tr("Preset"));
    menu_ = new QMenu(name_);
    name_->setMenu(menu_);
    connect(menu_, &QMenu::aboutToShow, this, &PresetBar::fillMenu);
    row->addWidget(name_);
    stepButton(QStringLiteral("chevron-right"), tr("Next preset"), 1);

    for (Factory& each : factory)
        presets_.push_back(
            Preset{.name = std::move(each.name), .values = std::move(each.values), .factory = true});
    load();
    // The defaults, or a setting that came with the project.
    preset_ = source_.read() == presets_.front().values ? 0 : -1;
    showName();
}

bool PresetBar::known(int index) const
{
    return index >= 0 && static_cast<std::size_t>(index) < presets_.size();
}

void PresetBar::restore(int preset, bool modified)
{
    preset_ = known(preset) ? preset : -1;
    modified_ = modified;
    showName();
}

void PresetBar::markModified()
{
    if (applying_ || modified_)
        return;
    modified_ = true;
    showName();
}

void PresetBar::showName()
{
    const QString name = known(preset_) ? presets_[static_cast<std::size_t>(preset_)].name : tr("Custom");
    // A star: changed since it was chosen.
    name_->setText(known(preset_) && modified_ ? name + QStringLiteral(" *") : name);
}

void PresetBar::choose(int index)
{
    if (!known(index))
        return;
    applying_ = true;
    source_.apply(presets_[static_cast<std::size_t>(index)].values);
    applying_ = false;
    preset_ = index;
    modified_ = false;
    showName();
}

void PresetBar::step(int by)
{
    const int count = static_cast<int>(presets_.size());
    // From "Custom": forward to the first, back to the last.
    const int from = preset_ >= 0 ? preset_ : (by > 0 ? -1 : 0);
    choose(((from + by) % count + count) % count);
}

void PresetBar::fillMenu()
{
    menu_->clear();
    bool userSection = false;
    for (std::size_t i = 0; i < presets_.size(); ++i) {
        if (!presets_[i].factory && !userSection) {
            menu_->addSeparator();
            userSection = true;
        }
        QAction* action
            = menu_->addAction(presets_[i].name, this, [this, i] { choose(static_cast<int>(i)); });
        action->setCheckable(true);
        action->setChecked(static_cast<int>(i) == preset_);
    }
    menu_->addSeparator();
    menu_->addAction(tr("Save Preset As..."), this, &PresetBar::saveAs);
    const bool mine = known(preset_) && !presets_[static_cast<std::size_t>(preset_)].factory;
    QAction* removal
        = menu_->addAction(mine ? tr("Delete \"%1\"").arg(presets_[static_cast<std::size_t>(preset_)].name)
                                : tr("Delete Preset"),
            this, &PresetBar::remove);
    removal->setEnabled(mine);
}

void PresetBar::saveAs()
{
    const bool mine = known(preset_) && !presets_[static_cast<std::size_t>(preset_)].factory;
    bool accepted = false;
    QString name = QInputDialog::getText(this, tr("Save Preset"), tr("Name:"), QLineEdit::Normal,
        mine ? presets_[static_cast<std::size_t>(preset_)].name : QString(), &accepted)
                       .simplified();
    if (!accepted || name.isEmpty())
        return;
    while (static_cast<std::size_t>(name.toUtf8().size()) > kMaxPresetNameBytes)
        name.chop(1);
    const auto named
        = [&name](const Preset& preset) { return preset.name.compare(name, Qt::CaseInsensitive) == 0; };
    auto existing = std::find_if(presets_.begin(), presets_.end(), named);
    if (existing != presets_.end() && existing->factory) {
        // The app's own presets stay as they are: the user's gets its own name.
        name = tr("%1 (mine)").arg(name);
        existing = std::find_if(presets_.begin(), presets_.end(), named);
    }
    if (existing != presets_.end()) {
        existing->values = source_.read();
    } else {
        const auto count = static_cast<std::size_t>(
            std::count_if(presets_.begin(), presets_.end(), [](const Preset& p) { return !p.factory; }));
        if (count >= kMaxPresets) {
            QMessageBox::warning(this, tr("Save Preset"),
                tr("There are %1 presets already. Delete one to save another.")
                    .arg(static_cast<qulonglong>(count)));
            return;
        }
        presets_.push_back(Preset{.name = name, .values = source_.read(), .factory = false});
        existing = presets_.end() - 1;
    }
    preset_ = static_cast<int>(existing - presets_.begin());
    modified_ = false;
    showName();
    save();
}

void PresetBar::remove()
{
    if (!known(preset_) || presets_[static_cast<std::size_t>(preset_)].factory)
        return;
    const int gone = preset_;
    presets_.erase(presets_.begin() + gone);
    preset_ = -1; // the sound stays; it is no preset now
    modified_ = false;
    showName();
    save();
    emit removed(gone);
}

void PresetBar::load()
{
    QFile file(path_);
    if (file.size() > static_cast<qint64>(kMaxPresetFileBytes) || !file.open(QIODevice::ReadOnly))
        return; // none saved yet, or not a file of ours
    const QByteArray text = file.readAll();
    for (const PluginPreset& saved :
        parsePluginPresets(std::string_view(text.constData(), static_cast<std::size_t>(text.size())))) {
        Preset preset{.name = QString::fromStdString(saved.name).simplified(),
            .values = presets_.front().values,
            .factory = false};
        const auto named = [&preset](const Preset& other) {
            return other.name.compare(preset.name, Qt::CaseInsensitive) == 0;
        };
        if (preset.name.isEmpty() || std::any_of(presets_.begin(), presets_.end(), named))
            continue; // a name is one preset
        for (const auto& [id, value] : saved.values)
            if (id < preset.values.size())
                preset.values[id] = source_.clamp(id, value);
        presets_.push_back(std::move(preset));
    }
}

void PresetBar::save()
{
    std::vector<PluginPreset> mine;
    for (const Preset& preset : presets_) {
        if (preset.factory)
            continue;
        PluginPreset saved{.name = preset.name.toStdString(), .values = {}};
        for (std::uint32_t id = 0; id < preset.values.size(); ++id)
            saved.values.emplace_back(id, preset.values[id]);
        mine.push_back(std::move(saved));
    }
    const std::string text = writePluginPresets(mine);
    // Written whole or not at all: a crash half way leaves the old file.
    QSaveFile file(path_);
    if (!QDir().mkpath(QFileInfo(path_).absolutePath()) || !file.open(QIODevice::WriteOnly)
        || file.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size())
        || !file.commit())
        QMessageBox::warning(this, tr("Presets"),
            tr("The presets could not be saved to %1.").arg(QDir::toNativeSeparators(path_)));
}

} // namespace vloomdaw
