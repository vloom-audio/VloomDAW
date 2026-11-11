// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/Scales.hpp"
#include "ui/widgets/LcdDisplay/LcdDisplay.hpp"

class QMenu;

namespace vloomdaw {

/// LCD panel showing the project's key: root note ("C" .. "B", caption
/// ROOT) and scale ("MAJ", "MIN", …, caption SCALE). The piano roll will use
/// it to highlight the scale.
///
/// On either field: drag up / down or scroll to step through the values
/// (wrapping round), or click without dragging to pick from a menu.
class KeyDisplay : public LcdDisplay {
    Q_OBJECT

public:
    explicit KeyDisplay(QWidget* parent = nullptr);

    /// Pitch class 0 (C) .. 11 (B).
    [[nodiscard]] int rootNote() const { return root_; }
    [[nodiscard]] Scale scale() const { return scale_; }

public slots:
    /// Shows `pitchClass` (any integer, wrapped to 0..11) without emitting.
    void setRootNote(int pitchClass);
    /// Shows `scale` without emitting.
    void setScale(vloomdaw::Scale scale);

signals:
    /// The user picked a root note, 0 (C) .. 11 (B).
    void rootNoteChanged(int pitchClass);
    void scaleChanged(vloomdaw::Scale scale);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum Field : qsizetype { kRootField = 0, kScaleField = 1 };

    /// Steps field `field` by `steps` from `from` (wrapping), as the user.
    void stepByUser(qsizetype field, int from, int steps);
    void applyUserRoot(int pitchClass);
    void applyUserScale(int index);
    void showMenu(qsizetype field);
    void showKey();
    [[nodiscard]] int scaleIndex() const { return static_cast<int>(scale_); }

    QMenu* menu_ = nullptr;
    int root_ = 0;
    Scale scale_ = Scale::Major;
    qsizetype pressedField_ = -1; ///< field under the press, -1 when none
    bool dragged_ = false; ///< the press became a drag, so no menu on release
    double dragStartY_ = 0.0;
    int dragStartValue_ = 0;
};

} // namespace vloomdaw
