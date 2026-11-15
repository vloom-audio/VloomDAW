// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/ParameterEditorWindow.hpp"

#include "ui/widgets/Knob/Knob.hpp"

#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace vloomdaw {

namespace {

bool isFrequency(const vloom::ParameterInfo& parameter)
{
    return parameter.unit == "Hz" && parameter.minValue > 0.0 && parameter.maxValue > parameter.minValue;
}

/// How `value` reads, with the parameter's unit.
QString valueText(const vloom::ParameterInfo& parameter, double value)
{
    if (parameter.unit == "Hz")
        return value >= 1000.0 ? QStringLiteral("%1 kHz").arg(value / 1000.0, 0, 'f', 2)
                               : QStringLiteral("%1 Hz").arg(value, 0, 'f', value < 100.0 ? 1 : 0);
    if (parameter.unit == "dB")
        return QStringLiteral("%1%2 dB")
            .arg(value > 0.0 ? QStringLiteral("+") : QString())
            .arg(value, 0, 'f', 1);
    const QString number = QString::number(value, 'f', 2);
    return parameter.unit.empty()
        ? number
        : QStringLiteral("%1 %2").arg(number, QString::fromStdString(parameter.unit));
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

ParameterEditorWindow::ParameterEditorWindow(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::Tool)
    , instance_(std::move(instance))
{
    setObjectName(QStringLiteral("parameterEditor"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(title);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(14, 12, 14, 12);
    row->setSpacing(16);

    // One column per group (a band of an equalizer), its controls stacked;
    // a parameter without a group gets a column of its own.
    const vloom::AudioProcessor& processor = instance_->processor();
    QVBoxLayout* column = nullptr;
    std::string columnGroup;
    bool headed = false; // the column has its heading
    const auto heading = [this](QWidget* widget) {
        widget->setObjectName(QStringLiteral("parameterGroup"));
        QFont font = widget->font();
        font.setBold(true);
        widget->setFont(font);
    };
    for (const vloom::ParameterInfo& parameter : processor.parameters()) {
        if (!(parameter.maxValue > parameter.minValue))
            continue;
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
        const QString group = QString::fromStdString(parameter.group);
        // "Low Shelf Gain" in the group "Low Shelf" reads "Gain".
        QString name = QString::fromStdString(parameter.name);
        if (!group.isEmpty() && name.startsWith(group + QLatin1Char(' ')))
            name = name.mid(group.size() + 1);

        if (parameter.stepped && parameter.minValue == 0.0 && parameter.maxValue == 1.0) {
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
    if (column == nullptr)
        row->addWidget(new QLabel(tr("This plugin has nothing to adjust."), this));
    for (int i = 0; i < row->count(); ++i)
        if (auto* each = qobject_cast<QVBoxLayout*>(row->itemAt(i)->layout()))
            each->addStretch(1); // short columns stay at the top
    setFixedSize(sizeHint());
}

} // namespace vloomdaw
