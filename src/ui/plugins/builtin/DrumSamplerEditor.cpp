// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/DrumSamplerEditor.hpp"

#include "core/SampleView.hpp"
#include "ui/DroppedFiles.hpp"
#include "ui/plugins/SampleFiles.hpp"
#include "ui/plugins/builtin/ArcKnob.hpp"
#include "ui/plugins/builtin/ControlPanel.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"
#include "ui/plugins/builtin/ParameterControl.hpp"
#include "ui/plugins/builtin/Readout.hpp"
#include "ui/plugins/builtin/SampleParts.hpp"

#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace vloomdaw {

namespace {

namespace style = editor_style;
using Drums = vloom::dsp::DrumSampler;

constexpr int kWatchMs = 300; ///< how often the slots are looked at for samples loaded elsewhere
constexpr int kColumns = 4;
constexpr int kGap = 8; ///< between two pads
constexpr int kGridWidth = 416;
constexpr int kWaveHeight = 168;
constexpr double kHandle = 7.0; ///< the decay point's radius

std::uint32_t id(int pad, Drums::PadField field) { return Drums::padParameter(pad, field); }

/// How parameter `info` reads at `value` (a key: "C3"); empty for none.
QString choiceName(const vloom::ParameterInfo* info, double value)
{
    return info != nullptr ? ControlSpec::of(*info).text(value) : QString();
}

/// The presets: parameters only (a preset never changes the samples).
std::vector<std::pair<std::uint32_t, double>> everyPad(Drums::PadField field, double value)
{
    std::vector<std::pair<std::uint32_t, double>> values;
    for (int pad = 0; pad < Drums::kPads; ++pad)
        values.emplace_back(id(pad, field), value);
    return values;
}

/// The right column's box of knobs, as the design draws it.
class KnobBox final : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(style::line(), 1.0));
        p.setBrush(style::faint());
        p.drawRoundedRect(
            QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), style::kPanelRadius, style::kPanelRadius);
    }
};

/// The whole display: the editor's background behind the pads and the pad's controls.
class DrumBody final : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.fillRect(rect(), style::background());
    }
};

} // namespace

/// The 16 pads, four by four.
class PadGrid final : public QWidget {
public:
    struct Pad {
        QString note; ///< its key: "C1"
        QString name; ///< its sample's name; empty: none
        bool missing = false; ///< its file could not be read
    };

    std::function<Pad(int)> padOf;
    std::function<void(int)> onSelect; ///< a pad was clicked (or chosen with the keys)
    std::function<void(int)> onLoad; ///< an empty pad was clicked, or Enter pressed on a pad
    std::function<void(int, const QStringList&)> onDrop; ///< files dropped on a pad

    PadGrid()
    {
        setAcceptDrops(true);
        setFocusPolicy(Qt::StrongFocus);
        setFixedWidth(kGridWidth);
        setMinimumHeight(4 * 60 + 3 * kGap);
        setAccessibleName(tr("Pads"));
        setToolTip(tr("Click a pad to choose it; drop samples on a pad to load them"));
    }

    void setSelected(int pad)
    {
        selected_ = pad;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        for (int pad = 0; pad < Drums::kPads; ++pad) {
            const QRectF box = padRect(pad);
            const bool selected = pad == selected_;
            const Pad state = padOf ? padOf(pad) : Pad{};
            if (selected) {
                p.setPen(QPen(style::accent(), 2.0));
                p.setBrush(style::faint().lighter(122)); // #333 on the default theme
                p.drawRoundedRect(box.adjusted(1, 1, -1, -1), 10, 10);
            } else {
                p.setPen(QPen(pad == dropAt_ ? style::accent() : style::line(), 1.0));
                p.setBrush(style::faint());
                p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
            }
            const QRectF inside = box.adjusted(9, 8, -9, -8);
            p.setFont(style::font(font(), 10, QFont::Normal, true));
            p.setPen(style::muted());
            p.drawText(inside, Qt::AlignLeft | Qt::AlignTop, state.note);
            const QFont nameFont = style::font(font(), 12, selected ? QFont::Bold : QFont::DemiBold);
            p.setFont(nameFont);
            QString name = state.name.isEmpty() ? tr("Empty") : state.name;
            if (state.missing)
                name = tr("Missing: %1").arg(name);
            p.setPen(state.name.isEmpty() || state.missing ? style::muted() : style::text());
            p.drawText(inside, Qt::AlignLeft | Qt::AlignBottom,
                QFontMetrics(nameFont).elidedText(name, Qt::ElideRight, static_cast<int>(inside.width())));
        }
        if (hasFocus()) {
            p.setPen(QPen(style::accent(), 1.0, Qt::DotLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(padRect(selected_).adjusted(-3, -3, 3, 3), 12, 12);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        const int pad = padAt(event->position());
        if (pad < 0)
            return;
        if (onSelect)
            onSelect(pad);
        if (padOf && padOf(pad).name.isEmpty() && onLoad)
            onLoad(pad);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        // A second click on a pad with a sample: replace it.
        const int pad = padAt(event->position());
        if (pad >= 0 && padOf && !padOf(pad).name.isEmpty() && onLoad)
            onLoad(pad);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        int next = selected_;
        switch (event->key()) {
        case Qt::Key_Left:
            next = selected_ - 1;
            break;
        case Qt::Key_Right:
            next = selected_ + 1;
            break;
        case Qt::Key_Up:
            next = selected_ - kColumns;
            break;
        case Qt::Key_Down:
            next = selected_ + kColumns;
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (onLoad)
                onLoad(selected_);
            return;
        default:
            QWidget::keyPressEvent(event);
            return;
        }
        if (next >= 0 && next < Drums::kPads && onSelect)
            onSelect(next);
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (!droppedAudio(event->mimeData()).isEmpty())
            event->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent* event) override
    {
        const int pad = padAt(event->position());
        if (pad != dropAt_) {
            dropAt_ = pad;
            update();
        }
        if (pad >= 0)
            event->acceptProposedAction();
        else
            event->ignore();
    }
    void dragLeaveEvent(QDragLeaveEvent* /*event*/) override
    {
        dropAt_ = -1;
        update();
    }
    void dropEvent(QDropEvent* event) override
    {
        const int pad = padAt(event->position());
        dropAt_ = -1;
        update();
        const QStringList files = droppedAudioFiles(event->mimeData());
        if (pad < 0 || files.isEmpty())
            return;
        event->acceptProposedAction();
        if (onDrop)
            onDrop(pad, files);
    }

private:
    [[nodiscard]] QRectF padRect(int pad) const
    {
        const double w = (width() - (kColumns - 1) * kGap) / static_cast<double>(kColumns);
        const int rows = Drums::kPads / kColumns;
        const double h = (height() - (rows - 1) * kGap) / static_cast<double>(rows);
        const int column = pad % kColumns;
        const int row = pad / kColumns;
        return {column * (w + kGap), row * (h + kGap), w, h};
    }
    [[nodiscard]] int padAt(QPointF point) const
    {
        for (int pad = 0; pad < Drums::kPads; ++pad)
            if (padRect(pad).contains(point))
                return pad;
        return -1;
    }

    int selected_ = 0;
    int dropAt_ = -1;
};

/// The chosen pad's sample, with its decay over it.
class PadWaveform final : public QWidget {
public:
    using Value = std::function<double(std::uint32_t)>;

    std::function<void(double)> onDecay; ///< the point was dragged: the decay in ms
    std::function<void()> onDecayReset;
    std::function<void(const QStringList&)> onDrop;

    explicit PadWaveform(Value value)
        : value_(std::move(value))
    {
        setAcceptDrops(true);
        setMouseTracking(true);
        setFixedHeight(kWaveHeight);
        setAccessibleName(tr("The pad's sample with its decay"));
    }

    void setPad(int pad, const vloom::SampleSlotInfo& info,
        std::shared_ptr<const SampleWaveforms::Waveform> waveform, bool loading)
    {
        pad_ = pad;
        loaded_ = info.loaded();
        seconds_ = info.loaded() && info.sampleRate > 0.0 ? static_cast<double>(info.frames) / info.sampleRate
                                                          : 0.0;
        missing_ = !info.error.empty();
        waveform_ = std::move(waveform);
        loading_ = loading;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(frame, 10, 10);
        p.setClipPath(clip);
        p.fillRect(rect(), style::background());
        const QRectF area = waveArea();
        p.fillRect(QRectF(0, area.center().y() - 0.5, width(), 1), style::line());
        if (waveform_ != nullptr) {
            QColor wave = style::text();
            wave.setAlphaF(0.7f);
            paintWaveform(
                p, area, waveform_->peaks, value_(id(pad_, Drums::kReverse)) >= 0.5, wave, 2.0, 1.2);
            // Before Start does not play: dimmed.
            const double start = area.left() + area.width() * value_(id(pad_, Drums::kStart)) / 100.0;
            QColor shade = style::background();
            shade.setAlphaF(0.55f);
            p.fillRect(QRectF(QPointF(area.left(), area.top()), QPointF(start, area.bottom())), shade);
            paintDecay(p, area, start);
        } else {
            p.setFont(style::font(font(), 11));
            p.setPen(style::muted());
            const QString message = loading_ ? tr("Reading the waveform…")
                : missing_                   ? tr("The pad's file is missing")
                : loaded_                    ? tr("No waveform for this file")
                                             : tr("Drop a sample here, or click Load");
            p.drawText(area, Qt::AlignCenter, message);
        }
        p.setClipping(false);
        p.setPen(QPen(dropping_ ? style::accent() : style::faint(), dropping_ ? 2.0 : 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(frame, 10, 10);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        dragging_ = event->button() == Qt::LeftButton && nearHandle(event->position());
        if (dragging_)
            dragTo(event->position().x());
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (dragging_)
            dragTo(event->position().x());
        else
            setCursor(nearHandle(event->position()) ? Qt::SizeHorCursor : Qt::ArrowCursor);
    }
    void mouseReleaseEvent(QMouseEvent* /*event*/) override { dragging_ = false; }
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (nearHandle(event->position()) && onDecayReset)
            onDecayReset();
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (droppedAudio(event->mimeData()).isEmpty())
            return;
        event->acceptProposedAction();
        dropping_ = true;
        update();
    }
    void dragLeaveEvent(QDragLeaveEvent* /*event*/) override
    {
        dropping_ = false;
        update();
    }
    void dropEvent(QDropEvent* event) override
    {
        dropping_ = false;
        update();
        const QStringList files = droppedAudioFiles(event->mimeData());
        if (files.isEmpty())
            return;
        event->acceptProposedAction();
        if (onDrop)
            onDrop(files);
    }

private:
    [[nodiscard]] QRectF waveArea() const { return QRectF(rect()).adjusted(0, 10, 0, -10); }

    /// The pad plays at this speed against the sample's own (its tune).
    [[nodiscard]] double speed() const
    {
        return playbackSpeed(value_(id(pad_, Drums::kTune)), value_(id(pad_, Drums::kFine)));
    }
    [[nodiscard]] bool decays() const { return value_(id(pad_, Drums::kDecay)) < Drums::kNoDecayMs; }

    /// The decay's point: where it is down to 1/e (a fifth of the decay time),
    /// as the design places it.
    [[nodiscard]] std::optional<QPointF> handle() const
    {
        if (waveform_ == nullptr || seconds_ <= 0.0)
            return std::nullopt;
        const QRectF area = waveArea();
        const double start = area.left() + area.width() * value_(id(pad_, Drums::kStart)) / 100.0;
        if (!decays())
            return QPointF(area.right() - kHandle - 2.0, levelY(1.0));
        const double t = value_(id(pad_, Drums::kDecay)) / 1000.0 / 5.0;
        const double x = start + t * speed() / seconds_ * area.width();
        return QPointF(std::min(x, area.right() - kHandle - 2.0), levelY(std::exp(-1.0)));
    }
    [[nodiscard]] bool nearHandle(QPointF point) const
    {
        const auto at = handle();
        return at && std::hypot(point.x() - at->x(), point.y() - at->y()) <= kHandle + 4.0;
    }
    [[nodiscard]] double levelY(double level) const
    {
        const QRectF area = waveArea();
        return area.bottom() - level * area.height();
    }

    void dragTo(double x)
    {
        if (!onDecay || seconds_ <= 0.0)
            return;
        const QRectF area = waveArea();
        const double start = area.left() + area.width() * value_(id(pad_, Drums::kStart)) / 100.0;
        const double t = std::max(0.0, x - start) / area.width() * seconds_ / speed();
        onDecay(std::clamp(t * 5.0 * 1000.0, 1.0, Drums::kNoDecayMs));
    }

    void paintDecay(QPainter& p, const QRectF& area, double start)
    {
        if (seconds_ <= 0.0)
            return;
        const double decay = value_(id(pad_, Drums::kDecay)) / 1000.0;
        const double sustain = decays() ? 0.0 : 1.0;
        QPainterPath path(QPointF(start, levelY(1.0)));
        for (double x = start + 1.0; x <= area.right() + 0.5; x += 1.0) {
            const double t = (x - start) / area.width() * seconds_ / speed();
            path.lineTo(x, levelY(envelopeLevel(t, 0.0, decay, sustain)));
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(style::accent(), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        if (const auto at = handle()) {
            p.setPen(QPen(style::accent(), 2.5));
            p.setBrush(style::background());
            p.drawEllipse(*at, kHandle, kHandle);
        }
    }

    Value value_;
    int pad_ = 0;
    bool loaded_ = false;
    bool missing_ = false;
    bool loading_ = false;
    double seconds_ = 0.0;
    std::shared_ptr<const SampleWaveforms::Waveform> waveform_;
    bool dragging_ = false;
    bool dropping_ = false;
};

DrumSamplerEditor::DrumSamplerEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
    : BuiltinEditorWindow(std::move(instance), title, sampleRate,
          {
              {tr("Tight Kit"), everyPad(Drums::kDecay, 250.0)},
              {tr("Hi-hat Choke"),
                  {{id(4, Drums::kChoke), 1.0}, {id(5, Drums::kChoke), 1.0}, {id(6, Drums::kChoke), 1.0}}},
              {tr("Gated Pads"),
                  [] {
                      auto values = everyPad(Drums::kMode, 1.0);
                      const auto release = everyPad(Drums::kRelease, 60.0);
                      values.insert(values.end(), release.begin(), release.end());
                      return values;
                  }()},
              {tr("Soft Hands"), everyPad(Drums::kVelocity, 40.0)},
          },
          parent)
{
    resize(900, 520); // the design's size
    page_.fill(-1);
    panel()->hide(); // the pad's knobs sit in the right column, as the design has them

    waveforms_ = new SampleWaveforms(Drums::kPads, this);
    connect(waveforms_, &SampleWaveforms::changed, this, [this](int slot) {
        grid_->update();
        if (slot == selected_)
            refreshSelected();
    });

    auto* body = new DrumBody;
    auto* row = new QHBoxLayout(body);
    row->setContentsMargins(16, 16, 16, 16);
    row->setSpacing(16);

    grid_ = new PadGrid;
    grid_->padOf = [this](int pad) {
        const vloom::SampleSlotInfo& info = slots_[static_cast<std::size_t>(pad)];
        const QString path = QString::fromStdString(info.path);
        return PadGrid::Pad{.note = choiceName(parameter(id(pad, Drums::kKey)), value(id(pad, Drums::kKey))),
            .name = path.isEmpty() ? QString() : QFileInfo(path).completeBaseName(),
            .missing = !info.error.empty()};
    };
    grid_->onSelect = [this](int pad) { select(pad); };
    grid_->onLoad = [this](int pad) { chooseFile(pad); };
    grid_->onDrop = [this](int pad, const QStringList& files) {
        select(pad);
        load(pad, files);
    };
    row->addWidget(grid_);

    auto* column = new QVBoxLayout;
    column->setSpacing(12);
    auto* heading = new QHBoxLayout;
    heading->setSpacing(8);
    name_ = new QLabel;
    name_->setFont(style::font(font(), 14, QFont::Bold));
    heading->addWidget(name_);
    where_ = new QLabel;
    where_->setFont(style::font(font(), 11));
    where_->setStyleSheet(QStringLiteral("color: %1").arg(style::muted().name()));
    heading->addWidget(where_);
    heading->addStretch(1);
    loadButton_ = new SmallButton(tr("Load"));
    loadButton_->setToolTip(tr("Load a sample into this pad"));
    connect(loadButton_, &QAbstractButton::clicked, this, [this] { chooseFile(selected_); });
    heading->addWidget(loadButton_);
    clearButton_ = new SmallButton(tr("Clear"));
    clearButton_->setToolTip(tr("Remove this pad's sample"));
    connect(clearButton_, &QAbstractButton::clicked, this, [this] { clearPad(selected_); });
    heading->addWidget(clearButton_);
    column->addLayout(heading);

    wave_ = new PadWaveform([this](std::uint32_t parameterId) { return value(parameterId); });
    wave_->onDecay = [this](double ms) { change(id(selected_, Drums::kDecay), ms); };
    wave_->onDecayReset = [this] {
        if (const vloom::ParameterInfo* info = parameter(id(selected_, Drums::kDecay)))
            change(info->id, info->defaultValue);
    };
    wave_->onDrop = [this](const QStringList& files) { load(selected_, files); };
    column->addWidget(wave_);

    auto* box = new KnobBox;
    auto* boxRow = new QHBoxLayout(box);
    boxRow->setContentsMargins(14, 12, 14, 12);
    knobs_ = new QStackedWidget;
    boxRow->addWidget(knobs_);
    column->addWidget(box);
    readouts_ = new QStackedWidget;
    column->addWidget(readouts_);
    column->addStretch(1);
    row->addLayout(column, 1);
    setDisplay(body);

    sample_ = readout(tr("Sample"));
    sample_->setCursor(Qt::PointingHandCursor);
    sample_->installEventFilter(this);
    footer()->addLeft(sample_);
    loaded_ = readout(tr("Pads loaded"));
    loaded_->setWidestText(tr("%1 of %2").arg(16).arg(16));
    footer()->addRight(loaded_);
    footer()->addRight(readout(Drums::kMasterGain, tr("Master")));

    // What the pads and the waveform show.
    for (int pad = 0; pad < Drums::kPads; ++pad) {
        bind(id(pad, Drums::kKey), [this](double) {
            grid_->update();
            refreshSelected();
        });
        for (const Drums::PadField field :
            {Drums::kTune, Drums::kFine, Drums::kDecay, Drums::kStart, Drums::kReverse})
            bind(id(pad, field), [this, pad](double) {
                if (pad == selected_)
                    wave_->update();
            });
    }

    watch_ = new QTimer(this);
    watch_->setInterval(kWatchMs);
    connect(watch_, &QTimer::timeout, this, [this] { refreshSlots(); });
    refreshSlots();
    select(0);
}

void DrumSamplerEditor::makeControls(int pad)
{
    if (page_[static_cast<std::size_t>(pad)] >= 0)
        return;
    // The knobs, as the design's row (its Cutoff has no parameter: Fine instead).
    auto* knobs = new QWidget;
    auto* knobRow = new QHBoxLayout(knobs);
    knobRow->setContentsMargins(0, 0, 0, 0);
    knobRow->setSpacing(0);
    ArcKnob* decay = knob(id(pad, Drums::kDecay), tr("Decay"));
    decay->setFormatter([](double ms) {
        return ms >= Drums::kNoDecayMs ? tr("Off") : QString::fromStdString(formatParameterValue("ms", ms));
    });
    decay->setToolTip(tr("How long the sound takes to die away; at its longest, it does not"));
    ArcKnob* pan = knob(id(pad, Drums::kPan), tr("Pan"));
    pan->setFormatter([](double percent) { return QString::fromStdString(panText(percent)); });
    const std::array<QWidget*, 5> row{knob(id(pad, Drums::kTune), tr("Pitch")),
        knob(id(pad, Drums::kFine), tr("Fine")), decay, pan, knob(id(pad, Drums::kGain), tr("Gain"))};
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (i > 0)
            knobRow->addStretch(1);
        knobRow->addWidget(row[i]);
    }
    knobs_->addWidget(knobs);

    // The readouts under them, as the design's line (Choke group, Velocity).
    auto* lines = new QWidget;
    auto* grid = new QGridLayout(lines);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(6);
    Readout* key = readout(id(pad, Drums::kKey), tr("Key"));
    key->setToolTip(tr("The key that plays this pad"));
    Readout* choke = readout(id(pad, Drums::kChoke), tr("Choke group"));
    choke->setFormatter(
        [](double group) { return group < 0.5 ? tr("None") : QString::number(std::lround(group)); });
    choke->setToolTip(tr("A hit cuts every sound of its group (an open hi-hat by a closed one)"));
    Readout* mode = readout(id(pad, Drums::kMode), tr("Mode"));
    mode->setWidestText(tr("One-shot"));
    mode->setToolTip(tr("One-shot: plays to the end; Gate: stops with its release when the key goes up"));
    PillToggle* reverse = toggle(id(pad, Drums::kReverse), tr("Reverse"), PillToggle::Layout::Inline);
    Readout* start = readout(id(pad, Drums::kStart), tr("Start"));
    Readout* release = readout(id(pad, Drums::kRelease), tr("Release"));
    release->setToolTip(tr("Gate mode: how long the sound takes to stop once the key goes up"));
    Readout* velocity = readout(id(pad, Drums::kVelocity), tr("Velocity"));
    velocity->setToolTip(tr("How much harder hits play louder"));
    grid->addWidget(key, 0, 0);
    grid->addWidget(choke, 0, 1);
    grid->addWidget(mode, 0, 2);
    grid->addWidget(reverse, 0, 3);
    grid->addWidget(start, 1, 0);
    grid->addWidget(release, 1, 1);
    grid->addWidget(velocity, 1, 2);
    grid->setColumnStretch(4, 1);
    readouts_->addWidget(lines);

    page_[static_cast<std::size_t>(pad)] = knobs_->count() - 1;
}

void DrumSamplerEditor::select(int pad)
{
    if (pad < 0 || pad >= Drums::kPads)
        return;
    selected_ = pad;
    makeControls(pad);
    knobs_->setCurrentIndex(page_[static_cast<std::size_t>(pad)]);
    readouts_->setCurrentIndex(page_[static_cast<std::size_t>(pad)]);
    grid_->setSelected(pad);
    refreshSelected();
}

void DrumSamplerEditor::refreshSelected()
{
    const vloom::SampleSlotInfo& info = slots_[static_cast<std::size_t>(selected_)];
    const QString path = QString::fromStdString(info.path);
    const QString padName = tr("Pad %1").arg(selected_ + 1);
    name_->setText(path.isEmpty()
            ? padName
            : name_->fontMetrics().elidedText(QFileInfo(path).completeBaseName(), Qt::ElideRight, 200));
    where_->setText(path.isEmpty()
            ? choiceName(parameter(id(selected_, Drums::kKey)), value(id(selected_, Drums::kKey)))
            : tr("%1 · %2").arg(padName,
                  choiceName(parameter(id(selected_, Drums::kKey)), value(id(selected_, Drums::kKey)))));
    loadButton_->setText(path.isEmpty() ? tr("Load") : tr("Replace"));
    loadButton_->updateGeometry();
    clearButton_->setVisible(!path.isEmpty());
    wave_->setPad(selected_, info, waveforms_->waveform(selected_), waveforms_->loading(selected_));

    QString file = path.isEmpty() ? tr("None (click to load)") : QFileInfo(path).fileName();
    if (!path.isEmpty() && !info.error.empty())
        file = tr("Missing: %1").arg(file);
    sample_->setText(sample_->fontMetrics().elidedText(file, Qt::ElideMiddle, 320));
    QString tip = path.isEmpty() ? tr("Click to load a sample into %1").arg(padName)
                                 : QDir::toNativeSeparators(path) + tr("\nClick to replace it");
    if (!info.error.empty())
        tip += QLatin1Char('\n') + QString::fromStdString(info.error);
    sample_->setToolTip(tip);
}

void DrumSamplerEditor::refreshSlots()
{
    vloom::AudioProcessor& processor = instance().processor();
    int loaded = 0;
    bool changed = false;
    for (int pad = 0; pad < Drums::kPads; ++pad) {
        vloom::SampleSlotInfo info = processor.sampleSlot(pad);
        vloom::SampleSlotInfo& known = slots_[static_cast<std::size_t>(pad)];
        if (info.path != known.path || info.frames != known.frames || info.error != known.error) {
            known = std::move(info);
            changed = true;
        }
        waveforms_->update(pad, known);
        if (known.loaded())
            ++loaded;
    }
    loaded_->setText(tr("%1 of %2").arg(loaded).arg(Drums::kPads));
    if (changed) {
        grid_->update();
        refreshSelected();
    }
}

void DrumSamplerEditor::load(int pad, const QStringList& paths)
{
    bool any = false;
    // Several files: this pad and the ones after it, one each.
    for (int i = 0; i < paths.size() && pad + i < Drums::kPads; ++i)
        any = loadSampleFile(instance().processor(), pad + i, paths[i], wave_) || any;
    refreshSlots();
    if (any)
        emit edited();
}

void DrumSamplerEditor::chooseFile(int pad)
{
    const QString file
        = chooseSampleFile(this, QString::fromStdString(slots_[static_cast<std::size_t>(pad)].path));
    if (!file.isEmpty())
        load(pad, {file});
}

void DrumSamplerEditor::clearPad(int pad)
{
    if (!instance().processor().clearSample(pad))
        return;
    refreshSlots();
    emit edited();
}

bool DrumSamplerEditor::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == sample_ && event->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        chooseFile(selected_);
        return true;
    }
    return BuiltinEditorWindow::eventFilter(watched, event);
}

void DrumSamplerEditor::showEvent(QShowEvent* event)
{
    BuiltinEditorWindow::showEvent(event);
    refreshSlots();
    watch_->start();
}

void DrumSamplerEditor::hideEvent(QHideEvent* event)
{
    BuiltinEditorWindow::hideEvent(event);
    watch_->stop();
}

} // namespace vloomdaw
