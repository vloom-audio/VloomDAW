// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QDialog>
#include <QString>

namespace vloomdaw {

struct AboutRuntimeInfo {
    QString audioStatus;
    int scannedPluginCount = 0;
};

/// Studio-grade "About VloomDAW" dialog showing brand hero banner, live engine
/// & architecture specs, copyable diagnostics, GPLv3 license & open-source
/// credits, and direct project links.
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(const AboutRuntimeInfo& runtime, QWidget* parent = nullptr);

private:
    [[nodiscard]] QString buildDiagnosticsText() const;

    AboutRuntimeInfo runtime_;
};

} // namespace vloomdaw
