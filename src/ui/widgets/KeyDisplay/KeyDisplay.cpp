// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/KeyDisplay/KeyDisplay.hpp"

#include "core/NoteNames.hpp"

#include <QActionGroup>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>

#include <cmath>

namespace vloomdaw {

namespace {

constexpr int kPixelsPerStep = 12; ///< vertical drag per value
constexpr int kDragThreshold = 4; ///< pixels before a press counts as a drag

QString latin1(std::string_view text) { return QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size())); }

int scaleCount() { return static_cast<int>(allScales().size()); }

/// `value` wrapped into 0..count-1.
int wrap(int value, int count)
{
    const int r = value % count;
    return r < 0 ? r + count : r;
}

} // namespace

KeyDisplay::KeyDisplay(QWidget* parent)
    : LcdDisplay(parent)
{
    setObjectName(QStringLiteral("keyDisplay"));
    setCursor(Qt::SizeVerCursor);
    setToolTip(tr("Key: root note and scale. Drag up/down or scroll to change, click to pick\n"
                  "(the piano roll will highlight the scale)"));
    // "88" fits the widest note ("C#"), "8888" the widest scale ("HMIN").
    setFields({
        {latin1(noteName(root_)), QStringLiteral("88"), tr("ROOT"), Separator::Rule},
        {latin1(scaleInfo(scale_).shortName), QStringLiteral("8888"), tr("SCALE"), Separator::Dot},
    });
    showKey();
}

void KeyDisplay::setRootNote(int pitchClass)
{
    pitchClass = pitchClassOf(pitchClass);
    if (pitchClass == root_)
        return;
    root_ = pitchClass;
    showKey();
}

void KeyDisplay::setScale(Scale scale)
{
    if (scale == scale_)
        return;
    scale_ = scale;
    showKey();
}

void KeyDisplay::showKey()
{
    setFieldValue(kRootField, latin1(noteName(root_)));
    setFieldValue(kScaleField, latin1(scaleInfo(scale_).shortName));
    setAccessibleName(tr("Key %1 %2").arg(latin1(noteName(root_)), latin1(scaleInfo(scale_).name)));
}

void KeyDisplay::applyUserRoot(int pitchClass)
{
    pitchClass = pitchClassOf(pitchClass);
    if (pitchClass == root_)
        return;
    root_ = pitchClass;
    showKey();
    emit rootNoteChanged(root_);
}

void KeyDisplay::applyUserScale(int index)
{
    const auto scale = static_cast<Scale>(wrap(index, scaleCount()));
    if (scale == scale_)
        return;
    scale_ = scale;
    showKey();
    emit scaleChanged(scale_);
}

void KeyDisplay::stepByUser(qsizetype field, int from, int steps)
{
    if (field == kRootField)
        applyUserRoot(from + steps);
    else if (field == kScaleField)
        applyUserScale(from + steps);
}

void KeyDisplay::showMenu(qsizetype field)
{
    // Rebuilt on each click, so the check mark matches; never while open.
    if (menu_ == nullptr)
        menu_ = new QMenu(this);
    menu_->clear();
    auto* group = new QActionGroup(menu_);
    const auto add = [&](const QString& text, bool checked, auto apply) {
        QAction* action = menu_->addAction(text);
        action->setCheckable(true);
        action->setChecked(checked);
        group->addAction(action);
        connect(action, &QAction::triggered, this, apply);
    };
    if (field == kRootField) {
        for (int pc = 0; pc < kPitchClasses; ++pc)
            add(latin1(noteName(pc)), pc == root_, [this, pc] { applyUserRoot(pc); });
    } else {
        for (const ScaleInfo& s : allScales()) {
            const int index = static_cast<int>(s.scale);
            add(latin1(s.name), s.scale == scale_, [this, index] { applyUserScale(index); });
        }
    }
    menu_->popup(mapToGlobal(fieldRect(field).bottomLeft()));
}

void KeyDisplay::mousePressEvent(QMouseEvent* event)
{
    const qsizetype field = fieldAt(event->position().toPoint());
    if (event->button() != Qt::LeftButton || field < 0) {
        LcdDisplay::mousePressEvent(event);
        return;
    }
    pressedField_ = field;
    dragged_ = false;
    dragStartY_ = event->globalPosition().y();
    dragStartValue_ = field == kRootField ? root_ : scaleIndex();
}

void KeyDisplay::mouseMoveEvent(QMouseEvent* event)
{
    if (pressedField_ < 0)
        return;
    const double dy = dragStartY_ - event->globalPosition().y();
    if (!dragged_ && std::abs(dy) < kDragThreshold)
        return;
    dragged_ = true;
    stepByUser(pressedField_, dragStartValue_, static_cast<int>(dy / kPixelsPerStep));
}

void KeyDisplay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && pressedField_ >= 0) {
        const qsizetype field = pressedField_;
        pressedField_ = -1;
        if (!dragged_)
            showMenu(field);
        return;
    }
    LcdDisplay::mouseReleaseEvent(event);
}

void KeyDisplay::wheelEvent(QWheelEvent* event)
{
    const qsizetype field = fieldAt(event->position().toPoint());
    const QPoint delta = event->angleDelta();
    const int notches = (delta.y() != 0 ? delta.y() : delta.x()) / 120;
    if (field < 0 || notches == 0) {
        event->ignore();
        return;
    }
    stepByUser(field, field == kRootField ? root_ : scaleIndex(), notches);
    event->accept();
}

} // namespace vloomdaw
