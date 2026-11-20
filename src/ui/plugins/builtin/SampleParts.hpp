// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/SampleView.hpp"

#include <QAbstractButton>
#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

class QColor;
class QPainter;
class QRectF;
class QThread;

namespace vloom {
struct SampleSlotInfo;
}

namespace vloomdaw {

/// The waveforms of the samples in a sampler's slots, for its editor.
///
/// The engine does not hand its sample data to the UI, so each one is read
/// again on a worker thread from the file its slot reports
/// (AudioProcessor::sampleSlot()): the same WAV the engine plays
/// (playableWav(), the decoded copy in the cache for other types), never a
/// file on a network path. Only peaks are kept (kColumns per sample).
class SampleWaveforms : public QObject {
    Q_OBJECT

public:
    struct Waveform {
        std::vector<PeakColumn> peaks;
        double seconds = 0.0; ///< the sample's length at its own rate
    };

    static constexpr std::size_t kColumns = 2048;

    /// For `count` slots.
    SampleWaveforms(int count, QObject* parent);
    ~SampleWaveforms() override; ///< waits for the file being read

    /// What slot `slot` holds now: reads its file when that changed (another
    /// path or length); forgets the waveform when it has none.
    void update(int slot, const vloom::SampleSlotInfo& info);
    /// The waveform of `slot`; null while it is read, or when there is none.
    [[nodiscard]] std::shared_ptr<const Waveform> waveform(int slot) const;
    /// True while the file of `slot` is being read.
    [[nodiscard]] bool loading(int slot) const;

signals:
    /// The waveform of `slot` came in or went.
    void changed(int slot);

private:
    struct Slot {
        std::string path;
        std::int64_t frames = 0;
        std::uint64_t request = 0; ///< the read whose result is wanted; 0: none
        std::shared_ptr<const Waveform> waveform;
    };

    std::vector<Slot> slots_;
    std::uint64_t requests_ = 0;
    QThread* worker_ = nullptr;
    QObject* reader_ = nullptr; ///< lives on the worker
};

/// Draws `peaks` (a whole sample) across `area` in upright strokes every
/// `spacing` px, `width` px wide, as the designs do; `reversed`: the end on
/// the left. Full scale fills the height.
void paintWaveform(QPainter& p, const QRectF& area, std::span<const PeakColumn> peaks, bool reversed,
    const QColor& colour, double spacing, double width);

/// A small framed text button of the sample editors ("Load", "Clear"), as
/// the designs draw their M and S buttons: 24 px high, a 6 px radius.
class SmallButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit SmallButton(const QString& text, QWidget* parent = nullptr);
    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace vloomdaw
