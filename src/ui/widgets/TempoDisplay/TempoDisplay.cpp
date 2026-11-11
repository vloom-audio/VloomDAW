// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/TempoDisplay/TempoDisplay.hpp"

#include <QDoubleValidator>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>

namespace vloomdaw {

namespace {

constexpr qsizetype kTempoField = 0;
constexpr double kDragStep = 0.25; ///< BPM per pixel
constexpr double kFineDragStep = 0.01; ///< with Shift held
constexpr double kWheelStep = 1.0; ///< BPM per wheel notch
constexpr double kFineWheelStep = 0.1;

double roundToHundredth(double value) { return std::round(value * 100.0) / 100.0; }

QString formatBpm(double bpm) { return QString::number(bpm, 'f', 2); }

/// Commits on Return / focus loss and cancels on Escape.
class InlineEditor : public QLineEdit {
public:
    using QLineEdit::QLineEdit;
    std::function<void(bool)> finished;

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape) {
            finished(false);
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            finished(true);
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent* event) override
    {
        QLineEdit::focusOutEvent(event);
        if (isVisible())
            finished(true);
    }
};

} // namespace

TempoDisplay::TempoDisplay(QWidget* parent)
    : LcdDisplay(parent)
{
    setObjectName(QStringLiteral("tempoDisplay"));
    setMouseTracking(true);
    setToolTip(tr("Tempo — drag up/down (Shift: fine), scroll, or double-click to type"));
    setFields({
        {formatBpm(bpm_), QStringLiteral("888.88"), tr("BPM"), Separator::Rule},
        {QStringLiteral("4/4"), QStringLiteral("88/8"), tr("SIG"), Separator::Dot},
    });
}

void TempoDisplay::setTempo(double bpm)
{
    if (dragging_ || editor_ != nullptr)
        return; // the user's gesture wins until it ends
    bpm_ = std::clamp(bpm, kMinBpm, kMaxBpm);
    showTempo();
}

void TempoDisplay::setTimeSignature(int beatsPerBar, int beatUnit)
{
    setFieldValue(1, QStringLiteral("%1/%2").arg(beatsPerBar).arg(beatUnit));
}

void TempoDisplay::showTempo() { setFieldValue(kTempoField, formatBpm(bpm_)); }

void TempoDisplay::applyUserTempo(double bpm)
{
    const double clamped = roundToHundredth(std::clamp(bpm, kMinBpm, kMaxBpm));
    if (clamped == bpm_)
        return;
    bpm_ = clamped;
    showTempo();
    emit tempoChanged(bpm_);
}

bool TempoDisplay::overTempo(const QPoint& pos) const { return fieldAt(pos) == kTempoField; }

void TempoDisplay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !overTempo(event->position().toPoint())) {
        LcdDisplay::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    dragStartBpm_ = bpm_;
    dragStartY_ = event->globalPosition().y();
}

void TempoDisplay::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) {
        setCursor(overTempo(event->position().toPoint()) ? Qt::SizeVerCursor : Qt::ArrowCursor);
        return;
    }
    const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? kFineDragStep : kDragStep;
    applyUserTempo(dragStartBpm_ + (dragStartY_ - event->globalPosition().y()) * step);
}

void TempoDisplay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        dragging_ = false;
    LcdDisplay::mouseReleaseEvent(event);
}

void TempoDisplay::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && overTempo(event->position().toPoint())) {
        dragging_ = false;
        beginEdit();
        return;
    }
    LcdDisplay::mouseDoubleClickEvent(event);
}

void TempoDisplay::wheelEvent(QWheelEvent* event)
{
    if (!overTempo(event->position().toPoint())) {
        LcdDisplay::wheelEvent(event);
        return;
    }
    // Some platforms turn Shift+wheel into horizontal scrolling.
    const QPoint delta = event->angleDelta();
    const int notches = (delta.y() != 0 ? delta.y() : delta.x()) / 120;
    if (notches == 0)
        return;
    const bool fine = event->modifiers().testFlag(Qt::ShiftModifier);
    applyUserTempo(bpm_ + notches * (fine ? kFineWheelStep : kWheelStep));
    event->accept();
}

void TempoDisplay::beginEdit()
{
    if (editor_ != nullptr)
        return;
    auto* editor = new InlineEditor(this);
    editor->setObjectName(QStringLiteral("lcdEditor"));
    editor->setFont(digitFont());
    editor->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    editor->setValidator(new QDoubleValidator(kMinBpm, kMaxBpm, 2, editor));
    editor->setText(formatBpm(bpm_));
    editor->selectAll();
    editor->finished = [this](bool commit) { finishEdit(commit); };
    editor->setGeometry(fieldRect(kTempoField).adjusted(-4, 3, 4, -3));
    editor_ = editor;
    editor->show();
    editor->setFocus(Qt::MouseFocusReason);
}

void TempoDisplay::finishEdit(bool commit)
{
    if (editor_ == nullptr)
        return;
    QLineEdit* editor = editor_;
    editor_ = nullptr; // first: hiding the editor triggers focusOut again
    bool ok = false;
    const double value = QLocale().toDouble(editor->text(), &ok);
    editor->hide();
    editor->deleteLater();
    if (commit && ok)
        applyUserTempo(value);
}

} // namespace vloomdaw
