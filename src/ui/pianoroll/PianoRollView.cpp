// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/pianoroll/PianoRollView.hpp"

#include "core/NoteNames.hpp"
#include "ui/Theme.hpp"
#include "ui/widgets/ColorMenu/ColorMenu.hpp"
#include "ui/widgets/Timeline/Timeline.hpp"

#include <QKeyEvent>
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

int snapDown(double tick, int grid) { return static_cast<int>(std::floor(tick / grid)) * grid; }
int snapNearest(double tick, int grid) { return static_cast<int>(std::lround(tick / grid)) * grid; }

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
    if (pattern != pattern_ || channel != channel_)
        selection_.clear();
    pattern_ = pattern;
    channel_ = channel;
    drag_ = Drag::None;
    dragChanged_ = false;
    sound(-1, 0);
    viewport()->unsetCursor();
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
    const auto* list = notes();
    const std::size_t count = list != nullptr ? list->size() : 0;
    if (count != expectedCount_) {
        // Not an edit of this view: the flags no longer name the same notes.
        selection_.clear();
        expectedCount_ = count;
    }
    selection_.resize(count, false);
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

QRect PianoRollView::laneRect() const
{
    const QRect v = viewport()->rect();
    // A short window keeps most of its height for the notes.
    const int height = std::min(kLaneHeight, std::max(0, (v.height() - timeline::kRulerHeight) / 3));
    return {kKeyboardWidth, v.height() - height, v.width() - kKeyboardWidth, height};
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

void PianoRollView::hideEvent(QHideEvent* event)
{
    QAbstractScrollArea::hideEvent(event);
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

    // Notes: darker the softer they are played, outlined when selected.
    if (list != nullptr && !area.isEmpty()) {
        p.save();
        p.setClipRect(area);
        p.setRenderHint(QPainter::Antialiasing);
        for (std::size_t i = 0; i < list->size(); ++i) {
            const Note& note = (*list)[i];
            const QRectF r = noteRect(note).adjusted(0.5, 1.0, -0.5, -1.0);
            if (!r.intersects(area))
                continue;
            const bool chosen = isSelected(selection_, i);
            const QColor body = shaded(fill, note.velocity);
            p.setPen(chosen ? QPen(c.text, 1.5) : QPen(fill.darker(160), 1));
            p.setBrush(chosen ? body.lighter(125) : body);
            p.drawRoundedRect(r, 2, 2);
            // Resize grip.
            if (r.width() > 12)
                p.fillRect(QRectF(r.right() - 3, r.top() + 3, 1, r.height() - 6), fill.darker(170));
        }
        // The box being dragged to select notes.
        if (drag_ == Drag::Box) {
            const QRect box = QRect(dragStart_, dragLast_).normalized().intersected(grid);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(c.accent);
            p.setBrush(QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 40));
            p.drawRect(box.adjusted(0, 0, -1, -1));
        }
        p.restore();
    }

    timeline::paintPlayhead(p, grid, scale, playhead_);

    // The velocity lane: a stem per note, as tall as the note is hard.
    const QRect laneArea = lane.intersected(event->rect());
    if (!laneArea.isEmpty()) {
        p.fillRect(laneArea, c.rail);
        p.fillRect(QRect(lane.left(), lane.top(), lane.width(), 1).intersected(laneArea), c.border);
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
    }

    // Ruler.
    const QRect ruler(kKeyboardWidth, 0, grid.width(), timeline::kRulerHeight);
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

    // The corners: above the keyboard, and beside the lane.
    p.fillRect(QRect(0, 0, kKeyboardWidth, timeline::kRulerHeight), c.panel);
    if (lane.height() > 0) {
        const QRect corner(0, lane.top(), kKeyboardWidth, lane.height());
        p.fillRect(corner, c.panel);
        p.fillRect(QRect(0, lane.top(), kKeyboardWidth, 1), c.border);
        p.setFont(small);
        p.setPen(c.textDim);
        p.drawText(corner.adjusted(6, 4, -4, 0), Qt::AlignLeft | Qt::AlignTop, tr("Velocity"));
    }
}

void PianoRollView::edit(const std::vector<Note>& next)
{
    expectedCount_ = next.size();
    selection_.resize(next.size(), false);
    emit notesChanged(next);
    updateScrollBars();
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
}

void PianoRollView::sound(int key, int velocity)
{
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
}

void PianoRollView::endDrag()
{
    if (drag_ == Drag::None)
        return;
    drag_ = Drag::None;
    sound(-1, 0);
    viewport()->unsetCursor();
    if (dragChanged_)
        emit editFinished();
    dragChanged_ = false;
    dragNotes_.clear();
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

    // The keyboard plays; the lane sets velocities.
    if (event->button() == Qt::LeftButton && pos.x() < kKeyboardWidth && pos.y() >= timeline::kRulerHeight
        && pos.y() < gridRect().bottom()) {
        dragNotes_.clear();
        beginDrag(Drag::Keys, 0, pos);
        sound(keyAt(pos.y()), lastVelocity_);
        return;
    }
    if (event->button() == Qt::LeftButton && laneRect().contains(pos)) {
        dragNotes_ = *list;
        beginDrag(Drag::Velocity, 0, pos);
        setVelocityAt(pos, pos);
        return;
    }
    if (!gridRect().contains(pos))
        return;
    const auto hit = noteAt(pos);

    if (event->button() == Qt::RightButton) {
        dragNotes_ = *list;
        beginDrag(Drag::Erase, 0, pos);
        eraseAt(pos);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    if (hit) {
        std::size_t index = hit->first;
        if (ctrl) {
            // In or out of the selection; nothing moves.
            selection_.resize(list->size(), false);
            selection_[index] = !selection_[index];
            viewport()->update();
            return;
        }
        if (!isSelected(selection_, index))
            selectOnly(index);
        dragNotes_ = *list;
        if (shift && !hit->second) {
            // A copy of the selection is what moves; the notes stay.
            const std::vector<Note> copies = selectedNotes(dragNotes_, selection_);
            if (dragNotes_.size() + copies.size() <= kMaxPatternNotes) {
                std::size_t rank = 0; // the clicked note's place among the selected ones
                for (std::size_t i = 0; i < index; ++i)
                    rank += isSelected(selection_, i) ? 1 : 0;
                index = dragNotes_.size() + rank;
                selection_.assign(dragNotes_.size(), false);
                dragNotes_.insert(dragNotes_.end(), copies.begin(), copies.end());
                selection_.resize(dragNotes_.size(), true);
            }
        }
        beginDrag(hit->second ? Drag::Resize : Drag::Move, index, pos);
        sound(dragNote_.key, dragNote_.velocity);
        viewport()->update();
        return;
    }
    if (ctrl) {
        boxBase_ = shift ? selection_ : NoteSelection{};
        if (!shift)
            selection_.assign(list->size(), false);
        dragNotes_.clear();
        beginDrag(Drag::Box, 0, pos);
        viewport()->update();
        return;
    }
    // New note at the clicked step, then drag it into place.
    if (list->size() >= kMaxPatternNotes)
        return;
    dragNotes_ = *list;
    dragNotes_.push_back(Project::clamped(
        Note{keyAt(pos.y()), snapDown(tickAt(pos.x()), snapTicks_), lastLength_, lastVelocity_}));
    selection_.assign(dragNotes_.size(), false);
    selection_.back() = true;
    beginDrag(Drag::Move, dragNotes_.size() - 1, pos);
    dragChanged_ = true;
    edit(dragNotes_);
    sound(dragNote_.key, dragNote_.velocity);
}

void PianoRollView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    hover_ = pos;
    const auto* list = notes();
    if (drag_ == Drag::None || list == nullptr) {
        if (laneRect().contains(pos)) {
            viewport()->setCursor(Qt::SizeVerCursor);
            return;
        }
        const auto hit = noteAt(pos);
        viewport()->setCursor(hit ? (hit->second ? Qt::SizeHorCursor : Qt::OpenHandCursor) : Qt::ArrowCursor);
        return;
    }
    const bool free = event->modifiers().testFlag(Qt::AltModifier);
    const double dt = tickAt(pos.x()) - dragStartTick_;
    const auto snapped = [this, free](double tick) {
        return free ? static_cast<int>(std::lround(tick)) : snapNearest(tick, snapTicks_);
    };

    switch (drag_) {
    case Drag::Move: {
        const std::vector<Note> next = movedNotes(dragNotes_, selection_,
            snapped(dragNote_.start + dt) - dragNote_.start, keyAt(pos.y()) - dragStartKey_);
        if (next != *list) {
            dragChanged_ = true;
            edit(next);
        }
        if (dragIndex_ < next.size())
            sound(next[dragIndex_].key, next[dragIndex_].velocity);
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
        viewport()->update();
        break;
    }
    case Drag::Erase:
        eraseAt(pos);
        break;
    case Drag::Velocity:
        setVelocityAt(dragLast_, pos);
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
    if (event->type() == QEvent::Leave)
        hover_.reset();
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

void PianoRollView::selectAll()
{
    selection_.assign(expectedCount_, true);
    viewport()->update();
}

void PianoRollView::clearSelection()
{
    selection_.assign(expectedCount_, false);
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
    // Where the mouse points, else at the first bar line in view.
    const int at = hover_ && gridRect().contains(*hover_)
        ? snapDown(tickAt(hover_->x()), snapTicks_)
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
    // Right after the selection, on the snap grid.
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
