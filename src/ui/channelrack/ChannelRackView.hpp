// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QStringList>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace vloomdaw {

class Project;

/// The channel rack's rows: a mute light, a volume knob, the mixer insert,
/// the channel name and the pattern's step pads per channel (16 per bar),
/// for the current pattern. A "+" after the last pad asks for a bar more.
/// Reads the Project; edits are emitted as signals for MainWindow to apply.
///
/// Volume knob: drag up / down (Shift: fine), scroll, double-click for unity.
/// Pan knob, beside it: the same, double-click for the centre.
/// Insert box: click to pick the mixer insert the channel plays into, or
/// scroll; "-" is the master.
///
/// Click a pad to toggle it and drag along the row to paint the same state;
/// right-click to clear. Click a name to select the channel, double-click it
/// to open the piano roll. Drop a WAV file (from the browser or the desktop)
/// on a channel to give it that sample, or below the last one for a new
/// channel.
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
    /// Names for the insert menu: [0] the master, [i] insert i.
    void setInsertNames(const QStringList& names);

    [[nodiscard]] QSize sizeHint() const override;

signals:
    void stepSet(std::size_t channel, int step, bool on);
    void muteToggled(std::size_t channel);
    void channelSelected(std::size_t channel);
    void channelOpened(std::size_t channel);
    /// A WAV file was dropped on `channel`; channel == the channel count
    /// means below the last one (a new channel). `path` is a local path.
    void sampleDropped(std::size_t channel, const QString& path);
    /// Plugin `plugin` (an index into the plugin store) was dragged from the
    /// Plugins panel onto `channel`; channel == the channel count means a
    /// new channel.
    void pluginDropped(std::size_t channel, std::size_t plugin);
    /// The volume knob moved: fader position 0..1 (0.8 = unity).
    void volumeChanged(std::size_t channel, double volume);
    /// The pan knob moved: -1 left .. +1 right.
    void panChanged(std::size_t channel, double pan);
    /// The channel was routed to mixer insert `insert` (0 = master).
    void insertChanged(std::size_t channel, int insert);
    /// The user clicked the "+" after the last pad: one bar of pads more.
    void addBarRequested();
    /// A colour (0xRRGGBB) was picked for `channel` (right-click its name); nullopt: none.
    void channelColorChanged(std::size_t channel, std::optional<std::uint32_t> color);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool event(QEvent* event) override;

private:
    enum class Part { None, Mute, Volume, Pan, Insert, Name, Step, AddBar };
    struct Hit {
        Part part = Part::None;
        std::size_t channel = 0;
        int step = -1;
    };
    [[nodiscard]] Hit hitAt(const QPoint& pos) const;
    [[nodiscard]] QRect padRect(std::size_t channel, int step) const;
    [[nodiscard]] std::size_t channelCount() const;
    [[nodiscard]] int stepCount() const;
    /// The "+" cell after the last pad of `channel`'s row.
    [[nodiscard]] QRect addBarRect(std::size_t channel) const;
    /// Channel row at y, or channelCount() below the last row.
    [[nodiscard]] std::size_t dropRowAt(int y) const;
    void setDropRow(int row);
    void chooseInsert(std::size_t channel, const QPoint& at);
    [[nodiscard]] QString insertLabel(int insert) const;

    const Project* project_ = nullptr;
    int dropRow_ = -1; ///< row highlighted while a file is dragged over, -1 = none
    std::size_t pattern_ = 0;
    std::size_t selected_ = 0;
    int playStep_ = -1;

    QStringList insertNames_{QStringLiteral("Master")};

    // Volume or pan knob drag.
    Part draggedKnob_ = Part::None; ///< Volume, Pan or None
    std::size_t knobChannel_ = 0;
    double knobAtPress_ = 0.0;
    double pressY_ = 0.0;

    // Paint-drag along one row.
    bool painting_ = false;
    bool paintState_ = false;
    std::size_t paintChannel_ = 0;
    int lastPaintedStep_ = -1;
};

} // namespace vloomdaw
