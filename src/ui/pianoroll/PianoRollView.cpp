// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/pianoroll/PianoRollView.hpp"

#include "core/NoteNames.hpp"
#include "ui/Theme.hpp"
#include "ui/widgets/ColorMenu/ColorMenu.hpp"
#include "ui/widgets/Timeline/Timeline.hpp"

#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QScrollBar>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr int kKeyboardWidth = 60;
constexpr int kKeyHeight = 14;
constexpr int kKeyCount = kHighestKey - kLowestKey + 1;
constexpr int kResizeEdge = 6; ///< pixels at a note's right end that resize it
constexpr int kLaneHeight = 64; ///< the velocity lane
constexpr int kLanePad = 6; ///< above the tallest stem
constexpr int kStemReach = 3; ///< pixels beside a stem that still set its velocity
constexpr double kMinPixelsPerBeat = 16.0;
constexpr double kMaxPixelsPerBeat = 256.0;

bool isBlackKey(int key)
{
    switch (pitchClassOf(key)) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

int snapDown(double tick, int grid)
{
    return grid <= 1 ? static_cast<int>(std::floor(tick)) : static_cast<int>(std::floor(tick / grid)) * grid;
}

int snapNearest(double tick, int grid)
{
    return grid <= 1 ? static_cast<int>(std::lround(tick))
                     : static_cast<int>(std::lround(tick / grid)) * grid;
}

QString formatKeyName(int key)
{
    const std::string_view pc = noteName(pitchClassOf(key));
    return QStringLiteral("%1%2")
        .arg(QLatin1StringView(pc.data(), static_cast<qsizetype>(pc.size())))
        .arg(key / kPitchClasses);
}

/// `base` with `amount` (0..1) of `tint` mixed in, as an opaque colour.
QColor blend(const QColor& base, const QColor& tint, double amount)
{
    const auto mix = [amount](int a, int b) { return static_cast<int>(std::lround(a + (b - a) * amount)); };
    return {mix(base.red(), tint.red()), mix(base.green(), tint.green()), mix(base.blue(), tint.blue())};
}

/// A note's colour: darker the softer it is played.
QColor shaded(const QColor& fill, int velocity)
{
    return fill.darker(100 + (kMaxVelocity - velocity) * 80 / kMaxVelocity);
}

/// Whether the piano roll acts on this key itself (so a menu shortcut must not take it).
bool isEditKey(const QKeyEvent& event)
{
    const Qt::KeyboardModifiers held = event.modifiers() & ~Qt::KeypadModifier;
    switch (event.key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
    case Qt::Key_Escape:
    case Qt::Key_Left:
    case Qt::Key_Right:
        return held == Qt::NoModifier;
    case Qt::Key_Up:
    case Qt::Key_Down:
        return held == Qt::NoModifier || held == Qt::ShiftModifier;
    case Qt::Key_A:
    case Qt::Key_C:
    case Qt::Key_X:
    case Qt::Key_V:
    case Qt::Key_D:
    case Qt::Key_Q:
    case Qt::Key_Y:
        return held == Qt::ControlModifier;
    case Qt::Key_Z:
        return held == Qt::ControlModifier || held == (Qt::ControlModifier | Qt::ShiftModifier);
    default:
        return false;
    }
}

} // namespace

PianoRollView::PianoRollView(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setObjectName(QStringLiteral("pianoRollView"));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    // paintEvent() covers every pixel it is asked for: skip the background.
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    horizontalScrollBar()->setSingleStep(32);
    verticalScrollBar()->setSingleStep(kKeyHeight);
}

void PianoRollView::setProject(const Project* project)
{
    project_ = project;
    refresh();
}

void PianoRollView::setTarget(std::size_t pattern, std::size_t channel)
{
    const bool changedTarget = (pattern != pattern_ || channel != channel_);
    if (changedTarget)
        selection_.clear();
    pattern_ = pattern;
    channel_ = channel;
    drag_ = Drag::None;
    dragChanged_ = false;
    sliceLine_.reset();
    sound(-1, 0);
    viewport()->unsetCursor();
    refresh();
    if (changedTarget)
        scrollToChannelNotes();
}

void PianoRollView::setKey(int root, Scale scale)
{
    root_ = root;
    scale_ = scale;
    viewport()->update();
}

void PianoRollView::setLaneMode(LaneMode mode, int cc)
{
    if (drag_ != Drag::None)
        endDrag();
    laneMode_ = mode;
    if (mode == LaneMode::Controller)
        laneCc_ = std::clamp(cc, 0, kMaxMidiControlNumber);
    viewport()->update();
}

void PianoRollView::setSnapTicks(int ticks)
{
    snapTicks_ = std::max(kMinNoteLength, ticks);
    viewport()->update();
}

void PianoRollView::setSnapEnabled(bool enabled)
{
    if (snapEnabled_ == enabled)
        return;
    snapEnabled_ = enabled;
    viewport()->update();
}

void PianoRollView::setTool(Tool tool)
{
    if (tool_ == tool)
        return;
    if (drag_ != Drag::None)
        endDrag();
    tool_ = tool;
    sliceLine_.reset();
    if (hover_)
        updateHoverCursor(*hover_);
    viewport()->update();
}

void PianoRollView::zoomBy(double factor)
{
    const QRect grid = gridRect();
    const int centerX = grid.left() + grid.width() / 2;
    const double anchor = tickAt(centerX);
    pixelsPerBeat_ = std::clamp(pixelsPerBeat_ * factor, kMinPixelsPerBeat, kMaxPixelsPerBeat);
    updateScrollBars();
    horizontalScrollBar()->setValue(static_cast<int>(anchor * pixelsPerTick()) - (centerX - kKeyboardWidth));
    viewport()->update();
}

void PianoRollView::setPlayhead(double tick)
{
    if (tick == playhead_)
        return;

    if (followPlayhead_ && tick >= 0.0 && drag_ == Drag::None) {
        const QRect grid = gridRect();
        if (grid.width() > 60) {
            const double px = tick * pixelsPerTick();
            const int curScroll = horizontalScrollBar()->value();
            if (px > curScroll + grid.width() * 0.92 || px < curScroll) {
                horizontalScrollBar()->setValue(std::max(0, static_cast<int>(px - grid.width() * 0.15)));
            }
        }
    }

    // Repaint only the strips under the old and new line, and only when the
    // line moves (a quarter pixel): a full repaint 30 times a second is the main cost.
    const timeline::Scale scale{
        horizontalScrollBar()->value() / pixelsPerTick(), pixelsPerTick(), kKeyboardWidth};
    const QRect area(0, 0, viewport()->width(), viewport()->height());
    if (!timeline::playheadMoved(area, scale, playhead_, tick)) {
        playhead_ = tick;
        return;
    }
    const QRect before = timeline::playheadRect(area, scale, playhead_);
    playhead_ = tick;
    viewport()->update(before);
    viewport()->update(timeline::playheadRect(area, scale, playhead_));
}

void PianoRollView::refresh()
{
    const auto* list = notes();
    const std::size_t count = list != nullptr ? list->size() : 0;
    if (count != expectedCount_) {
        // Not an edit of this view: the flags no longer name the same notes.
        selection_.clear();
        expectedCount_ = count;
    }
    selection_.resize(count, false);
    updateScrollBars();
    emitSelectionSummary();
    viewport()->update();
}

void PianoRollView::scrollToChannelNotes()
{
    const auto* list = notes();
    if (list == nullptr || list->empty())
        return;
    int low = kHighestKey;
    int high = kLowestKey;
    for (const Note& n : *list) {
        low = std::min(low, n.key);
        high = std::max(high, n.key);
    }
    if (low > high)
        return;
    const int midKey = (low + high) / 2;
    const int viewRows = std::max(8, gridRect().height() / kKeyHeight);
    const int topKey = std::clamp(midKey + viewRows / 2, kLowestKey, kHighestKey);
    scrolledToMiddle_ = true;
    verticalScrollBar()->setValue(std::max(0, (kHighestKey - topKey) * kKeyHeight));
}

void PianoRollView::emitSelectionSummary()
{
    const auto* list = notes();
    if (list == nullptr) {
        emit selectionSummaryChanged(0, QString());
        return;
    }
    int count = 0;
    int low = kHighestKey;
    int high = kLowestKey;
    for (std::size_t i = 0; i < list->size(); ++i) {
        if (!isSelected(selection_, i))
            continue;
        ++count;
        low = std::min(low, (*list)[i].key);
        high = std::max(high, (*list)[i].key);
    }
    if (count == 0) {
        emit selectionSummaryChanged(0, QString());
        return;
    }
    const QString pitchRange = (low == high)
        ? formatKeyName(low)
        : QStringLiteral("%1–%2").arg(formatKeyName(low), formatKeyName(high));
    const QString text = count == 1 ? pitchRange : tr("%1 notes · %2").arg(count).arg(pitchRange);
    emit selectionSummaryChanged(count, text);
}

const std::vector<Note>* PianoRollView::notes() const
{
    return project_ != nullptr ? project_->notes(pattern_, channel_) : nullptr;
}

const std::vector<ControlPoint>* PianoRollView::controls() const
{
    return project_ != nullptr ? project_->controls(pattern_, channel_) : nullptr;
}

bool PianoRollView::matchesActiveLane(const ControlPoint& pt) const
{
    switch (laneMode_) {
    case LaneMode::Velocity:
        return false;
    case LaneMode::PitchBend:
        return pt.kind == ControlKind::PitchBend;
    case LaneMode::Controller:
        return pt.kind == ControlKind::Controller && pt.number == laneCc_;
    case LaneMode::Pressure:
        return pt.kind == ControlKind::Pressure;
    }
    return false;
}

QString PianoRollView::activeLaneShortLabel() const
{
    switch (laneMode_) {
    case LaneMode::Velocity:
        return tr("Velocity");
    case LaneMode::PitchBend:
        return tr("Pitch");
    case LaneMode::Controller:
        if (laneCc_ == 1)
            return tr("Mod CC1");
        if (laneCc_ == 11)
            return tr("Expr 11");
        if (laneCc_ == 64)
            return tr("Sust 64");
        if (laneCc_ == 74)
            return tr("Cut 74");
        return tr("CC %1").arg(laneCc_);
    case LaneMode::Pressure:
        return tr("AfterTch");
    }
    return tr("Velocity");
}

double PianoRollView::pixelsPerTick() const { return pixelsPerBeat_ / kTicksPerBeat; }

int PianoRollView::lengthTicks() const
{
    // The pattern plus room to write past its end, at least four bars.
    const int pattern = project_ != nullptr ? project_->patternLength(pattern_) : kTicksPerBar;
    return std::max(4 * kTicksPerBar, pattern + 2 * kTicksPerBar);
}

QRect PianoRollView::rulerRect() const
{
    const QRect v = viewport()->rect();
    return {kKeyboardWidth, 0, std::max(0, v.width() - kKeyboardWidth), timeline::kRulerHeight};
}

QRect PianoRollView::laneRect() const
{
    const QRect v = viewport()->rect();
    // A short window keeps most of its height for the notes.
    const int height = std::min(kLaneHeight, std::max(0, (v.height() - timeline::kRulerHeight) / 3));
    return {kKeyboardWidth, v.height() - height, v.width() - kKeyboardWidth, height};
}

QRect PianoRollView::laneCornerRect() const
{
    const QRect lane = laneRect();
    return {0, lane.top(), kKeyboardWidth, lane.height()};
}

QRect PianoRollView::gridRect() const
{
    const QRect v = viewport()->rect();
    return {kKeyboardWidth, timeline::kRulerHeight, v.width() - kKeyboardWidth,
        v.height() - timeline::kRulerHeight - laneRect().height()};
}

double PianoRollView::tickAt(int x) const
{
    return (x - kKeyboardWidth + horizontalScrollBar()->value()) / pixelsPerTick();
}

int PianoRollView::keyAt(int y) const
{
    const int row = (y - timeline::kRulerHeight + verticalScrollBar()->value()) / kKeyHeight;
    return std::clamp(kHighestKey - row, kLowestKey, kHighestKey);
}

int PianoRollView::yOfKey(int key) const
{
    return timeline::kRulerHeight + (kHighestKey - key) * kKeyHeight - verticalScrollBar()->value();
}

QRectF PianoRollView::noteRect(const Note& note) const
{
    const double x = kKeyboardWidth + note.start * pixelsPerTick() - horizontalScrollBar()->value();
    return {x, static_cast<double>(yOfKey(note.key)), std::max(3.0, note.length * pixelsPerTick()),
        static_cast<double>(kKeyHeight)};
}

int PianoRollView::effectiveGrid(Qt::KeyboardModifiers mods) const
{
    if (!snapEnabled_ || mods.testFlag(Qt::AltModifier))
        return 1;
    if (mods.testFlag(Qt::ShiftModifier) && tool_ == Tool::Slice)
        return kTicksPerStep;
    return snapTicks_;
}

std::optional<std::pair<std::size_t, bool>> PianoRollView::noteAt(const QPoint& pos) const
{
    const auto* list = notes();
    if (list == nullptr || !gridRect().contains(pos))
        return std::nullopt;
    for (std::size_t i = list->size(); i-- > 0;) {
        const QRectF r = noteRect((*list)[i]);
        if (r.contains(pos))
            return std::pair{i, pos.x() >= r.right() - kResizeEdge};
    }
    return std::nullopt;
}

void PianoRollView::updateScrollBars()
{
    const QRect grid = gridRect();
    horizontalScrollBar()->setPageStep(grid.width());
    horizontalScrollBar()->setRange(
        0, std::max(0, static_cast<int>(lengthTicks() * pixelsPerTick()) - grid.width()));
    verticalScrollBar()->setPageStep(grid.height());
    verticalScrollBar()->setRange(0, std::max(0, kKeyCount * kKeyHeight - grid.height()));
}

void PianoRollView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void PianoRollView::showEvent(QShowEvent* event)
{
    QAbstractScrollArea::showEvent(event);
    updateScrollBars();
    if (!scrolledToMiddle_) {
        scrolledToMiddle_ = true;
        const auto* list = notes();
        if (list != nullptr && !list->empty())
            scrollToChannelNotes();
        else
            verticalScrollBar()->setValue((kHighestKey - 72) * kKeyHeight);
    }
}

void PianoRollView::hideEvent(QHideEvent* event)
{
    QAbstractScrollArea::hideEvent(event);
    if (drag_ == Drag::Scrub)
        emit playbackReleased();
    drag_ = Drag::None;
    sound(-1, 0); // no key left sounding behind a hidden editor
}

void PianoRollView::paintEvent(QPaintEvent* event)
{
    const Theme::Colors& c = Theme::colors();
    QPainter p(viewport());
    const QRect grid = gridRect();
    const QRect lane = laneRect();
    // Only what Qt asked for: a moving playhead repaints a thin strip.
    const QRect area = grid.intersected(event->rect());
    const timeline::Scale scale{
        horizontalScrollBar()->value() / pixelsPerTick(), pixelsPerTick(), kKeyboardWidth};
    // Opaque mixes (no per-pixel blending) for the root row and past the end.
    const QColor rootRow = blend(c.sidebar, c.accent, 0.07);
    const QColor rootRowOut = blend(c.rail, c.accent, 0.07);
    const auto* list = notes();
    // The channel's colour when it has one, else the pattern's.
    const auto& channelColor = project_ != nullptr && channel_ < project_->channels().size()
        ? project_->channels()[channel_].color
        : std::nullopt;
    const QColor fill = channelColor ? toQColor(*channelColor) : Theme::patternColor(pattern_);

    const int firstKey = std::min(kHighestKey, keyAt(grid.top()));
    const int lastKey = std::max(kLowestKey, keyAt(grid.bottom()));
    if (!area.isEmpty()) {
        // Rows: in-scale rows lighter than the rest; the root row tinted.
        const int top = std::min(kHighestKey, keyAt(area.top()));
        const int bottom = std::max(kLowestKey, keyAt(area.bottom()));
        for (int key = top; key >= bottom; --key) {
            const bool inScale = scaleContains(scale_, root_, key);
            const bool isRoot = pitchClassOf(key) == root_;
            const QColor rowFill = isRoot ? (inScale ? rootRow : rootRowOut) : (inScale ? c.sidebar : c.rail);
            const QRect row = QRect(area.left(), yOfKey(key), area.width(), kKeyHeight).intersected(area);
            p.fillRect(row, rowFill);
            const int lineY = yOfKey(key) + kKeyHeight - 1;
            if (lineY >= area.top() && lineY <= area.bottom())
                p.fillRect(
                    QRect(area.left(), lineY, area.width(), 1), pitchClassOf(key) == 0 ? c.border : c.rail);
        }
        const int finest = snapEnabled_ ? std::min(snapTicks_, kTicksPerStep) : kTicksPerBeat;
        timeline::paintGrid(p, area, scale, finest);

        // Shade past the pattern's end.
        if (project_ != nullptr) {
            const int endX = static_cast<int>(scale.x(project_->patternLength(pattern_)));
            const QRect past
                = QRect(endX, area.top(), area.right() - endX + 1, area.height()).intersected(area);
            if (!past.isEmpty())
                p.fillRect(past, QColor(c.base.red(), c.base.green(), c.base.blue(), 110));
        }
    }

    QFont noteFont = font();
    noteFont.setPointSizeF(noteFont.pointSizeF() > 0 ? noteFont.pointSizeF() * 0.72 : 7.0);
    noteFont.setWeight(QFont::DemiBold);

    // Notes: darker the softer they are played, outlined when selected, with pitch name label.
    if (list != nullptr && !area.isEmpty()) {
        p.save();
        p.setClipRect(area);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(noteFont);
        for (std::size_t i = 0; i < list->size(); ++i) {
            const Note& note = (*list)[i];
            const QRectF r = noteRect(note).adjusted(0.5, 1.0, -0.5, -1.0);
            if (!r.intersects(area))
                continue;
            const bool chosen = isSelected(selection_, i);
            const QColor body = shaded(fill, note.velocity);
            p.setPen(chosen ? QPen(c.text, 1.6) : QPen(fill.darker(160), 1));
            p.setBrush(chosen ? body.lighter(130) : body);
            p.drawRoundedRect(r, 2.2, 2.2);

            if (r.width() >= 24.0) {
                p.setPen(chosen ? c.base : (body.lightnessF() > 0.52 ? c.base : c.text));
                p.drawText(r.adjusted(3.5, -0.5, -5.0, 0.0), Qt::AlignLeft | Qt::AlignVCenter,
                    formatKeyName(note.key));
            }

            // Resize grip.
            if (r.width() > 12)
                p.fillRect(QRectF(r.right() - 3, r.top() + 3, 1, r.height() - 6), fill.darker(170));
        }

        // The box being dragged to select notes.
        if (drag_ == Drag::Box) {
            const QRect box = QRect(dragStart_, dragLast_).normalized().intersected(grid);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QPen(c.accent, 1, Qt::DashLine));
            p.setBrush(QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 46));
            p.drawRect(box.adjusted(0, 0, -1, -1));
        }

        // Slice tool cut line.
        if (sliceLine_) {
            const int high = std::max(sliceLine_->fromKey, sliceLine_->toKey);
            const int low = std::min(sliceLine_->fromKey, sliceLine_->toKey);
            const double sx
                = kKeyboardWidth + sliceLine_->tick * pixelsPerTick() - horizontalScrollBar()->value();
            const double y0 = yOfKey(high);
            const double y1 = yOfKey(low) + kKeyHeight;
            p.setPen(QPen(c.text, 1.5));
            p.drawLine(QPointF(std::floor(sx) + 0.5, y0), QPointF(std::floor(sx) + 0.5, y1));
            p.setPen(Qt::NoPen);
            p.setBrush(c.accent);
            QPolygonF tip;
            tip << QPointF(sx - 4.0, y0) << QPointF(sx + 4.0, y0) << QPointF(sx, y0 + 5.0);
            p.drawPolygon(tip);
        }
        p.restore();
    }

    timeline::paintPlayhead(p, grid, scale, playhead_);

    // The velocity / MIDI control lane along the bottom.
    const QRect laneArea = lane.intersected(event->rect());
    if (!laneArea.isEmpty()) {
        p.fillRect(laneArea, c.rail);
        p.fillRect(QRect(lane.left(), lane.top(), lane.width(), 1).intersected(laneArea), c.border);
        if (laneMode_ == LaneMode::Velocity) {
            if (list != nullptr) {
                p.save();
                p.setClipRect(laneArea);
                const int room = std::max(1, lane.height() - kLanePad);
                for (std::size_t i = 0; i < list->size(); ++i) {
                    const Note& note = (*list)[i];
                    const int x = static_cast<int>(std::lround(scale.x(note.start)));
                    if (x + 6 < laneArea.left() || x - 6 > laneArea.right())
                        continue;
                    const int height = std::max(2, room * note.velocity / kMaxVelocity);
                    const QColor stem = isSelected(selection_, i) ? c.text : shaded(fill, note.velocity);
                    p.fillRect(QRect(x, lane.bottom() - height + 1, 2, height), stem);
                    p.fillRect(QRect(x, lane.bottom() - height + 1, 6, 3), stem); // its flag
                }
                p.restore();
            }
        } else {
            p.save();
            p.setClipRect(laneArea);
            const int finest = snapEnabled_ ? std::min(snapTicks_, kTicksPerStep) : kTicksPerBeat;
            timeline::paintGrid(p, laneArea, scale, finest);

            const bool bipolar = (laneMode_ == LaneMode::PitchBend);
            const double topY = lane.top() + kLanePad;
            const double botY = lane.bottom() - 2;
            const double spanY = std::max(4.0, botY - topY);
            const double zeroY = bipolar ? (topY + spanY * 0.5) : botY;

            if (bipolar) {
                p.fillRect(QRect(laneArea.left(), static_cast<int>(std::lround(zeroY)), laneArea.width(), 1),
                    c.border);
            }

            const std::vector<ControlPoint>* allPts
                = (drag_ == Drag::ControlDraw || drag_ == Drag::ControlErase) ? &dragControls_ : controls();
            if (allPts != nullptr) {
                std::vector<ControlPoint> pts;
                pts.reserve(allPts->size());
                for (const ControlPoint& pt : *allPts) {
                    if (matchesActiveLane(pt))
                        pts.push_back(pt);
                }
                std::sort(pts.begin(), pts.end(),
                    [](const ControlPoint& a, const ControlPoint& b) { return a.start < b.start; });

                const auto yForVal = [&](double val) {
                    if (bipolar) {
                        const double norm = (1.0 - std::clamp(val, -1.0, 1.0)) * 0.5;
                        return topY + norm * spanY;
                    }
                    const double norm = 1.0 - std::clamp(val, 0.0, 1.0);
                    return topY + norm * spanY;
                };

                p.setRenderHint(QPainter::Antialiasing, true);
                const QColor areaTint(c.accent.red(), c.accent.green(), c.accent.blue(), 68);
                const int endLimitX = static_cast<int>(std::lround(scale.x(lengthTicks())));
                for (std::size_t i = 0; i < pts.size(); ++i) {
                    const double x0 = scale.x(pts[i].start);
                    const double x1 = (i + 1 < pts.size())
                        ? scale.x(pts[i + 1].start)
                        : std::max(x0 + 6.0, static_cast<double>(endLimitX));
                    if (x1 < laneArea.left() - 4.0 || x0 > laneArea.right() + 4.0)
                        continue;
                    const double y = yForVal(pts[i].value);
                    const double rectTop = std::min(y, zeroY);
                    const double rectH = std::max(1.0, std::abs(zeroY - y));
                    p.fillRect(QRectF(x0, rectTop, std::max(1.0, x1 - x0), rectH), areaTint);

                    p.setPen(QPen(c.accent, 1.5));
                    p.drawLine(QPointF(x0, y), QPointF(x1, y));
                    if (i + 1 < pts.size()) {
                        const double yNext = yForVal(pts[i + 1].value);
                        p.drawLine(QPointF(x1, y), QPointF(x1, yNext));
                    }
                    p.setPen(Qt::NoPen);
                    p.setBrush(c.text);
                    p.drawEllipse(QPointF(x0, y), 2.4, 2.4);
                }
            }
            p.restore();
        }
    }

    // Ruler.
    const QRect ruler = rulerRect();
    if (event->rect().intersects(ruler)) {
        timeline::paintRuler(p, ruler, scale);
        timeline::paintPlayheadHead(p, ruler, scale, playhead_);
    }

    // Keyboard (skipped when only the grid needs repainting).
    const QRect keys(0, timeline::kRulerHeight, kKeyboardWidth, grid.height());
    if (!event->rect().intersects(QRect(0, 0, kKeyboardWidth, viewport()->height())))
        return;
    p.save();
    p.setClipRect(keys);
    p.fillRect(keys, c.text);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() > 0 ? small.pointSizeF() * 0.75 : 7.0);
    p.setFont(small);
    for (int key = firstKey; key >= lastKey; --key) {
        const QRect row(0, yOfKey(key), kKeyboardWidth, kKeyHeight);
        if (isBlackKey(key)) {
            p.fillRect(QRect(row.left(), row.top(), kKeyboardWidth * 3 / 5, row.height()), c.base);
        } else {
            p.fillRect(QRect(row.left(), row.bottom(), row.width(), 1), c.textDim);
        }
        if (key == sounding_) // the key being played
            p.fillRect(
                row.adjusted(0, 0, 0, -1), QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 170));
        if (pitchClassOf(key) == 0) {
            p.setPen(c.base);
            // C5 is MIDI 60, as in FL Studio.
            p.drawText(row.adjusted(0, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter,
                QStringLiteral("C%1").arg(key / kPitchClasses));
        }
    }
    p.restore();
    p.fillRect(QRect(kKeyboardWidth - 1, keys.top(), 1, keys.height()), c.border);

    // The corners: above the keyboard, and beside the lane (lane mode selector).
    p.fillRect(QRect(0, 0, kKeyboardWidth, timeline::kRulerHeight), c.panel);
    if (lane.height() > 0) {
        const QRect corner = laneCornerRect();
        p.fillRect(corner, c.panel);
        p.fillRect(QRect(0, lane.top(), kKeyboardWidth, 1), c.border);
        p.fillRect(QRect(kKeyboardWidth - 1, lane.top(), 1, lane.height()), c.border);

        const QRect pill(
            3, lane.top() + 4, kKeyboardWidth - 7, std::min(19, std::max(14, lane.height() - 6)));
        const bool cornerHovered = hover_ && corner.contains(*hover_);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(cornerHovered ? c.textDim : c.border, 1.0));
        p.setBrush(cornerHovered ? c.raised : c.window);
        p.drawRoundedRect(QRectF(pill).adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);

        p.setFont(small);
        p.setPen(laneMode_ == LaneMode::Velocity ? c.text : c.accent);
        p.drawText(pill.adjusted(4, 0, -9, 0), Qt::AlignLeft | Qt::AlignVCenter, activeLaneShortLabel());

        QPolygonF chev;
        const double cx = pill.right() - 5.5;
        const double cy = pill.center().y() + 0.5;
        chev << QPointF(cx - 2.6, cy - 1.4) << QPointF(cx + 2.6, cy - 1.4) << QPointF(cx, cy + 2.0);
        p.setPen(Qt::NoPen);
        p.setBrush(c.textDim);
        p.drawPolygon(chev);
        p.restore();

        if (lane.height() >= 42) {
            p.setFont(small);
            p.setPen(c.textDim);
            const QString sub = (laneMode_ == LaneMode::PitchBend)
                ? tr("±Bend")
                : (laneMode_ == LaneMode::Velocity ? tr("0–127") : tr("Curve"));
            p.drawText(QRect(4, pill.bottom() + 2, kKeyboardWidth - 8, lane.bottom() - pill.bottom() - 3),
                Qt::AlignLeft | Qt::AlignTop, sub);
        }
    }
}

void PianoRollView::edit(const std::vector<Note>& next)
{
    expectedCount_ = next.size();
    selection_.resize(next.size(), false);
    emit notesChanged(next);
    updateScrollBars();
    emitSelectionSummary();
    viewport()->update();
}

void PianoRollView::commit(const std::vector<Note>& next)
{
    const auto* list = notes();
    if (list == nullptr || next == *list)
        return;
    edit(next);
    emit editFinished();
}

void PianoRollView::selectOnly(std::size_t index)
{
    selection_.assign(expectedCount_, false);
    if (index < selection_.size())
        selection_[index] = true;
    emitSelectionSummary();
}

void PianoRollView::sound(int key, int velocity)
{
    if (!auditionEnabled_ && key >= 0)
        key = -1;
    if (key == sounding_)
        return;
    const QRect keys(0, 0, kKeyboardWidth, viewport()->height());
    if (sounding_ >= 0)
        emit keyReleased(sounding_);
    sounding_ = key >= kLowestKey && key <= kHighestKey ? key : -1;
    if (sounding_ >= 0)
        emit keyPressed(sounding_, std::clamp(velocity, kMinVelocity, kMaxVelocity));
    viewport()->update(keys);
}

void PianoRollView::beginDrag(Drag drag, std::size_t index, const QPoint& pos)
{
    drag_ = drag;
    dragIndex_ = index;
    dragNote_ = index < dragNotes_.size() ? dragNotes_[index] : Note{};
    dragStart_ = pos;
    dragLast_ = pos;
    dragStartTick_ = tickAt(pos.x());
    dragStartKey_ = keyAt(pos.y());
    if (drag == Drag::Move)
        viewport()->setCursor(Qt::ClosedHandCursor);
    else if (drag == Drag::Resize)
        viewport()->setCursor(Qt::SizeHorCursor);
    else if (drag == Drag::Box || drag == Drag::Slice)
        viewport()->setCursor(Qt::CrossCursor);
    else if (drag == Drag::Scrub)
        viewport()->setCursor(Qt::SplitHCursor);
}

void PianoRollView::endDrag()
{
    if (drag_ == Drag::None)
        return;
    const Drag finishedDrag = drag_;
    drag_ = Drag::None;
    sound(-1, 0);
    QToolTip::hideText();
    viewport()->unsetCursor();

    if (finishedDrag == Drag::Scrub) {
        emit playbackReleased();
        return;
    }

    if (finishedDrag == Drag::Slice && sliceLine_) {
        const auto* list = notes();
        if (list != nullptr) {
            const std::vector<Note> cut
                = slicedNotes(*list, sliceLine_->tick, sliceLine_->fromKey, sliceLine_->toKey);
            sliceLine_.reset();
            if (cut != *list) {
                selection_.clear();
                commit(cut);
                return;
            }
        }
        sliceLine_.reset();
        viewport()->update();
        return;
    }

    if (finishedDrag == Drag::ControlDraw || finishedDrag == Drag::ControlErase) {
        if (dragControlChanged_)
            emit controlEditFinished();
        dragControlChanged_ = false;
        dragControls_.clear();
        refresh();
        return;
    }

    if (dragChanged_)
        emit editFinished();
    dragChanged_ = false;
    dragNotes_.clear();
    paintLastSlot_ = -1;
    refresh(); // the pattern may have grown
}

void PianoRollView::eraseAt(const QPoint& pos)
{
    const auto hit = noteAt(pos);
    if (!hit || hit->first >= dragNotes_.size())
        return;
    dragNotes_.erase(dragNotes_.begin() + static_cast<std::ptrdiff_t>(hit->first));
    selection_.clear(); // the flags named the notes by position
    dragChanged_ = true;
    edit(dragNotes_);
}

void PianoRollView::insertNoteAndBeginMove(const QPoint& pos)
{
    const auto* list = notes();
    if (list == nullptr || list->size() >= kMaxPatternNotes)
        return;
    const int grid = effectiveGrid(Qt::NoModifier);
    dragNotes_ = *list;
    dragNotes_.push_back(Project::clamped(
        Note{keyAt(pos.y()), std::max(0, snapDown(tickAt(pos.x()), grid)), lastLength_, lastVelocity_}));
    selection_.assign(dragNotes_.size(), false);
    selection_.back() = true;
    beginDrag(Drag::Move, dragNotes_.size() - 1, pos);
    dragChanged_ = true;
    edit(dragNotes_);
    sound(dragNote_.key, dragNote_.velocity);
}

void PianoRollView::paintNoteAt(const QPoint& pos, int grid)
{
    if (!gridRect().contains(pos) || dragNotes_.size() >= kMaxPatternNotes)
        return;
    const int stepLen = std::max(kMinNoteLength, grid <= 1 ? lastLength_ : grid);
    const int slot = std::max(0, static_cast<int>(std::floor(tickAt(pos.x()) / stepLen)));
    const int key = keyAt(pos.y());
    if (slot == paintLastSlot_)
        return;
    paintLastSlot_ = slot;
    const int start = slot * stepLen;

    // Skip if an identical note already occupies `(key, start)`.
    for (const Note& existing : dragNotes_) {
        if (existing.key == key && existing.start == start)
            return;
    }
    dragNotes_.push_back(Project::clamped(Note{key, start, stepLen, lastVelocity_}));
    selection_.assign(dragNotes_.size(), false);
    selection_.back() = true;
    dragChanged_ = true;
    edit(dragNotes_);
    sound(key, lastVelocity_);
}

void PianoRollView::setVelocityAt(const QPoint& from, const QPoint& to)
{
    const QRect lane = laneRect();
    if (lane.height() <= kLanePad)
        return;
    const int left = std::min(from.x(), to.x()) - kStemReach;
    const int right = std::max(from.x(), to.x()) + kStemReach;
    const double level = static_cast<double>(lane.bottom() - to.y()) / (lane.height() - kLanePad);
    const int velocity
        = std::clamp(static_cast<int>(std::lround(level * kMaxVelocity)), kMinVelocity, kMaxVelocity);
    // With a selection, only its notes: a chord's notes share one stem.
    const bool chosenOnly = anySelected(selection_);
    bool changed = false;
    for (std::size_t i = 0; i < dragNotes_.size(); ++i) {
        const double x
            = kKeyboardWidth + dragNotes_[i].start * pixelsPerTick() - horizontalScrollBar()->value();
        if (x < left || x > right || (chosenOnly && !isSelected(selection_, i))
            || dragNotes_[i].velocity == velocity)
            continue;
        dragNotes_[i].velocity = velocity;
        changed = true;
    }
    if (!changed)
        return;
    lastVelocity_ = velocity;
    dragChanged_ = true;
    edit(dragNotes_);
}

void PianoRollView::drawControlAt(const QPoint& from, const QPoint& to, Qt::KeyboardModifiers mods)
{
    const QRect lane = laneRect();
    if (lane.height() <= kLanePad || laneMode_ == LaneMode::Velocity)
        return;

    const int grid = effectiveGrid(mods);
    const int tFrom = std::max(0, snapDown(std::max(0.0, tickAt(from.x())), grid));
    const int tTo = std::max(0, snapDown(std::max(0.0, tickAt(to.x())), grid));
    const int tMin = std::min(tFrom, tTo);
    const int tMax = std::max(tFrom, tTo);

    const double topY = lane.top() + kLanePad;
    const double botY = lane.bottom() - 2;
    const double spanY = std::max(4.0, botY - topY);
    const double norm = std::clamp((botY - static_cast<double>(to.y())) / spanY, 0.0, 1.0);

    ControlKind kind = ControlKind::PitchBend;
    int cc = 0;
    double value = 0.0;
    if (laneMode_ == LaneMode::PitchBend) {
        kind = ControlKind::PitchBend;
        value = std::clamp(norm * 2.0 - 1.0, -1.0, 1.0);
        if (std::abs(value) < 0.025)
            value = 0.0;
    } else if (laneMode_ == LaneMode::Controller) {
        kind = ControlKind::Controller;
        cc = laneCc_;
        value = norm;
    } else {
        kind = ControlKind::Pressure;
        value = norm;
    }

    // Remove existing points of the active lane across the swept interval [tMin, tMax]
    // so dragging paints a clean continuous curve without leftover spikes in between.
    dragControls_.erase(std::remove_if(dragControls_.begin(), dragControls_.end(),
                            [this, tMin, tMax](const ControlPoint& pt) {
                                return matchesActiveLane(pt) && pt.start >= tMin && pt.start <= tMax;
                            }),
        dragControls_.end());

    if (dragControls_.size() >= kMaxPatternControls)
        return;
    dragControls_.push_back(Project::clamped(ControlPoint{tTo, kind, cc, value}));
    std::stable_sort(dragControls_.begin(), dragControls_.end(),
        [](const ControlPoint& a, const ControlPoint& b) { return a.start < b.start; });
    dragControlChanged_ = true;
    emit controlsChanged(dragControls_);
    viewport()->update();
}

void PianoRollView::eraseControlAt(const QPoint& from, const QPoint& to)
{
    if (laneMode_ == LaneMode::Velocity)
        return;
    const double reachTicks = std::max(1.0, 4.0 / pixelsPerTick());
    const int tMin
        = std::max(0, static_cast<int>(std::floor(std::min(tickAt(from.x()), tickAt(to.x())) - reachTicks)));
    const int tMax = static_cast<int>(std::ceil(std::max(tickAt(from.x()), tickAt(to.x())) + reachTicks));
    const std::size_t before = dragControls_.size();
    dragControls_.erase(std::remove_if(dragControls_.begin(), dragControls_.end(),
                            [this, tMin, tMax](const ControlPoint& pt) {
                                return matchesActiveLane(pt) && pt.start >= tMin && pt.start <= tMax;
                            }),
        dragControls_.end());
    if (dragControls_.size() == before)
        return;
    dragControlChanged_ = true;
    emit controlsChanged(dragControls_);
    viewport()->update();
}

void PianoRollView::openLaneMenu(const QPoint& globalPos)
{
    const auto* allPts = controls();
    const auto hasPoints = [allPts](ControlKind kind, int cc) {
        if (allPts == nullptr)
            return false;
        return std::any_of(allPts->begin(), allPts->end(), [kind, cc](const ControlPoint& pt) {
            return pt.kind == kind && (kind != ControlKind::Controller || pt.number == cc);
        });
    };

    QMenu menu(this);
    const auto addLaneItem = [&](const QString& title, LaneMode mode, int cc, bool hasData) {
        const QString text = hasData ? QStringLiteral("%1  •").arg(title) : title;
        QAction* act = menu.addAction(text, this, [this, mode, cc] { setLaneMode(mode, cc); });
        act->setCheckable(true);
        act->setChecked(laneMode_ == mode && (mode != LaneMode::Controller || laneCc_ == cc));
    };

    addLaneItem(tr("Note Velocity"), LaneMode::Velocity, 0, notes() != nullptr && !notes()->empty());
    menu.addSeparator();
    addLaneItem(tr("Pitch Bend"), LaneMode::PitchBend, 0, hasPoints(ControlKind::PitchBend, 0));
    addLaneItem(tr("Mod Wheel (CC 1)"), LaneMode::Controller, 1, hasPoints(ControlKind::Controller, 1));
    addLaneItem(tr("Expression (CC 11)"), LaneMode::Controller, 11, hasPoints(ControlKind::Controller, 11));
    addLaneItem(
        tr("Sustain Pedal (CC 64)"), LaneMode::Controller, 64, hasPoints(ControlKind::Controller, 64));
    addLaneItem(
        tr("Filter Cutoff (CC 74)"), LaneMode::Controller, 74, hasPoints(ControlKind::Controller, 74));
    addLaneItem(
        tr("Channel Pressure (Aftertouch)"), LaneMode::Pressure, 0, hasPoints(ControlKind::Pressure, 0));

    menu.addSeparator();
    menu.addAction(tr("Other MIDI CC (0–127)…"), this, [this] {
        bool ok = false;
        const int cc = QInputDialog::getInt(
            this, tr("Select MIDI CC"), tr("MIDI Controller Number (0–127):"), laneCc_, 0, 127, 1, &ok);
        if (ok)
            setLaneMode(LaneMode::Controller, cc);
    });

    if (laneMode_ != LaneMode::Velocity && allPts != nullptr) {
        const bool activeHasPts = std::any_of(
            allPts->begin(), allPts->end(), [this](const ControlPoint& pt) { return matchesActiveLane(pt); });
        if (activeHasPts) {
            menu.addSeparator();
            menu.addAction(tr("Clear %1 Curve").arg(activeLaneShortLabel()), this, [this] {
                const auto* cur = controls();
                if (cur == nullptr)
                    return;
                std::vector<ControlPoint> next;
                next.reserve(cur->size());
                for (const ControlPoint& pt : *cur) {
                    if (!matchesActiveLane(pt))
                        next.push_back(pt);
                }
                if (next.size() != cur->size()) {
                    emit controlsChanged(next);
                    emit controlEditFinished();
                    viewport()->update();
                }
            });
        }
    }

    menu.exec(globalPos);
}

void PianoRollView::updateHoverCursor(const QPoint& pos)
{
    if (rulerRect().contains(pos) || laneCornerRect().contains(pos)) {
        viewport()->setCursor(Qt::PointingHandCursor);
        return;
    }
    if (laneRect().contains(pos)) {
        viewport()->setCursor(laneMode_ == LaneMode::Velocity ? Qt::SizeVerCursor : Qt::CrossCursor);
        return;
    }
    if (!gridRect().contains(pos)) {
        viewport()->setCursor(Qt::ArrowCursor);
        return;
    }
    switch (tool_) {
    case Tool::Slice:
        viewport()->setCursor(Qt::CrossCursor);
        return;
    case Tool::Playback:
        viewport()->setCursor(Qt::SplitHCursor);
        return;
    case Tool::Delete:
        viewport()->setCursor(Qt::ForbiddenCursor);
        return;
    case Tool::Select:
    case Tool::Draw:
    case Tool::Paint: {
        const auto hit = noteAt(pos);
        viewport()->setCursor(hit ? (hit->second ? Qt::SizeHorCursor : Qt::OpenHandCursor) : Qt::ArrowCursor);
        return;
    }
    }
}

void PianoRollView::mousePressEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    const auto* list = notes();
    if (list == nullptr || drag_ != Drag::None)
        return;
    setFocus(Qt::MouseFocusReason);
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    dragChanged_ = false;

    // Clicking or dragging the top ruler (or using the Playback tool) scrubs playback.
    if (event->button() == Qt::LeftButton
        && (rulerRect().contains(pos) || (tool_ == Tool::Playback && gridRect().contains(pos)))) {
        dragNotes_.clear();
        beginDrag(Drag::Scrub, 0, pos);
        lastScrubTick_ = std::max(0.0, tickAt(pos.x()));
        setPlayhead(lastScrubTick_);
        emit playbackRequested(lastScrubTick_);
        return;
    }

    // Left piano keyboard plays notes audibly.
    if (event->button() == Qt::LeftButton && pos.x() < kKeyboardWidth && pos.y() >= timeline::kRulerHeight
        && pos.y() < gridRect().bottom()) {
        dragNotes_.clear();
        beginDrag(Drag::Keys, 0, pos);
        sound(keyAt(pos.y()), lastVelocity_);
        return;
    }

    // Bottom-left lane corner opens the Velocity / Pitch Bend / MIDI CC selector menu.
    if ((event->button() == Qt::LeftButton || event->button() == Qt::RightButton)
        && laneCornerRect().contains(pos)) {
        openLaneMenu(event->globalPosition().toPoint());
        return;
    }

    // Bottom lane edits note velocities or MIDI control curves.
    if (laneRect().contains(pos)) {
        if (laneMode_ == LaneMode::Velocity) {
            if (event->button() == Qt::LeftButton) {
                dragNotes_ = *list;
                beginDrag(Drag::Velocity, 0, pos);
                setVelocityAt(pos, pos);
            }
            return;
        }
        const auto* curControls = controls();
        dragControls_ = curControls != nullptr ? *curControls : std::vector<ControlPoint>{};
        dragControlChanged_ = false;
        if (event->button() == Qt::RightButton
            || (event->button() == Qt::LeftButton && tool_ == Tool::Delete)) {
            beginDrag(Drag::ControlErase, 0, pos);
            eraseControlAt(pos, pos);
            return;
        }
        if (event->button() == Qt::LeftButton) {
            beginDrag(Drag::ControlDraw, 0, pos);
            drawControlAt(pos, pos, event->modifiers());
            return;
        }
        return;
    }

    if (!gridRect().contains(pos))
        return;

    // Right-click (or Delete tool left-click) erases notes.
    if (event->button() == Qt::RightButton || (event->button() == Qt::LeftButton && tool_ == Tool::Delete)) {
        dragNotes_ = *list;
        beginDrag(Drag::Erase, 0, pos);
        eraseAt(pos);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    // Slice tool cuts notes across a vertical line.
    if (tool_ == Tool::Slice) {
        const int g = effectiveGrid(event->modifiers());
        const int cutTick = std::max(kMinNoteLength, snapNearest(tickAt(pos.x()), g));
        const int k = keyAt(pos.y());
        sliceLine_ = SliceLine{cutTick, k, k};
        dragNotes_.clear();
        beginDrag(Drag::Slice, 0, pos);
        viewport()->update();
        return;
    }

    const auto hit = noteAt(pos);
    if (hit) {
        std::size_t index = hit->first;
        if (ctrl) {
            selection_.resize(list->size(), false);
            selection_[index] = !selection_[index];
            emitSelectionSummary();
            viewport()->update();
            return;
        }
        if (!isSelected(selection_, index))
            selectOnly(index);
        dragNotes_ = *list;
        if (shift && !hit->second) {
            // Shift+drag on a note moves a duplicate of the selection.
            const std::vector<Note> copies = selectedNotes(dragNotes_, selection_);
            if (dragNotes_.size() + copies.size() <= kMaxPatternNotes) {
                std::size_t rank = 0;
                for (std::size_t i = 0; i < index; ++i)
                    rank += isSelected(selection_, i) ? 1 : 0;
                index = dragNotes_.size() + rank;
                selection_.assign(dragNotes_.size(), false);
                dragNotes_.insert(dragNotes_.end(), copies.begin(), copies.end());
                selection_.resize(dragNotes_.size(), true);
                emitSelectionSummary();
            }
        }
        beginDrag(hit->second ? Drag::Resize : Drag::Move, index, pos);
        sound(dragNote_.key, dragNote_.velocity);
        viewport()->update();
        return;
    }

    // Empty grid space:
    // - In Select mode (or when holding Ctrl in Draw/Paint mode): marquee box-selects notes!
    if (tool_ == Tool::Select || ctrl) {
        boxBase_ = (shift || (tool_ == Tool::Select && ctrl)) ? selection_ : NoteSelection{};
        if (!shift && !(tool_ == Tool::Select && ctrl))
            selection_.assign(list->size(), false);
        emitSelectionSummary();
        dragNotes_.clear();
        beginDrag(Drag::Box, 0, pos);
        viewport()->update();
        return;
    }

    if (tool_ == Tool::Paint) {
        dragNotes_ = *list;
        paintLastSlot_ = -1;
        beginDrag(Drag::Paint, 0, pos);
        paintNoteAt(pos, effectiveGrid(event->modifiers()));
        return;
    }

    // Draw tool: place a new note and drag it into place.
    insertNoteAndBeginMove(pos);
}

void PianoRollView::mouseDoubleClickEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (event->button() == Qt::LeftButton && laneMode_ == LaneMode::PitchBend && laneRect().contains(pos)) {
        if (drag_ == Drag::ControlDraw || drag_ == Drag::ControlErase)
            endDrag();
        const auto* curControls = controls();
        std::vector<ControlPoint> next = curControls != nullptr ? *curControls : std::vector<ControlPoint>{};
        const int grid = effectiveGrid(event->modifiers());
        const int t = std::max(0, snapDown(std::max(0.0, tickAt(pos.x())), grid));
        next.erase(
            std::remove_if(next.begin(), next.end(),
                [t](const ControlPoint& pt) { return pt.kind == ControlKind::PitchBend && pt.start == t; }),
            next.end());
        if (next.size() < kMaxPatternControls) {
            next.push_back(Project::clamped(ControlPoint{t, ControlKind::PitchBend, 0, 0.0}));
            std::stable_sort(next.begin(), next.end(),
                [](const ControlPoint& a, const ControlPoint& b) { return a.start < b.start; });
            emit controlsChanged(next);
            emit controlEditFinished();
            viewport()->update();
        }
        return;
    }
    if (event->button() == Qt::LeftButton && tool_ == Tool::Select && gridRect().contains(pos)) {
        if (const auto hit = noteAt(pos)) {
            // Double-clicking an existing note in Select mode deletes it cleanly.
            const auto* list = notes();
            if (list != nullptr && hit->first < list->size()) {
                NoteSelection del(list->size(), false);
                del[hit->first] = true;
                selection_.clear();
                commit(withoutNotes(*list, del));
                return;
            }
        } else {
            // Double-clicking empty space in Select mode inserts a new note!
            if (drag_ == Drag::Box)
                endDrag();
            insertNoteAndBeginMove(pos);
            endDrag();
            return;
        }
    }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

void PianoRollView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    const QRect corner = laneCornerRect();
    const bool wasInCorner = hover_ && corner.contains(*hover_);
    hover_ = pos;
    if (wasInCorner != corner.contains(pos))
        viewport()->update(corner);
    const auto* list = notes();
    if (drag_ == Drag::None || list == nullptr) {
        if (tool_ == Tool::Slice && gridRect().contains(pos)) {
            const int g = effectiveGrid(event->modifiers());
            const int cutTick = std::max(kMinNoteLength, snapNearest(tickAt(pos.x()), g));
            const int k = keyAt(pos.y());
            const SliceLine preview{cutTick, k, k};
            if (!sliceLine_ || sliceLine_->tick != preview.tick || sliceLine_->fromKey != preview.fromKey) {
                sliceLine_ = preview;
                viewport()->update();
            }
        } else if (sliceLine_) {
            sliceLine_.reset();
            viewport()->update();
        }
        updateHoverCursor(pos);
        return;
    }

    const int grid = effectiveGrid(event->modifiers());
    const double dt = tickAt(pos.x()) - dragStartTick_;
    const auto snapped = [grid](double tick) { return snapNearest(tick, grid); };

    switch (drag_) {
    case Drag::Move: {
        const int deltaKeys = keyAt(pos.y()) - dragStartKey_;
        const std::vector<Note> next
            = movedNotes(dragNotes_, selection_, snapped(dragNote_.start + dt) - dragNote_.start, deltaKeys);
        if (next != *list) {
            dragChanged_ = true;
            edit(next);
        }
        if (dragIndex_ < next.size()) {
            const Note& curNote = next[dragIndex_];
            sound(curNote.key, curNote.velocity);
            const int actualShift = curNote.key - dragNote_.key;
            if (actualShift != 0) {
                const QString sign = actualShift > 0 ? QStringLiteral("+") : QString();
                QToolTip::showText(event->globalPosition().toPoint(),
                    tr("Pitch: %1%2 st  ·  %3").arg(sign).arg(actualShift).arg(formatKeyName(curNote.key)),
                    viewport());
            } else {
                QToolTip::hideText();
            }
        }
        break;
    }
    case Drag::Resize: {
        const std::vector<Note> next
            = resizedNotes(dragNotes_, selection_, snapped(dragNote_.end() + dt) - dragNote_.end());
        if (dragIndex_ < next.size())
            lastLength_ = next[dragIndex_].length;
        if (next != *list) {
            dragChanged_ = true;
            edit(next);
        }
        break;
    }
    case Drag::Box: {
        const QRectF box = QRect(dragStart_, pos).normalized();
        selection_ = boxBase_;
        selection_.resize(list->size(), false);
        for (std::size_t i = 0; i < list->size(); ++i)
            if (noteRect((*list)[i]).intersects(box))
                selection_[i] = true;
        emitSelectionSummary();
        viewport()->update();
        break;
    }
    case Drag::Erase:
        eraseAt(pos);
        break;
    case Drag::Paint:
        paintNoteAt(pos, grid);
        break;
    case Drag::Slice:
        if (sliceLine_) {
            sliceLine_->toKey = keyAt(pos.y());
            viewport()->update();
        }
        break;
    case Drag::Scrub: {
        const double tick = std::max(0.0, tickAt(pos.x()));
        if (std::abs(tick - lastScrubTick_) * pixelsPerTick() >= 2.5) {
            lastScrubTick_ = tick;
            setPlayhead(tick);
            emit playbackRequested(tick);
        }
        break;
    }
    case Drag::Velocity:
        setVelocityAt(dragLast_, pos);
        break;
    case Drag::ControlDraw:
        drawControlAt(dragLast_, pos, event->modifiers());
        break;
    case Drag::ControlErase:
        eraseControlAt(dragLast_, pos);
        break;
    case Drag::Keys:
        sound(pos.y() >= timeline::kRulerHeight && pos.y() < gridRect().bottom() ? keyAt(pos.y()) : -1,
            lastVelocity_);
        break;
    case Drag::None:
        break;
    }
    dragLast_ = pos;
}

void PianoRollView::mouseReleaseEvent(QMouseEvent* /*event*/) { endDrag(); }

bool PianoRollView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::Leave) {
        const bool wasInCorner = hover_ && laneCornerRect().contains(*hover_);
        hover_.reset();
        if (wasInCorner)
            viewport()->update(laneCornerRect());
        if (drag_ == Drag::None && sliceLine_) {
            sliceLine_.reset();
            viewport()->update();
        }
    }
    return QAbstractScrollArea::viewportEvent(event);
}

bool PianoRollView::event(QEvent* event)
{
    // Its own editing keys come here, not to a menu with the same shortcut.
    if (event->type() == QEvent::ShortcutOverride && isEditKey(*static_cast<QKeyEvent*>(event))) {
        event->accept();
        return true;
    }
    return QAbstractScrollArea::event(event);
}

void PianoRollView::keyPressEvent(QKeyEvent* event)
{
    if (!isEditKey(*event) || drag_ != Drag::None) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelection();
        break;
    case Qt::Key_Escape:
        clearSelection();
        break;
    case Qt::Key_Left:
        nudge(-snapTicks_, 0);
        break;
    case Qt::Key_Right:
        nudge(snapTicks_, 0);
        break;
    case Qt::Key_Up:
        nudge(0, shift ? kPitchClasses : 1);
        break;
    case Qt::Key_Down:
        nudge(0, shift ? -kPitchClasses : -1);
        break;
    case Qt::Key_A:
        selectAll();
        break;
    case Qt::Key_C:
        copySelection();
        break;
    case Qt::Key_X:
        cutSelection();
        break;
    case Qt::Key_V:
        paste();
        break;
    case Qt::Key_D:
        duplicateSelection();
        break;
    case Qt::Key_Q:
        quantize();
        break;
    case Qt::Key_Y:
        emit redoRequested();
        break;
    case Qt::Key_Z:
        if (shift)
            emit redoRequested();
        else
            emit undoRequested();
        break;
    default:
        break;
    }
    event->accept();
}

void PianoRollView::nudge(int ticks, int keys)
{
    if (const auto* list = notes(); list != nullptr && hasSelection())
        commit(movedNotes(*list, selection_, ticks, keys));
}

void PianoRollView::transpose(int semitones)
{
    const auto* list = notes();
    if (list == nullptr || list->empty() || semitones == 0)
        return;
    const NoteSelection target = hasSelection() ? selection_ : NoteSelection(list->size(), true);
    commit(movedNotes(*list, target, 0, semitones));
}

void PianoRollView::applyLegato()
{
    const auto* list = notes();
    if (list == nullptr || list->empty())
        return;
    const NoteSelection target = hasSelection() ? selection_ : NoteSelection(list->size(), true);
    commit(legatoNotes(*list, target));
}

void PianoRollView::snapToScale()
{
    const auto* list = notes();
    if (list == nullptr || list->empty())
        return;
    const NoteSelection target = hasSelection() ? selection_ : NoteSelection(list->size(), true);
    commit(snappedToScaleNotes(*list, target, root_, scale_));
}

void PianoRollView::scaleVelocity(double factor)
{
    const auto* list = notes();
    if (list == nullptr || list->empty())
        return;
    const NoteSelection target = hasSelection() ? selection_ : NoteSelection(list->size(), true);
    commit(scaledVelocityNotes(*list, target, factor));
}

void PianoRollView::selectAll()
{
    selection_.assign(expectedCount_, true);
    emitSelectionSummary();
    viewport()->update();
}

void PianoRollView::clearSelection()
{
    selection_.assign(expectedCount_, false);
    emitSelectionSummary();
    viewport()->update();
}

void PianoRollView::copySelection()
{
    if (const auto* list = notes(); list != nullptr && hasSelection())
        clipboard_ = selectedNotes(*list, selection_);
}

void PianoRollView::cutSelection()
{
    copySelection();
    deleteSelection();
}

void PianoRollView::deleteSelection()
{
    const auto* list = notes();
    if (list == nullptr || !hasSelection())
        return;
    const std::vector<Note> next = withoutNotes(*list, selection_);
    selection_.clear();
    commit(next);
}

void PianoRollView::paste()
{
    const auto* list = notes();
    if (list == nullptr || clipboard_.empty())
        return;
    const int grid = effectiveGrid(Qt::NoModifier);
    const int at = hover_ && gridRect().contains(*hover_)
        ? snapDown(tickAt(hover_->x()), grid)
        : static_cast<int>(std::ceil(std::max(0.0, tickAt(kKeyboardWidth)) / kTicksPerBar)) * kTicksPerBar;
    const std::size_t before = list->size();
    const std::vector<Note> next = withPasted(*list, clipboard_, at);
    if (next.size() == before)
        return;
    selection_.assign(before, false);
    selection_.resize(next.size(), true);
    commit(next);
}

void PianoRollView::duplicateSelection()
{
    const auto* list = notes();
    const auto span = list != nullptr ? selectionSpan(*list, selection_) : std::nullopt;
    if (!span)
        return;
    const int length = (span->second - span->first + snapTicks_ - 1) / snapTicks_ * snapTicks_;
    const std::size_t before = list->size();
    const std::vector<Note> next = withPasted(*list, selectedNotes(*list, selection_), span->first + length);
    if (next.size() == before)
        return;
    selection_.assign(before, false);
    selection_.resize(next.size(), true);
    commit(next);
}

void PianoRollView::quantize()
{
    const auto* list = notes();
    if (list == nullptr)
        return;
    commit(
        quantizedNotes(*list, hasSelection() ? selection_ : NoteSelection(list->size(), true), snapTicks_));
}

void PianoRollView::scrollContentsBy(int /*dx*/, int /*dy*/) { viewport()->update(); }

void PianoRollView::wheelEvent(QWheelEvent* event)
{
    const bool overRuler
        = event->position().y() < timeline::kRulerHeight && event->position().x() >= kKeyboardWidth;

    // Alt + Mouse Wheel over selected notes adjusts their pitch directly!
    if (event->modifiers().testFlag(Qt::AltModifier) && !event->modifiers().testFlag(Qt::ControlModifier)
        && hasSelection() && gridRect().contains(event->position().toPoint())) {
        const int raw = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
        if (raw != 0) {
            const int unit = event->modifiers().testFlag(Qt::ShiftModifier) ? kPitchClasses : 1;
            transpose((raw > 0 ? 1 : -1) * unit);
            event->accept();
            return;
        }
    }

    if (event->modifiers().testFlag(Qt::ShiftModifier) && !event->modifiers().testFlag(Qt::ControlModifier)) {
        const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta);
        event->accept();
        return;
    }
    if (!event->modifiers().testFlag(Qt::ControlModifier) && !overRuler) {
        QAbstractScrollArea::wheelEvent(event);
        return;
    }
    // Ctrl+wheel (or mouse wheel directly over the timeline ruler): zoom around the mouse.
    const double steps = static_cast<double>(event->angleDelta().y()) / 120.0;
    if (std::abs(steps) < 1e-4)
        return;
    const int mouseX = static_cast<int>(event->position().x());
    const double anchor = tickAt(mouseX);
    pixelsPerBeat_ = std::clamp(pixelsPerBeat_ * std::pow(1.25, steps), kMinPixelsPerBeat, kMaxPixelsPerBeat);
    updateScrollBars();
    horizontalScrollBar()->setValue(static_cast<int>(anchor * pixelsPerTick()) - (mouseX - kKeyboardWidth));
    viewport()->update();
    event->accept();
}

} // namespace vloomdaw
