// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QColor>
#include <QPointF>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

class QPainter;
class QPainterPath;

namespace vloomdaw {

class Readout;

/// Pieces the editors of the time and modulation effects share (delay,
/// reverb, chorus, flanger, phaser, tremolo).
namespace time_effects {

/// The colours of the designs' voices and channels: teal, yellow, pink,
/// violet (chorus voices 1 to 4; tremolo left and right are 0 and 2).
[[nodiscard]] QColor voiceColour(int index);

/// How far the floating panel reaches up a display (its height and margin).
[[nodiscard]] int panelReach();

/// A percentage with its sign: "+60 %", "−20 %", "0 %" (feedback, offsets).
[[nodiscard]] QString signedPercentText(double percent);
/// An angle in whole degrees: "90°".
[[nodiscard]] QString degreesText(double degrees);

/// A time as an axis reads it: "250 ms", "1.50 s".
[[nodiscard]] QString timeText(double seconds);

/// Upright grid lines with their times over `span` seconds from x `left`
/// (time 0) to `right`, from `top` to `bottom`, the labels at `labelY`
/// (a baseline). Leaves out time 0.
void paintTimeAxis(
    QPainter& painter, double left, double right, double span, double top, double bottom, double labelY);

} // namespace time_effects

/// A display of an effect with one round handle to drag (an echo, a notch,
/// the decay): the subclass paints, says where the handle is (handle()) and
/// turns the place it is dragged to into a parameter (onHandleDragged).
/// Shift drags finely; a double-click on the handle calls onHandleReset.
class HandleDisplay : public QWidget {
    Q_OBJECT

public:
    explicit HandleDisplay(QWidget* parent = nullptr);

    /// The handle was dragged so its centre would be at `point` (not kept
    /// in the display: the editor turns it into a value in range).
    std::function<void(QPointF point)> onHandleDragged;
    /// The handle was double-clicked.
    std::function<void()> onHandleReset;
    /// A drag ended: a display that held its scale still for it can fit it again.
    std::function<void()> onDragFinished;

    [[nodiscard]] bool isDragging() const { return dragging_; }
    /// The pointer over the handle: a cursor for the way it moves.
    void setHandleCursor(Qt::CursorShape cursor) { cursor_ = cursor; }

    [[nodiscard]] QSize minimumSizeHint() const override { return {320, 200}; }

protected:
    /// Where the handle's centre is now; nullopt: no handle.
    [[nodiscard]] virtual std::optional<QPointF> handle() const = 0;
    /// Draws the handle at `centre`: a dark disc ringed in the accent.
    void paintHandle(QPainter& painter, QPointF centre) const;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    [[nodiscard]] bool onHandle(QPointF point) const;

    Qt::CursorShape cursor_ = Qt::SizeAllCursor;
    bool dragging_ = false;
    QPointF pressedAt_; ///< where the drag started
    QPointF handleAt_; ///< the handle's centre then
};

/// A frequency response over a frequency axis (the flanger's comb, the
/// phaser's notches): the response now as a filled curve, where it sweeps
/// to dashed, 0 dB as a line, and a handle on that line at a frequency to
/// drag sideways (hzAt() turns its place into one).
class ResponseDisplay : public HandleDisplay {
    Q_OBJECT

public:
    /// The response in dB at a frequency (Hz).
    using Response = std::function<double(double)>;

    struct Axis {
        bool log = true; ///< log (20 Hz .. 20 kHz) or linear
        double fromHz = 20.0;
        double toHz = 20000.0;
        std::vector<std::pair<double, QString>> grid; ///< lines and their names
    };

    explicit ResponseDisplay(Axis axis, QWidget* parent = nullptr);

    /// Draws `now` filled, each of `sweeps` dashed, the handle at `handleHz`
    /// (none: nullopt) named `handleLabel`.
    void setCurves(Response now, std::vector<Response> sweeps, std::optional<double> handleHz,
        const QString& handleLabel);

    /// The frequency at `x`, kept on the axis.
    [[nodiscard]] double hzAt(double x) const;

protected:
    [[nodiscard]] std::optional<QPointF> handle() const override;
    void paintEvent(QPaintEvent* event) override;

private:
    [[nodiscard]] double xOf(double hz) const;
    [[nodiscard]] double yOf(double db) const;
    [[nodiscard]] double plotBottom() const;
    [[nodiscard]] QPainterPath trace(const Response& response) const;

    Axis axis_;
    Response now_;
    std::vector<Response> sweeps_;
    std::optional<double> handleHz_;
    QString handleLabel_;
};

/// A row of buttons for a choice, one lit (the designs' segmented switch:
/// Tremolo's "Tremolo | Pan"), its name over it in small muted text.
class SegmentedChoice : public QWidget {
    Q_OBJECT

public:
    SegmentedChoice(const QString& label, QStringList options, QWidget* parent = nullptr);

    /// Lights option `index` without emitting chosen().
    void setCurrent(int index);
    [[nodiscard]] int current() const { return current_; }

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    /// The user chose option `index`.
    void chosen(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] QRectF segment(int index) const;

    QString label_;
    QStringList options_;
    int current_ = 0;
};

/// The footer's "Output" reading: the peak of what the effect puts out,
/// held for a moment so it can be read, "–" while no audio runs.
class OutputLevel {
public:
    explicit OutputLevel(Readout* readout);

    /// A reading from BuiltinEditorWindow::meter().
    void push(bool running, double outputDb);

private:
    Readout* readout_;
    double held_ = -std::numeric_limits<double>::infinity();
    int ticks_ = 0; ///< readings since held_ was set
};

} // namespace vloomdaw
