// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/ParameterControl.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vloomdaw {

namespace {

constexpr double kClickSlop = 3.0; ///< px a click may move and still be a click

} // namespace

ControlSpec ControlSpec::of(const vloom::ParameterInfo& parameter, std::optional<ParameterScale> scale)
{
    ControlSpec spec;
    spec.minValue = parameter.minValue;
    spec.maxValue = parameter.maxValue;
    spec.defaultValue = parameter.defaultValue;
    spec.stepped = parameter.stepped;
    spec.unit = parameter.unit;
    spec.scale = scale.value_or(defaultScale(parameter.unit, parameter.minValue, parameter.maxValue));
    if (parameter.stepped)
        for (const std::string& label : parameter.valueLabels)
            spec.labels.push_back(QString::fromStdString(label));
    return spec;
}

double ControlSpec::clamp(double value) const
{
    if (std::isnan(value))
        return defaultValue;
    value = std::clamp(value, minValue, maxValue);
    return stepped ? std::round(value) : value;
}

double ControlSpec::positionOf(double value) const
{
    return vloomdaw::positionOf(scale, minValue, maxValue, value);
}

double ControlSpec::valueAt(double position) const
{
    return vloomdaw::valueAt(scale, minValue, maxValue, position, stepped);
}

std::optional<double> ControlSpec::zeroPosition() const
{
    return vloomdaw::zeroPosition(scale, minValue, maxValue);
}

QString ControlSpec::text(double value) const
{
    if (isChoice()) {
        const auto index = static_cast<qsizetype>(std::lround(clamp(value) - minValue));
        return labels.value(index);
    }
    if (isSwitch())
        return value >= 0.5 ? QObject::tr("On") : QObject::tr("Off");
    return QString::fromStdString(formatParameterValue(unit, value));
}

ParameterControl::ParameterControl(const QString& label, QWidget* parent)
    : QWidget(parent)
    , label_(label)
{
    setAccessibleName(label);
}

void ParameterControl::setLabel(const QString& label)
{
    label_ = label;
    updateGeometry();
    update();
}

void ParameterControl::setSpec(const ControlSpec& spec)
{
    spec_ = spec;
    setFocusPolicy(Qt::TabFocus); // a click does not draw the focus ring; Tab does
    setCursor(spec.isChoice() ? Qt::PointingHandCursor : Qt::SizeVerCursor);
    setToolTip(spec.isChoice() ? tr("%1: click or drag to change").arg(label_)
                               : tr("%1: drag or scroll (Shift: fine), double-click to reset").arg(label_));
    value_ = spec.clamp(value_);
    refreshText();
}

void ParameterControl::setValue(double value)
{
    if (!spec_) {
        value_ = value;
        return;
    }
    value = spec_->clamp(value);
    if (value == value_ && !text_.isEmpty())
        return;
    value_ = value;
    refreshText();
}

void ParameterControl::setText(const QString& text)
{
    if (text == text_)
        return;
    text_ = text;
    setAccessibleDescription(text);
    update();
}

void ParameterControl::setFormatter(std::function<QString(double)> format)
{
    format_ = std::move(format);
    refreshText();
}

double ParameterControl::position() const { return spec_ ? spec_->positionOf(value_) : 0.0; }

void ParameterControl::refreshText()
{
    if (!spec_)
        return;
    setText(format_ ? format_(value_) : spec_->text(value_));
}

void ParameterControl::editTo(double value)
{
    if (!spec_)
        return;
    value = spec_->clamp(value);
    if (value == value_)
        return;
    value_ = value;
    refreshText();
    emit valueEdited(value);
}

void ParameterControl::mousePressEvent(QMouseEvent* event)
{
    if (!spec_ || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    pressed_ = true;
    dragged_ = false;
    lastY_ = event->position().y();
    dragAt_ = position();
    event->accept();
}

void ParameterControl::mouseMoveEvent(QMouseEvent* event)
{
    if (!pressed_ || !spec_)
        return;
    const double y = event->position().y();
    if (!dragged_ && std::abs(y - lastY_) < kClickSlop)
        return;
    dragged_ = true;
    dragAt_ = dragPosition(dragAt_, lastY_ - y, event->modifiers().testFlag(Qt::ShiftModifier));
    lastY_ = y;
    editTo(spec_->valueAt(dragAt_));
}

void ParameterControl::mouseReleaseEvent(QMouseEvent* event)
{
    if (!pressed_ || event->button() != Qt::LeftButton)
        return;
    pressed_ = false;
    // A click on a choice moves it on to the next name, round to the first.
    if (!dragged_ && spec_ && spec_->isChoice())
        editTo(value_ >= spec_->maxValue ? spec_->minValue : value_ + 1.0);
}

void ParameterControl::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!spec_ || event->button() != Qt::LeftButton)
        return;
    if (spec_->isChoice()) {
        // The second click of a quick pair: one more step, not a reset.
        pressed_ = true;
        dragged_ = false;
        lastY_ = event->position().y();
        dragAt_ = position();
        return;
    }
    editTo(spec_->defaultValue);
}

void ParameterControl::wheelEvent(QWheelEvent* event)
{
    if (!spec_) {
        event->ignore();
        return;
    }
    // Some platforms turn Shift+wheel into horizontal scrolling.
    const QPoint delta = event->angleDelta();
    const double notches = (delta.y() != 0 ? delta.y() : delta.x()) / 120.0;
    if (notches == 0.0) {
        event->ignore();
        return;
    }
    if (spec_->stepped)
        editTo(stepValue(value_, notches > 0.0 ? 1 : -1, spec_->minValue, spec_->maxValue));
    else
        editTo(spec_->valueAt(
            wheelPosition(position(), notches, event->modifiers().testFlag(Qt::ShiftModifier))));
    event->accept();
}

void ParameterControl::keyPressEvent(QKeyEvent* event)
{
    if (!spec_) {
        QWidget::keyPressEvent(event);
        return;
    }
    int steps = 0;
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Right:
        steps = 1;
        break;
    case Qt::Key_Down:
    case Qt::Key_Left:
        steps = -1;
        break;
    case Qt::Key_Home:
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        editTo(spec_->defaultValue);
        return;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    if (spec_->stepped)
        editTo(stepValue(value_, steps, spec_->minValue, spec_->maxValue));
    else
        editTo(
            spec_->valueAt(wheelPosition(position(), steps, event->modifiers().testFlag(Qt::ShiftModifier))));
}

} // namespace vloomdaw
