// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QAbstractButton>

namespace vloomdaw {

/// An on / off switch (a checkable button drawn as a pill with a knob), in
/// the theme's accent colour when on. Use toggled() like a QCheckBox.
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT

public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override { return {40, 22}; }

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace vloomdaw
