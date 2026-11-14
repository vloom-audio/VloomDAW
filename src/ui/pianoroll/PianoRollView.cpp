// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/pianoroll/PianoRollView.hpp"

#include "core/NoteNames.hpp"
#include "ui/Theme.hpp"
#include "ui/widgets/ColorMenu/ColorMenu.hpp"
#include "ui/widgets/Timeline/Timeline.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr int kKeyboardWidth = 60;
constexpr int kKeyHeight = 14;
constexpr int kKeyCount = kHighestKey - kLowestKey + 1;
constexpr int kResizeEdge = 6; ///< pixels at a note's right end that resize it
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

int snapDown(double tick, int grid) { return static_cast<int>(std::floor(tick / grid)) * grid; }

/// `base` with `amount` (0..1) of `tint` mixed in, as an opaque colour.
QColor blend(const QColor& base, const QColor& tint, double amount)
{
    const auto mix = [amount](int a, int b) { return static_cast<int>(std::lround(a + (b - a) * amount)); };
    return {mix(base.red(), tint.red()), mix(base.green(), tint.green()), mix(base.blue(), tint.blue())};
}
int snapNearest(double tick, int grid) { return static_cast<int>(std::lround(tick / grid)) * grid; }

} // namespace

PianoRollView::PianoRollView(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setObjectName(QStringLiteral("pianoRollView"));
    setFrameShape(QFrame::NoFrame);
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
    pattern_ = pattern;
    channel_ = channel;
    drag_ = Drag::None;
    refresh();
}

void PianoRollView::setKey(int root, Scale scale)
{
    root_ = root;
    scale_ = scale;
    viewport()->update();
}

void PianoRollView::setSnapTicks(int ticks)
{
    snapTicks_ = std::max(kMinNoteLength, ticks);
    viewport()->update();
}

void PianoRollView::setPlayhead(double tick)
{
    if (tick == playhead_)
        return;
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
    updateScrollBars();
    viewport()->update();
}

const std::vector<Note>* PianoRollView::notes() const
{
    return project_ != nullptr ? project_->notes(pattern_, channel_) : nullptr;
}

double PianoRollView::pixelsPerTick() const { return pixelsPerBeat_ / kTicksPerBeat; }

int PianoRollView::lengthTicks() const
{
    // The pattern plus room to write past its end, at least four bars.
    const int pattern = project_ != nullptr ? project_->patternLength(pattern_) : kTicksPerBar;
    return std::max(4 * kTicksPerBar, pattern + 2 * kTicksPerBar);
}

QRect PianoRollView::gridRect() const
{
    const QRect v = viewport()->rect();
    return {kKeyboardWidth, timeline::kRulerHeight, v.width() - kKeyboardWidth,
        v.height() - timeline::kRulerHeight};
}

double PianoRollView::tickAt(int x) const
{
    return (x - kKeyboardWidth + horizontalScrollBar()->value()) / pixelsPerTick();
}

int PianoRollView::keyAt(int y) const
{
    const int row = (y - timeline::kRulerHeight + verticalScrollBar()->value()) / kKeyHeight;
    return kHighestKey - row;
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
        // Start around middle C, where most parts are written.
        scrolledToMiddle_ = true;
        verticalScrollBar()->setValue((kHighestKey - 72) * kKeyHeight);
    }
}

void PianoRollView::paintEvent(QPaintEvent* event)
{
    const Theme::Colors& c = Theme::colors();
    QPainter p(viewport());
    const QRect grid = gridRect();
    // Only what Qt asked for: a moving playhead repaints a thin strip.
    const QRect area = grid.intersected(event->rect());
    const timeline::Scale scale{
        horizontalScrollBar()->value() / pixelsPerTick(), pixelsPerTick(), kKeyboardWidth};
    // Opaque mixes (no per-pixel blending) for the root row and past the end.
    const QColor rootRow = blend(c.sidebar, c.accent, 0.07);
    const QColor rootRowOut = blend(c.rail, c.accent, 0.07);

    const int firstKey = std::min(kHighestKey, keyAt(grid.top()));
    const int lastKey = std::max(kLowestKey, keyAt(grid.bottom()));
    if (!area.isEmpty()) {
        // Rows: in-scale rows lighter than the rest; the root row tinted.
        const int top = std::min(kHighestKey, keyAt(area.top()));
        const int bottom = std::max(kLowestKey, keyAt(area.bottom()));
        for (int key = top; key >= bottom; --key) {
            const bool inScale = scaleContains(scale_, root_, key);
            const bool isRoot = pitchClassOf(key) == root_;
            const QColor fill = isRoot ? (inScale ? rootRow : rootRowOut) : (inScale ? c.sidebar : c.rail);
            const QRect row = QRect(area.left(), yOfKey(key), area.width(), kKeyHeight).intersected(area);
            p.fillRect(row, fill);
            const int lineY = yOfKey(key) + kKeyHeight - 1;
            if (lineY >= area.top() && lineY <= area.bottom())
                p.fillRect(
                    QRect(area.left(), lineY, area.width(), 1), pitchClassOf(key) == 0 ? c.border : c.rail);
        }
        timeline::paintGrid(p, area, scale, std::min(snapTicks_, kTicksPerStep));

        // Shade past the pattern's end.
        if (project_ != nullptr) {
            const int endX = static_cast<int>(scale.x(project_->patternLength(pattern_)));
            const QRect past
                = QRect(endX, area.top(), area.right() - endX + 1, area.height()).intersected(area);
            if (!past.isEmpty())
                p.fillRect(past, QColor(c.base.red(), c.base.green(), c.base.blue(), 110));
        }
    }

    // Notes.
    if (const auto* list = notes(); list != nullptr && !area.isEmpty()) {
        p.save();
        p.setClipRect(area);
        p.setRenderHint(QPainter::Antialiasing);
        // The channel's colour when it has one, else the pattern's.
        const auto& channelColor
            = channel_ < project_->channels().size() ? project_->channels()[channel_].color : std::nullopt;
        const QColor fill = channelColor ? toQColor(*channelColor) : Theme::patternColor(pattern_);
        for (std::size_t i = 0; i < list->size(); ++i) {
            const QRectF r = noteRect((*list)[i]).adjusted(0.5, 1.0, -0.5, -1.0);
            if (!r.intersects(area))
                continue;
            p.setPen(QPen(fill.darker(160), 1));
            p.setBrush(drag_ != Drag::None && i == dragIndex_ ? fill.lighter(125) : fill);
            p.drawRoundedRect(r, 2, 2);
            // Resize grip.
            if (r.width() > 12)
                p.fillRect(QRectF(r.right() - 3, r.top() + 3, 1, r.height() - 6), fill.darker(170));
        }
        p.restore();
    }

    timeline::paintPlayhead(p, grid, scale, playhead_);

    // Ruler.
    const QRect ruler(kKeyboardWidth, 0, grid.width(), timeline::kRulerHeight);
    if (event->rect().intersects(ruler))
        timeline::paintRuler(p, ruler, scale);

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
        if (pitchClassOf(key) == 0) {
            p.setPen(c.base);
            // C5 is MIDI 60, as in FL Studio.
            p.drawText(row.adjusted(0, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter,
                QStringLiteral("C%1").arg(key / kPitchClasses));
        }
    }
    p.restore();
    p.fillRect(QRect(kKeyboardWidth - 1, keys.top(), 1, keys.height()), c.border);

    // Corner above the keyboard.
    p.fillRect(QRect(0, 0, kKeyboardWidth, timeline::kRulerHeight), c.panel);
}

void PianoRollView::beginDrag(std::size_t index, Drag drag, const QPoint& pos)
{
    const auto* list = notes();
    if (list == nullptr || index >= list->size())
        return;
    drag_ = drag;
    dragIndex_ = index;
    dragStartNote_ = (*list)[index];
    dragStartTick_ = tickAt(pos.x());
    dragStartKey_ = keyAt(pos.y());
    viewport()->setCursor(drag == Drag::Resize ? Qt::SizeHorCursor : Qt::ClosedHandCursor);
}

void PianoRollView::mousePressEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (project_ == nullptr || !gridRect().contains(pos))
        return;
    const auto hit = noteAt(pos);

    if (event->button() == Qt::RightButton) {
        if (hit)
            emit noteRemoved(hit->first);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    if (hit) {
        beginDrag(hit->first, hit->second ? Drag::Resize : Drag::Move, pos);
        return;
    }
    // New note at the clicked step, then drag it into place.
    const Note note{keyAt(pos.y()), snapDown(tickAt(pos.x()), snapTicks_), lastLength_};
    emit noteAdded(note);
    if (const auto* list = notes(); list != nullptr && !list->empty())
        beginDrag(list->size() - 1, Drag::Move, pos);
}

void PianoRollView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (drag_ == Drag::None) {
        const auto hit = noteAt(pos);
        viewport()->setCursor(hit ? (hit->second ? Qt::SizeHorCursor : Qt::OpenHandCursor) : Qt::ArrowCursor);
        return;
    }
    const bool free = event->modifiers().testFlag(Qt::AltModifier);
    const double dt = tickAt(pos.x()) - dragStartTick_;
    Note note = dragStartNote_;
    if (drag_ == Drag::Move) {
        note.start = free ? static_cast<int>(std::lround(dragStartNote_.start + dt))
                          : snapNearest(dragStartNote_.start + dt, snapTicks_);
        note.key = dragStartNote_.key + (keyAt(pos.y()) - dragStartKey_);
    } else {
        const int end = free ? static_cast<int>(std::lround(dragStartNote_.end() + dt))
                             : snapNearest(dragStartNote_.end() + dt, snapTicks_);
        note.length = end - note.start;
        lastLength_ = std::max(kMinNoteLength, note.length);
    }
    emit noteChanged(dragIndex_, Project::clamped(note));
}

void PianoRollView::mouseReleaseEvent(QMouseEvent* /*event*/)
{
    if (drag_ == Drag::None)
        return;
    drag_ = Drag::None;
    viewport()->unsetCursor();
    refresh(); // the pattern may have grown
}

void PianoRollView::scrollContentsBy(int /*dx*/, int /*dy*/) { viewport()->update(); }

void PianoRollView::wheelEvent(QWheelEvent* event)
{
    if (!event->modifiers().testFlag(Qt::ControlModifier)) {
        QAbstractScrollArea::wheelEvent(event);
        return;
    }
    // Ctrl+wheel: zoom around the mouse.
    const int notches = event->angleDelta().y() / 120;
    if (notches == 0)
        return;
    const int mouseX = static_cast<int>(event->position().x());
    const double anchor = tickAt(mouseX);
    pixelsPerBeat_
        = std::clamp(pixelsPerBeat_ * std::pow(1.25, notches), kMinPixelsPerBeat, kMaxPixelsPerBeat);
    updateScrollBars();
    horizontalScrollBar()->setValue(static_cast<int>(anchor * pixelsPerTick()) - (mouseX - kKeyboardWidth));
    viewport()->update();
    event->accept();
}

} // namespace vloomdaw
