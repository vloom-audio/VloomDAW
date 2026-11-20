// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/ParameterControl.hpp"

namespace vloomdaw {

/// A "Label value" reading in the footer of a built-in plugin's editor:
/// the label in muted text and the value in semibold, 11 px.
///
/// Bound to a parameter (BuiltinEditorWindow::readout(id)) it turns as
/// ParameterControl: drag, scroll, double-click; a choice ("Peak", "RMS")
/// steps on a click. Unbound it shows what setText() gives it: a meter
/// ("Gain reduction −4.4 dB"), a state ("Gate Open").
class Readout : public ParameterControl {
    Q_OBJECT

public:
    explicit Readout(const QString& label, QWidget* parent = nullptr);

    /// The widest text the value will show (e.g. "−80.0 dB"), so the footer
    /// does not shift as it changes. Empty: as wide as the text now.
    void setWidestText(const QString& text);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    [[nodiscard]] int valueWidth() const;

    QString widest_;
};

} // namespace vloomdaw
