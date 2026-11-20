// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QAbstractButton>

namespace vloomdaw {

/// The small on/off switch of the built-in plugins' editors: a 26 x 14 px
/// pill, the theme's accent when on. Its label goes under it (Stacked, in
/// the floating panel: "Auto makeup") or before it (Inline, in the footer).
///
/// A checkable button: toggled() / clicked(bool) when the user switches it,
/// setChecked() to show a value. BuiltinEditorWindow::toggle() makes one
/// bound to a switch parameter.
class PillToggle : public QAbstractButton {
    Q_OBJECT

public:
    enum class Layout { Stacked, Inline };

    explicit PillToggle(const QString& label, Layout layout = Layout::Stacked, QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    [[nodiscard]] bool hitButton(const QPoint& pos) const override { return rect().contains(pos); }

private:
    Layout layout_;
};

} // namespace vloomdaw
