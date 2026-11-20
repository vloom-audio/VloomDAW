// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/ParameterEditorWindow.hpp"

#include "core/ParameterChoices.hpp"
#include "ui/DroppedFiles.hpp"
#include "ui/plugins/ParameterText.hpp"
#include "ui/plugins/SampleFiles.hpp"
#include "ui/widgets/Knob/Knob.hpp"

#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace vloomdaw {

/// One sample slot: a button that picks a file, the file's name (or why it
/// is missing) and a button that empties it. An audio file dropped on it
/// loads too.
class SampleSlotControl final : public QFrame {
public:
    SampleSlotControl(int slot, std::function<void(int, const QString&)> load, std::function<void(int)> clear,
        QWidget* parent)
        : QFrame(parent)
        , slot_(slot)
        , load_(std::move(load))
    {
        setObjectName(QStringLiteral("sampleSlot"));
        setAcceptDrops(true);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 4);
        layout->setSpacing(2);
        auto* buttons = new QHBoxLayout;
        buttons->setSpacing(4);
        auto* pick = new QPushButton(tr("Load sample…"), this);
        pick->setToolTip(tr("Pick an audio file, or drop one here from the browser"));
        connect(pick, &QPushButton::clicked, this, [this] {
            const QString file = chooseSampleFile(this, path_);
            if (!file.isEmpty())
                load_(slot_, file);
        });
        auto* empty = new QToolButton(this);
        empty->setText(QStringLiteral("×"));
        empty->setToolTip(tr("Remove the sample"));
        connect(empty, &QToolButton::clicked, this, [this, clear = std::move(clear)] { clear(slot_); });
        buttons->addWidget(pick, 1);
        buttons->addWidget(empty);
        layout->addLayout(buttons);
        file_ = new QLabel(this);
        file_->setObjectName(QStringLiteral("sampleFile"));
        file_->setAlignment(Qt::AlignHCenter);
        file_->setMinimumWidth(110);
        file_->setMaximumWidth(150);
        layout->addWidget(file_);
    }

    /// Shows what the slot holds.
    void display(const vloom::SampleSlotInfo& info)
    {
        path_ = QString::fromStdString(info.path);
        const QString name = path_.isEmpty() ? tr("(empty)") : QFileInfo(path_).fileName();
        const bool missing = !info.error.empty();
        file_->setText(file_->fontMetrics().elidedText(
            missing ? tr("Missing: %1").arg(name) : name, Qt::ElideMiddle, 150));
        QString tip = QDir::toNativeSeparators(path_);
        if (missing)
            tip += QLatin1Char('\n') + QString::fromStdString(info.error);
        file_->setToolTip(tip);
        file_->setProperty("missing", missing);
        file_->style()->unpolish(file_);
        file_->style()->polish(file_);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (!droppedAudio(event->mimeData()).isEmpty())
            event->acceptProposedAction();
    }
    void dropEvent(QDropEvent* event) override
    {
        const QString file = droppedAudio(event->mimeData());
        if (file.isEmpty())
            return;
        event->acceptProposedAction();
        load_(slot_, file);
    }

private:
    int slot_;
    std::function<void(int, const QString&)> load_;
    QLabel* file_ = nullptr;
    QString path_;
};

namespace {

bool isFrequency(const vloom::ParameterInfo& parameter)
{
    return parameter.unit == "Hz" && parameter.minValue > 0.0 && parameter.maxValue > parameter.minValue;
}

/// How `value` reads, with the parameter's unit.
QString valueText(const vloom::ParameterInfo& parameter, double value)
{
    return parameterValueText(parameter.unit, value);
}

/// A knob's position (0 .. 1) for a frequency, in octaves, and back.
double toPosition(const vloom::ParameterInfo& parameter, double value)
{
    return std::log(value / parameter.minValue) / std::log(parameter.maxValue / parameter.minValue);
}
double fromPosition(const vloom::ParameterInfo& parameter, double position)
{
    return parameter.minValue * std::pow(parameter.maxValue / parameter.minValue, position);
}

} // namespace

ParameterEditorWindow::ParameterEditorWindow(std::shared_ptr<vloom::PluginInstance> instance,
    const QString& title, QWidget* parent, PluginEditorContext context)
    : QWidget(parent, Qt::Window | Qt::Tool)
    , instance_(std::move(instance))
{
    setObjectName(QStringLiteral("parameterEditor"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(title);
    if (context.slotTitle.isEmpty())
        context.slotTitle = title;

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    header_ = new PluginEditorHeader(std::move(context), nullptr, this, this);
    outer->addWidget(header_);

    auto* body = new QWidget(this);
    header_->setCaptureTarget(body);

    auto* row = new QHBoxLayout(body);
    row->setContentsMargins(14, 12, 14, 12);
    row->setSpacing(16);

    vloom::AudioProcessor& processor = instance_->processor();

    // Sample slots: each at the top of its group's column ("Sample", "Pad 3").
    const int slotCount = processor.sampleSlotCount();
    std::vector<QString> slotNames;
    for (int slot = 0; slot < slotCount; ++slot) {
        slots_.push_back(new SampleSlotControl(
            slot, [this](int at, const QString& path) { loadSample(at, path); },
            [this](int at) {
                if (instance_->processor().clearSample(at)) {
                    refreshSampleSlots();
                    emit edited();
                }
            },
            body));
        slotNames.push_back(QString::fromStdString(processor.sampleSlot(slot).name));
    }
    std::vector<bool> placed(static_cast<std::size_t>(slotCount), false);
    const auto placeSlotOf = [&](const QString& group, QVBoxLayout* into) {
        for (int slot = 0; slot < slotCount; ++slot)
            if (!placed[static_cast<std::size_t>(slot)]
                && slotNames[static_cast<std::size_t>(slot)] == group) {
                into->addWidget(slots_[static_cast<std::size_t>(slot)], 0, Qt::AlignHCenter);
                placed[static_cast<std::size_t>(slot)] = true;
            }
    };

    // One column per group (a band of an equalizer), its controls stacked;
    // a parameter without a group gets a column of its own.
    QVBoxLayout* column = nullptr;
    std::string columnGroup;
    bool headed = false; // the column has its heading
    const auto heading = [](QWidget* widget) {
        widget->setObjectName(QStringLiteral("parameterGroup"));
        QFont font = widget->font();
        font.setBold(true);
        widget->setFont(font);
    };
    for (const vloom::ParameterInfo& parameter : processor.parameters()) {
        if (!(parameter.maxValue > parameter.minValue))
            continue;
        const QString group = QString::fromStdString(parameter.group);
        if (column == nullptr || parameter.group.empty() || parameter.group != columnGroup) {
            column = new QVBoxLayout;
            column->setSpacing(6);
            row->addLayout(column);
            columnGroup = parameter.group;
            headed = false;
        }
        const double value
            = std::clamp(processor.parameterValue(parameter.id).value_or(parameter.defaultValue),
                parameter.minValue, parameter.maxValue);
        const std::uint32_t id = parameter.id;
        // "Low Shelf Gain" in the group "Low Shelf" reads "Gain".
        QString name = QString::fromStdString(parameter.name);
        if (!group.isEmpty() && name.startsWith(group + QLatin1Char(' ')))
            name = name.mid(group.size() + 1);

        const bool isSwitch = parameter.stepped && parameter.minValue == 0.0 && parameter.maxValue == 1.0;
        if (isSwitch && !isChoice(parameter)) {
            // A switch. Named after its group, it is the column's heading.
            auto* toggle = new QCheckBox(name, this);
            toggle->setChecked(value >= 0.5);
            connect(toggle, &QCheckBox::toggled, this, [this, id](bool on) {
                (void)instance_->setParameter(id, on ? 1.0 : 0.0);
                emit edited();
            });
            if (!headed && name == group) {
                heading(toggle);
                headed = true;
                placeSlotOf(group, column);
            }
            column->addWidget(toggle, 0, Qt::AlignHCenter);
            continue;
        }
        if (!headed) {
            auto* label = new QLabel(group, this); // empty: keeps the knobs level with the other columns'
            label->setAlignment(Qt::AlignHCenter);
            heading(label);
            column->addWidget(label);
            headed = true;
            placeSlotOf(group, column);
        }
        if (isChoice(parameter)) {
            // A choice: its values by name (a mode, a curve, a note length).
            auto* caption = new QLabel(name, this);
            caption->setAlignment(Qt::AlignHCenter);
            auto* choices = new QComboBox(this);
            choices->setObjectName(QStringLiteral("parameterChoice"));
            for (const std::string& label : parameter.valueLabels)
                choices->addItem(QString::fromStdString(label));
            choices->setCurrentIndex(choiceIndex(parameter, value));
            connect(choices, &QComboBox::currentIndexChanged, this, [this, id, parameter](int index) {
                if (index < 0)
                    return;
                (void)instance_->setParameter(id, choiceValue(parameter, index));
                emit edited();
            });
            column->addWidget(caption);
            column->addWidget(choices, 0, Qt::AlignHCenter);
            continue;
        }
        const bool octaves = isFrequency(parameter);
        // Frequencies: the knob turns 0 .. 1 and the value follows in octaves.
        auto* knob = octaves
            ? new Knob(name, 0.0, 1.0, toPosition(parameter, parameter.defaultValue), this)
            : new Knob(name, parameter.minValue, parameter.maxValue, parameter.defaultValue, this);
        const auto valueAt = [parameter, octaves](double position) {
            double v = octaves ? fromPosition(parameter, position) : position;
            if (parameter.stepped)
                v = std::round(v);
            return std::clamp(v, parameter.minValue, parameter.maxValue);
        };
        knob->setValueText(
            [parameter, valueAt](double position) { return valueText(parameter, valueAt(position)); });
        knob->setValue(octaves ? toPosition(parameter, value) : value);
        auto* reading = new QLabel(valueText(parameter, value), this);
        reading->setObjectName(QStringLiteral("parameterValue"));
        reading->setAlignment(Qt::AlignHCenter);
        reading->setMinimumWidth(72); // the longest reading: no jitter while turning
        connect(knob, &Knob::valueChanged, this, [this, id, parameter, valueAt, reading](double position) {
            const double v = valueAt(position);
            // A plugin whose queue is full drops this one; the next turn sends another.
            (void)instance_->setParameter(id, v);
            reading->setText(valueText(parameter, v));
            emit edited();
        });
        column->addWidget(knob, 0, Qt::AlignHCenter);
        column->addWidget(reading);
    }
    // Slots no group is named after: a column of their own, first.
    if (std::find(placed.begin(), placed.end(), false) != placed.end()) {
        auto* samples = new QVBoxLayout;
        samples->setSpacing(6);
        auto* label = new QLabel(tr("Samples"), this);
        label->setAlignment(Qt::AlignHCenter);
        heading(label);
        samples->addWidget(label);
        for (int slot = 0; slot < slotCount; ++slot)
            if (!placed[static_cast<std::size_t>(slot)]) {
                auto* name = new QLabel(slotNames[static_cast<std::size_t>(slot)], this);
                name->setAlignment(Qt::AlignHCenter);
                samples->addWidget(name);
                samples->addWidget(slots_[static_cast<std::size_t>(slot)], 0, Qt::AlignHCenter);
            }
        row->insertLayout(0, samples);
        column = samples;
    }
    // A meter for what dynamics turn down.
    if (processor.gainReductionDb().has_value()) {
        auto* meter = new QVBoxLayout;
        meter->setSpacing(6);
        auto* label = new QLabel(tr("Reduction"), this);
        label->setAlignment(Qt::AlignHCenter);
        heading(label);
        meter->addWidget(label);
        reduction_ = new QLabel(this);
        reduction_->setObjectName(QStringLiteral("parameterValue"));
        reduction_->setAlignment(Qt::AlignHCenter);
        reduction_->setMinimumWidth(72);
        meter->addWidget(reduction_);
        row->addLayout(meter);
        const auto showReduction = [this] {
            const float db = instance_->processor().gainReductionDb().value_or(0.0f);
            reduction_->setText(QStringLiteral("%1 dB").arg(db > 0.05f ? -db : 0.0f, 0, 'f', 1));
        };
        showReduction();
        meterTimer_ = new QTimer(this);
        connect(meterTimer_, &QTimer::timeout, this, showReduction);
        meterTimer_->start(50);
    }
    if (column == nullptr)
        row->addWidget(new QLabel(tr("This plugin has nothing to adjust."), this));
    for (int i = 0; i < row->count(); ++i)
        if (auto* each = qobject_cast<QVBoxLayout*>(row->itemAt(i)->layout()))
            each->addStretch(1); // short columns stay at the top
    refreshSampleSlots();

    // Most fit as they are; a big one (16 drum pads) scrolls within the screen.
    const QSize content = body->sizeHint();
    const QScreen* screen = parent != nullptr ? parent->screen() : QApplication::primaryScreen();
    const QSize room = screen != nullptr ? screen->availableGeometry().size() * 0.9 : QSize(1600, 900);
    const int headerHeight = header_->sizeHint().height();
    if (content.width() > room.width() || content.height() + headerHeight > room.height()) {
        auto* scroll = new QScrollArea(this);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(body);
        scroll->setWidgetResizable(true);
        outer->addWidget(scroll);
        resize(std::min(content.width() + 24, room.width()),
            std::min(content.height() + headerHeight + 24, room.height()));
        return;
    }
    outer->addWidget(body);
    const QSize hint = sizeHint();
    setFixedSize(std::max(hint.width(), 320), hint.height());
}

void ParameterEditorWindow::loadSample(int slot, const QString& path)
{
    QWidget* anchor = slot >= 0 && static_cast<std::size_t>(slot) < slots_.size()
        ? slots_[static_cast<std::size_t>(slot)]
        : nullptr;
    const bool loaded = loadSampleFile(instance_->processor(), slot, path, anchor);
    refreshSampleSlots();
    if (loaded)
        emit edited();
}

void ParameterEditorWindow::refreshSampleSlots()
{
    for (std::size_t slot = 0; slot < slots_.size(); ++slot)
        slots_[slot]->display(instance_->processor().sampleSlot(static_cast<int>(slot)));
}

void ParameterEditorWindow::setEditorContext(PluginEditorContext context)
{
    if (header_ != nullptr)
        header_->setContext(std::move(context));
}

void ParameterEditorWindow::setBypassedState(bool bypassed)
{
    if (header_ != nullptr)
        header_->setBypassedState(bypassed);
}

} // namespace vloomdaw
