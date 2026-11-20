// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/ParameterScale.hpp"

#include <vloom/engine/plugin/AudioProcessor.hpp>

#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <optional>
#include <string>

namespace vloomdaw {

/// What a control needs to know about the parameter it turns: its range,
/// how its travel spreads over it and how a value reads.
struct ControlSpec {
    double minValue = 0.0;
    double maxValue = 1.0;
    double defaultValue = 0.0;
    ParameterScale scale = ParameterScale::Linear;
    bool stepped = false;
    std::string unit;
    /// A choice's names, one per whole value from minValue (valueLabels).
    QStringList labels;

    /// From what the processor reports; `scale` overrides the one its unit
    /// suggests (defaultScale()).
    [[nodiscard]] static ControlSpec of(
        const vloom::ParameterInfo& parameter, std::optional<ParameterScale> scale = std::nullopt);

    [[nodiscard]] bool isChoice() const { return stepped && !labels.isEmpty(); }
    /// Stepped 0 .. 1 without names: on or off.
    [[nodiscard]] bool isSwitch() const
    {
        return stepped && labels.isEmpty() && minValue == 0.0 && maxValue == 1.0;
    }
    [[nodiscard]] double clamp(double value) const;
    [[nodiscard]] double positionOf(double value) const;
    [[nodiscard]] double valueAt(double position) const;
    /// Where the arc of a range across zero starts; nullopt: at the bottom.
    [[nodiscard]] std::optional<double> zeroPosition() const;
    /// "−18.0 dB", "RMS", "On".
    [[nodiscard]] QString text(double value) const;
};

/// The base of a control bound to one parameter that turns like a knob:
/// drag up or down (Shift: fine), scroll, arrow keys, double-click for the
/// default. A choice steps through its names, and a click moves it on to
/// the next one. Subclasses only paint (ArcKnob, Readout).
///
/// A control without a spec (setSpec() not called) only shows a text: a
/// readout of a meter.
class ParameterControl : public QWidget {
    Q_OBJECT

public:
    explicit ParameterControl(const QString& label, QWidget* parent = nullptr);

    void setSpec(const ControlSpec& spec);
    [[nodiscard]] const std::optional<ControlSpec>& spec() const { return spec_; }
    [[nodiscard]] const QString& label() const { return label_; }
    /// Names it; empty: no label (a reading that follows a switch with the name).
    void setLabel(const QString& label);
    [[nodiscard]] double value() const { return value_; }
    /// The text the control shows: the value's, or one set with setText().
    [[nodiscard]] const QString& text() const { return text_; }

    /// Shows `value` (kept in range) without emitting valueEdited().
    void setValue(double value);
    /// Shows `text` in place of a value: a meter's reading.
    void setText(const QString& text);
    /// How a value reads; the default is ControlSpec::text().
    void setFormatter(std::function<QString(double)> format);

signals:
    /// The user changed the value.
    void valueEdited(double value);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    /// The value as 0 .. 1 of the travel.
    [[nodiscard]] double position() const;

private:
    void editTo(double value);
    void refreshText();

    QString label_;
    std::optional<ControlSpec> spec_;
    double value_ = 0.0;
    QString text_;
    std::function<QString(double)> format_;
    bool pressed_ = false;
    bool dragged_ = false; ///< the press moved: not a click
    double lastY_ = 0.0; ///< where the drag was last
    double dragAt_ = 0.0; ///< the drag's position, unrounded (a stepped value moves in whole steps)
};

} // namespace vloomdaw
