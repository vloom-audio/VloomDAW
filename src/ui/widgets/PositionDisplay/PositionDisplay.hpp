// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/widgets/LcdDisplay/LcdDisplay.hpp"

namespace vloomdaw {

/// Song position on an LCD: BAR · BEAT · TICK, or MIN : SEC . MS.
/// Clicking it switches between the two.
class PositionDisplay : public LcdDisplay {
    Q_OBJECT

public:
    enum class Mode { BarsBeats, Time };

    explicit PositionDisplay(QWidget* parent = nullptr);

    /// "bar.beat.tick", e.g. "002.1.552".
    void setPosition(const QString& text);
    /// "M:SS.mmm", e.g. "0:02.276".
    void setTime(const QString& text);

    [[nodiscard]] Mode mode() const { return mode_; }
    void setMode(Mode mode);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void showCurrent();

    Mode mode_ = Mode::BarsBeats;
    QString position_ = QStringLiteral("001.1.000");
    QString time_ = QStringLiteral("0:00.000");
};

} // namespace vloomdaw
