// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/widgets/LcdDisplay/LcdDisplay.hpp"

class QLineEdit;

namespace vloomdaw {

/// Tempo and time signature on an LCD: BPM | SIG.
///
/// The BPM field is editable: drag up/down (Shift for fine steps), scroll, or
/// double-click to type a value. The time signature is display-only for now.
class TempoDisplay : public LcdDisplay {
    Q_OBJECT

public:
    static constexpr double kMinBpm = 10.0; ///< same range the engine accepts
    static constexpr double kMaxBpm = 999.0;

    explicit TempoDisplay(QWidget* parent = nullptr);

    [[nodiscard]] double tempo() const { return bpm_; }

public slots:
    /// Shows `bpm` unless the user is currently dragging or typing.
    void setTempo(double bpm);
    void setTimeSignature(int beatsPerBar, int beatUnit);

signals:
    void tempoChanged(double bpm);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void applyUserTempo(double bpm);
    void showTempo();
    void beginEdit();
    void finishEdit(bool commit);
    [[nodiscard]] bool overTempo(const QPoint& pos) const;

    double bpm_ = 120.0;
    bool dragging_ = false;
    double dragStartBpm_ = 0.0;
    qreal dragStartY_ = 0.0;
    QLineEdit* editor_ = nullptr;
};

} // namespace vloomdaw
