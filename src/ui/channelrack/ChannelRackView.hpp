// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QWidget>

#include <cstddef>

namespace vloomdaw {

class Project;

/// The channel rack's rows: a mute light, the channel name and 16 step pads
/// per channel, for the current pattern. Reads the Project; edits are
/// emitted as signals for MainWindow to apply.
///
/// Click a pad to toggle it and drag along the row to paint the same state;
/// right-click to clear. Click a name to select the channel, double-click it
/// to open the piano roll.
class ChannelRackView : public QWidget {
    Q_OBJECT

public:
    explicit ChannelRackView(QWidget* parent = nullptr);

    void setProject(const Project* project);
    void setPattern(std::size_t pattern);
    void setSelectedChannel(std::size_t channel);
    /// Step lit as the playhead (-1: none).
    void setPlayStep(int step);
    /// Re-reads the project (after an edit).
    void refresh();

    [[nodiscard]] QSize sizeHint() const override;

signals:
    void stepSet(std::size_t channel, int step, bool on);
    void muteToggled(std::size_t channel);
    void channelSelected(std::size_t channel);
    void channelOpened(std::size_t channel);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    enum class Part { None, Mute, Name, Step };
    struct Hit {
        Part part = Part::None;
        std::size_t channel = 0;
        int step = -1;
    };
    [[nodiscard]] Hit hitAt(const QPoint& pos) const;
    [[nodiscard]] QRect padRect(std::size_t channel, int step) const;
    [[nodiscard]] std::size_t channelCount() const;

    const Project* project_ = nullptr;
    std::size_t pattern_ = 0;
    std::size_t selected_ = 0;
    int playStep_ = -1;

    // Paint-drag along one row.
    bool painting_ = false;
    bool paintState_ = false;
    std::size_t paintChannel_ = 0;
    int lastPaintedStep_ = -1;
};

} // namespace vloomdaw
