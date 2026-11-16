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
#include <limits>

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
    clearNoteSelection();
    refresh();
}

void PianoRollView::setTarget(std::size_t pattern, std::size_t channel)
{
    if (pattern_ != pattern || channel_ != channel)
        clearNoteSelection();
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
    syncNoteSelection();
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
            const bool selected = isNoteSelected(i);
            p.setPen(QPen(selected ? c.text : fill.darker(160), selected ? 1.5 : 1.0));
            p.setBrush((drag_ != Drag::None && i == dragIndex_) || selected ? fill.lighter(122) : fill);
            p.drawRoundedRect(r, 2, 2);
            // Resize grip.
            if (r.width() > 12)
                p.fillRect(QRectF(r.right() - 3, r.top() + 3, 1, r.height() - 6), fill.darker(170));
        }
        if (band_) {
            QColor bandFill = c.accent;
            bandFill.setAlpha(40);
            p.setPen(QPen(c.accent, 1, Qt::DashLine));
            p.setBrush(bandFill);
            p.drawRect(QRectF(*band_).adjusted(0.5, 0.5, -0.5, -0.5));
        }
        p.restore();
    }

    // Ruler.
    const QRect ruler(kKeyboardWidth, 0, grid.width(), timeline::kRulerHeight);
    if (event->rect().intersects(ruler))
        timeline::paintRuler(p, ruler, scale);

    // Keyboard (skipped when only the grid needs repainting).
    const QRect keys(0, timeline::kRulerHeight, kKeyboardWidth, grid.height());
    if (event->rect().intersects(QRect(0, 0, kKeyboardWidth, viewport()->height()))) {
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

    timeline::paintPlayhead(p, QRect(grid.left(), 0, grid.width(), viewport()->height()), scale, playhead_);
}

bool PianoRollView::isNoteSelected(std::size_t index) const
{
    return index < selectedNotes_.size() && selectedNotes_[index] != 0;
}

std::size_t PianoRollView::selectedNoteCount() const
{
    return static_cast<std::size_t>(std::count(selectedNotes_.begin(), selectedNotes_.end(), 1));
}

void PianoRollView::clearNoteSelection() { std::fill(selectedNotes_.begin(), selectedNotes_.end(), 0); }

void PianoRollView::syncNoteSelection()
{
    const auto* list = notes();
    const std::size_t count = list != nullptr ? list->size() : 0;
    if (selectedNotes_.size() != count)
        selectedNotes_.assign(count, 0);
}

bool PianoRollView::hasSelectedNotes()
{
    syncNoteSelection();
    return selectedNoteCount() > 0;
}

void PianoRollView::selectAllNotes()
{
    syncNoteSelection();
    std::fill(selectedNotes_.begin(), selectedNotes_.end(), 1);
    viewport()->update();
}

void PianoRollView::deleteSelectedNotes()
{
    syncNoteSelection();
    if (selectedNoteCount() == 0)
        return;
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < selectedNotes_.size(); ++i)
        if (selectedNotes_[i] != 0)
            indices.push_back(i);
    clearNoteSelection();
    emit notesRemovedBatch(indices);
}

void PianoRollView::copySelectedNotes()
{
    const auto* list = notes();
    if (list == nullptr)
        return;
    syncNoteSelection();
    if (selectedNoteCount() == 0)
        return;

    clipboardNotes_.clear();
    int minStart = std::numeric_limits<int>::max();
    long long maxEnd = 0;
    for (std::size_t i = 0; i < selectedNotes_.size(); ++i) {
        if (selectedNotes_[i] == 0)
            continue;
        const Note& n = (*list)[i];
        clipboardNotes_.push_back(n);
        minStart = std::min(minStart, n.start);
        maxEnd = std::max(maxEnd, static_cast<long long>(n.start) + n.length);
    }
    constexpr long long kMaxTick = std::numeric_limits<int>::max() / 2;
    clipboardMinStart_ = minStart == std::numeric_limits<int>::max() ? 0 : minStart;
    clipboardMaxEnd_ = static_cast<int>(
        std::clamp(std::max(static_cast<long long>(clipboardMinStart_) + snapTicks_, maxEnd), 0LL, kMaxTick));
}

void PianoRollView::cutSelectedNotes()
{
    copySelectedNotes();
    deleteSelectedNotes();
}

void PianoRollView::pasteNotes()
{
    if (clipboardNotes_.empty())
        return;
    constexpr long long kMaxTick = std::numeric_limits<int>::max() / 2;
    const long long grid = std::max<long long>(kMinNoteLength, snapTicks_);
    const long long targetStart = playhead_ >= 0.0
        ? std::clamp<long long>(snapDown(playhead_, static_cast<int>(grid)), 0LL, kMaxTick)
        : std::min(kMaxTick, ((static_cast<long long>(clipboardMaxEnd_) + grid - 1) / grid) * grid);
    const long long delta = targetStart - clipboardMinStart_;

    std::vector<Note> added;
    added.reserve(clipboardNotes_.size());
    for (Note n : clipboardNotes_) {
        n.start = static_cast<int>(std::clamp(static_cast<long long>(n.start) + delta, 0LL, kMaxTick));
        added.push_back(Project::clamped(n));
    }

    const auto* before = notes();
    const std::size_t oldSize = before != nullptr ? before->size() : 0;
    emit notesAddedBatch(added);

    syncNoteSelection();
    clearNoteSelection();
    for (std::size_t i = oldSize; i < selectedNotes_.size(); ++i)
        selectedNotes_[i] = 1;
    viewport()->update();
}

void PianoRollView::duplicateSelectedNotes()
{
    const auto* list = notes();
    if (list == nullptr)
        return;
    syncNoteSelection();
    if (selectedNoteCount() == 0)
        return;

    constexpr long long kMaxTick = std::numeric_limits<int>::max() / 2;
    int minStart = std::numeric_limits<int>::max();
    long long maxEnd = 0;
    std::vector<Note> added;
    for (std::size_t i = 0; i < selectedNotes_.size(); ++i) {
        if (selectedNotes_[i] == 0)
            continue;
        const Note& n = (*list)[i];
        added.push_back(n);
        minStart = std::min(minStart, n.start);
        maxEnd = std::max(maxEnd, static_cast<long long>(n.start) + n.length);
    }

    const long long rawSpan = std::max<long long>(snapTicks_, maxEnd - minStart);
    const long long grid = std::max<long long>(kMinNoteLength, snapTicks_);
    const long long shift = ((rawSpan + grid - 1) / grid) * grid;
    for (Note& n : added)
        n.start = static_cast<int>(std::clamp(static_cast<long long>(n.start) + shift, 0LL, kMaxTick));

    const std::size_t oldSize = list->size();
    emit notesAddedBatch(added);

    syncNoteSelection();
    clearNoteSelection();
    for (std::size_t i = oldSize; i < selectedNotes_.size(); ++i)
        selectedNotes_[i] = 1;
    viewport()->update();
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
    if (drag == Drag::GroupMove) {
        groupStartNotes_.clear();
        for (std::size_t i = 0; i < selectedNotes_.size() && i < list->size(); ++i)
            if (selectedNotes_[i] != 0)
                groupStartNotes_.emplace_back(i, (*list)[i]);
    }
    viewport()->setCursor(drag == Drag::Resize ? Qt::SizeHorCursor : Qt::ClosedHandCursor);
}

void PianoRollView::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    const QPoint pos = event->position().toPoint();
    if (project_ == nullptr || !gridRect().contains(pos))
        return;
    syncNoteSelection();
    const auto hit = noteAt(pos);

    if (event->button() == Qt::RightButton) {
        if (hit)
            emit noteRemoved(hit->first);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);

    if (ctrl && !hit) {
        drag_ = Drag::Band;
        bandStart_ = pos;
        bandAdds_ = shift;
        band_ = QRect(pos, QSize(1, 1));
        viewport()->update();
        return;
    }

    if (hit) {
        if (ctrl) {
            selectedNotes_[hit->first] = selectedNotes_[hit->first] != 0 ? 0 : 1;
            viewport()->update();
            return;
        }
        if (hit->second) {
            if (!isNoteSelected(hit->first)) {
                clearNoteSelection();
                selectedNotes_[hit->first] = 1;
            }
            beginDrag(hit->first, Drag::Resize, pos);
            return;
        }
        if (isNoteSelected(hit->first) && selectedNoteCount() > 1) {
            beginDrag(hit->first, Drag::GroupMove, pos);
        } else {
            clearNoteSelection();
            selectedNotes_[hit->first] = 1;
            beginDrag(hit->first, Drag::Move, pos);
        }
        viewport()->update();
        return;
    }

    if (shift) {
        drag_ = Drag::Band;
        bandStart_ = pos;
        bandAdds_ = ctrl;
        band_ = QRect(pos, QSize(1, 1));
        viewport()->update();
        return;
    }

    clearNoteSelection();
    // New note at the clicked step, then drag it into place.
    const Note note{keyAt(pos.y()), snapDown(tickAt(pos.x()), snapTicks_), lastLength_};
    emit noteAdded(note);
    syncNoteSelection();
    if (const auto* list = notes(); list != nullptr && !list->empty()) {
        selectedNotes_[list->size() - 1] = 1;
        beginDrag(list->size() - 1, Drag::Move, pos);
    }
}

void PianoRollView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (drag_ == Drag::None) {
        const auto hit = noteAt(pos);
        viewport()->setCursor(hit ? (hit->second ? Qt::SizeHorCursor : Qt::OpenHandCursor) : Qt::ArrowCursor);
        return;
    }
    if (drag_ == Drag::Band) {
        band_ = QRect(bandStart_, pos).normalized().intersected(gridRect());
        viewport()->update();
        return;
    }
    const bool free = event->modifiers().testFlag(Qt::AltModifier);
    const double dt = tickAt(pos.x()) - dragStartTick_;
    const int dk = keyAt(pos.y()) - dragStartKey_;
    if (drag_ == Drag::GroupMove && !groupStartNotes_.empty()) {
        int earliest = std::numeric_limits<int>::max();
        int minKey = kHighestKey;
        int maxKey = kLowestKey;
        for (const auto& [idx, n] : groupStartNotes_) {
            earliest = std::min(earliest, n.start);
            minKey = std::min(minKey, n.key);
            maxKey = std::max(maxKey, n.key);
        }
        const int newEarliest
            = free ? static_cast<int>(std::lround(earliest + dt)) : snapNearest(earliest + dt, snapTicks_);
        const int shiftTick = std::max(0, newEarliest) - earliest;
        const int shiftKey = std::clamp(dk, kLowestKey - minKey, kHighestKey - maxKey);
        std::vector<std::pair<std::size_t, Note>> edits;
        edits.reserve(groupStartNotes_.size());
        for (auto [idx, n] : groupStartNotes_) {
            n.start += shiftTick;
            n.key += shiftKey;
            edits.emplace_back(idx, Project::clamped(n));
        }
        emit notesChangedBatch(edits);
        return;
    }
    Note note = dragStartNote_;
    if (drag_ == Drag::Move) {
        note.start = free ? static_cast<int>(std::lround(dragStartNote_.start + dt))
                          : snapNearest(dragStartNote_.start + dt, snapTicks_);
        note.key = dragStartNote_.key + dk;
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
    if (drag_ == Drag::Band && band_) {
        syncNoteSelection();
        if (!bandAdds_)
            clearNoteSelection();
        if (const auto* list = notes(); list != nullptr) {
            const QRectF box(*band_);
            for (std::size_t i = 0; i < list->size(); ++i)
                if (noteRect((*list)[i]).intersects(box))
                    selectedNotes_[i] = 1;
        }
        band_.reset();
        drag_ = Drag::None;
        viewport()->unsetCursor();
        viewport()->update();
        return;
    }
    drag_ = Drag::None;
    groupStartNotes_.clear();
    viewport()->unsetCursor();
    refresh(); // the pattern may have grown
}

void PianoRollView::keyPressEvent(QKeyEvent* event)
{
    syncNoteSelection();
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && selectedNoteCount() > 0) {
        deleteSelectedNotes();
        return;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        selectAllNotes();
        return;
    }
    if (event->key() == Qt::Key_Escape && selectedNoteCount() > 0) {
        clearNoteSelection();
        viewport()->update();
        return;
    }
    if (event->matches(QKeySequence::Copy) && selectedNoteCount() > 0) {
        copySelectedNotes();
        return;
    }
    if (event->matches(QKeySequence::Cut) && selectedNoteCount() > 0) {
        cutSelectedNotes();
        return;
    }
    if (event->matches(QKeySequence::Paste) && hasNoteClipboard()) {
        pasteNotes();
        return;
    }
    if (event->key() == Qt::Key_D && event->modifiers() == Qt::ControlModifier && selectedNoteCount() > 0) {
        duplicateSelectedNotes();
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void PianoRollView::scrollContentsBy(int /*dx*/, int /*dy*/) { viewport()->update(); }

void PianoRollView::wheelEvent(QWheelEvent* event)
{
    const QPoint pos = event->position().toPoint();
    const bool onRuler = pos.y() >= 0 && pos.y() < timeline::kRulerHeight && pos.x() >= kKeyboardWidth;

    if (onRuler || event->modifiers().testFlag(Qt::ControlModifier)) {
        // Ruler hover wheel or Ctrl+wheel: zoom horizontally around the mouse cursor.
        const int rawDelta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
        if (rawDelta == 0)
            return;
        const double steps = static_cast<double>(rawDelta) / 120.0;
        const int mouseX = std::max(kKeyboardWidth, pos.x());
        const double anchor = tickAt(mouseX);
        pixelsPerBeat_
            = std::clamp(pixelsPerBeat_ * std::pow(1.25, steps), kMinPixelsPerBeat, kMaxPixelsPerBeat);
        updateScrollBars();
        horizontalScrollBar()->setValue(
            static_cast<int>(anchor * pixelsPerTick()) - (mouseX - kKeyboardWidth));
        viewport()->update();
        event->accept();
        return;
    }

    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        const int rawDelta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
        if (rawDelta != 0) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - rawDelta);
            event->accept();
            return;
        }
    }

    QAbstractScrollArea::wheelEvent(event);
}

} // namespace vloomdaw
