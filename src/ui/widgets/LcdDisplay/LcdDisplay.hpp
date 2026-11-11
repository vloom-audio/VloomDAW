// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QFont>
#include <QList>
#include <QWidget>

namespace vloomdaw {

/// A recessed LCD panel showing a row of value fields, each with a caption
/// underneath and faint unlit segments behind the digits. Colours come from
/// Theme. Subclasses (PositionDisplay, TempoDisplay) decide what to show and
/// how the fields react to the mouse.
class LcdDisplay : public QWidget {
    Q_OBJECT

public:
    /// What separates a field from the next one.
    enum class Separator { Dot, Colon, Rule };

    struct Field {
        QString value; ///< what is lit, right-aligned, e.g. "002"
        QString mask; ///< unlit segments and minimum width, e.g. "8888"
        QString caption; ///< small label underneath, e.g. "BAR"
        Separator after = Separator::Dot;
    };

    explicit LcdDisplay(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    /// Replaces all fields; resizes only if a field needs more room.
    void setFields(QList<Field> fields);
    void setFieldValue(qsizetype index, const QString& value);
    [[nodiscard]] const QList<Field>& fields() const { return fields_; }

    /// Area of field `index` (digits and caption), in widget coordinates.
    [[nodiscard]] QRect fieldRect(qsizetype index) const;
    /// Index of the field under `pos`, or -1.
    [[nodiscard]] qsizetype fieldAt(const QPoint& pos) const;

    [[nodiscard]] const QFont& digitFont() const { return digitFont_; }

    void paintEvent(QPaintEvent* event) override;

private:
    [[nodiscard]] static QString segmentsFor(const Field& field);
    [[nodiscard]] int fieldWidth(const Field& field) const;
    [[nodiscard]] int contentWidth() const;
    [[nodiscard]] int contentLeft() const;

    QList<Field> fields_;
    QFont digitFont_;
    QFont captionFont_;
};

} // namespace vloomdaw
