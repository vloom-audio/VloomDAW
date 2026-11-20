// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QStringList>
#include <QWidget>

#include <functional>

namespace vloomdaw {

/// A row of buttons joined in one rounded frame, one per choice, the one
/// chosen filled with the theme's accent, with a small caption over it: a
/// choice parameter in the floating panel ("Character: Soft Tube Tape
/// Hard"). Click a button, or use the arrow keys when it has the focus.
///
/// Not bound by itself: an editor shows a value with setCurrent() (from
/// BuiltinEditorWindow::bind()) and changes it from onChosen.
class ChoiceButtons : public QWidget {
    Q_OBJECT

public:
    /// `caption` over the row; `choices` left to right; `width` of the row in px.
    ChoiceButtons(const QString& caption, QStringList choices, int width, QWidget* parent = nullptr);

    /// Shows choice `index` as chosen (nothing for one out of range).
    void setCurrent(int index);
    [[nodiscard]] int current() const { return current_; }

    /// The user chose `index` (not called by setCurrent()).
    std::function<void(int index)> onChosen;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] QRectF rowRect() const;
    [[nodiscard]] QRectF buttonRect(int index) const;
    void choose(int index);

    QString caption_;
    QStringList choices_;
    int width_;
    int current_ = -1; ///< none until setCurrent()
};

} // namespace vloomdaw
