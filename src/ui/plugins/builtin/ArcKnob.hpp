// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/ParameterControl.hpp"

namespace vloomdaw {

/// The knob of the built-in plugins' editors: a 48 px dial with a 270°
/// track and the value's arc in the theme's accent (from zero for a range
/// across it), a dark centre with a pointer, the label under it in small
/// muted text and the value under that. 72 px wide.
///
/// Turns as ParameterControl. BuiltinEditorWindow::knob() makes one bound
/// to a parameter.
class ArcKnob : public ParameterControl {
    Q_OBJECT

public:
    explicit ArcKnob(const QString& label, QWidget* parent = nullptr);

    /// The arc's colour; an invalid colour (the default) is the theme's accent.
    void setArcColor(const QColor& color);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor arc_;
};

} // namespace vloomdaw
