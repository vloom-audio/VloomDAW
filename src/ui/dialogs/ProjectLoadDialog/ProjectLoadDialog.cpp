// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/ProjectLoadDialog/ProjectLoadDialog.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QEventLoop>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

ProjectLoadDialog::ProjectLoadDialog(const QString& projectName, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("projectLoadDialog"));
    setWindowTitle(tr("Loading %1…").arg(projectName));
    setWindowModality(Qt::WindowModal);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setFixedWidth(450);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(12);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("projectLoadCard"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 18, 16, 18);
    cardLayout->setSpacing(8);

    stepLabel_ = new QLabel(tr("Preparing project…"), card);
    stepLabel_->setObjectName(QStringLiteral("projectLoadStep"));
    stepLabel_->setWordWrap(false);
    cardLayout->addWidget(stepLabel_);

    detailLabel_ = new QLabel(card);
    detailLabel_->setObjectName(QStringLiteral("projectLoadDetail"));
    detailLabel_->setVisible(false);
    cardLayout->addWidget(detailLabel_);

    cardLayout->addSpacing(6);

    bar_ = new QProgressBar(card);
    bar_->setObjectName(QStringLiteral("projectLoadBar"));
    bar_->setRange(0, 1000);
    bar_->setValue(0);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(6);
    cardLayout->addWidget(bar_);

    outer->addWidget(card);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addStretch(1);

    cancelBtn_ = new QPushButton(tr("Cancel"), this);
    cancelBtn_->setMinimumWidth(96);
    connect(cancelBtn_, &QPushButton::clicked, this, [this] {
        canceled_ = true;
        cancelBtn_->setEnabled(false);
        stepLabel_->setText(tr("Cancelling…"));
    });
    buttonRow->addWidget(cancelBtn_);
    outer->addLayout(buttonRow);
}

bool ProjectLoadDialog::advance(int step, int totalSteps, const QString& stepText, const QString& detailText)
{
    if (canceled_)
        return false;

    const int safeTotal = std::max(1, totalSteps);
    const int clampedStep = std::clamp(step, 0, safeTotal);
    const int permille = static_cast<int>(std::lround((clampedStep * 1000.0) / safeTotal));

    const QFontMetrics fm(stepLabel_->font());
    stepLabel_->setText(fm.elidedText(stepText, Qt::ElideMiddle, 400));

    if (detailText.isEmpty()) {
        detailLabel_->clear();
        detailLabel_->setVisible(false);
    } else {
        detailLabel_->setText(detailText);
        detailLabel_->setVisible(true);
    }
    bar_->setValue(permille);

    if (!isVisible())
        show();
    raise();
    repaint();
    QApplication::processEvents(QEventLoop::ExcludeSocketNotifiers, 20);
    return !canceled_;
}

void ProjectLoadDialog::finishAndClose()
{
    finishing_ = true;
    bar_->setValue(1000);
    hide();
    close();
}

void ProjectLoadDialog::reject()
{
    if (finishing_) {
        QDialog::reject();
        return;
    }
    // Mark cancelled instead of hiding mid-loop; MainWindow cleans up and closes us.
    canceled_ = true;
    if (cancelBtn_ != nullptr)
        cancelBtn_->setEnabled(false);
    if (stepLabel_ != nullptr)
        stepLabel_->setText(tr("Cancelling…"));
}

void ProjectLoadDialog::closeEvent(QCloseEvent* event)
{
    if (finishing_) {
        QDialog::closeEvent(event);
        return;
    }
    canceled_ = true;
    if (cancelBtn_ != nullptr)
        cancelBtn_->setEnabled(false);
    if (stepLabel_ != nullptr)
        stepLabel_->setText(tr("Cancelling…"));
    event->ignore();
}

} // namespace vloomdaw
