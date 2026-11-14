// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/launcher/LauncherDialog.hpp"

#include "core/ProjectFile.hpp"
#include "ui/FilePaths.hpp"
#include "ui/RecentProjects.hpp"
#include "ui/Theme.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEnterEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

namespace vloomdaw {

namespace {

constexpr QLatin1StringView kStartupKey{"launcher/showAtStartup"};
constexpr QLatin1StringView kTempoKey{"launcher/tempo"}; ///< the last new project's tempo
constexpr int kCardSpacing = 14;
constexpr int kCardWidth = 236;
constexpr int kCardHeight = 176;
constexpr int kPreviewHeight = 84;
/// Projects larger than this are listed without a preview (reading their
/// plugin states would make the launcher slow to open).
constexpr std::uintmax_t kMaxPreviewBytes = 16u << 20;

/// "1 bar" / "16 bars": the app ships no translations to pick plural forms.
QString counted(qint64 n, const QString& one, const QString& many) { return (n == 1 ? one : many).arg(n); }

/// "2 hours ago", "Yesterday", "Monday", "12 Nov 2026".
QString whenText(const QDateTime& when)
{
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 seconds = when.secsTo(now);
    if (seconds < 60)
        return QObject::tr("Just now");
    if (seconds < 3600)
        return counted(seconds / 60, QObject::tr("%1 minute ago"), QObject::tr("%1 minutes ago"));
    if (when.date() == now.date())
        return counted(seconds / 3600, QObject::tr("%1 hour ago"), QObject::tr("%1 hours ago"));
    if (when.date() == now.date().addDays(-1))
        return QObject::tr("Yesterday");
    if (when.date() > now.date().addDays(-7))
        return QLocale().dayName(when.date().dayOfWeek());
    return QLocale().toString(when.date(), QLocale::ShortFormat);
}

/// One project in the grid: an arrangement preview and what it is. Clicking
/// opens it; right-clicking pins it, removes it from the list or shows its
/// folder.
class ProjectCard : public QAbstractButton {
public:
    ProjectCard(QString path, std::optional<ProjectSummary> summary, bool pinned, QWidget* parent)
        : QAbstractButton(parent)
        , path_(std::move(path))
        , summary_(std::move(summary))
        , pinned_(pinned)
    {
        const QFileInfo info(path_);
        exists_ = info.exists();
        name_ = info.completeBaseName();
        when_ = exists_ ? whenText(info.lastModified()) : tr("File not found");
        setFixedSize(kCardWidth, kCardHeight);
        setCursor(exists_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setContextMenuPolicy(Qt::CustomContextMenu);
        setToolTip(QDir::toNativeSeparators(path_));
        setAccessibleName(tr("Open %1").arg(name_));
        setAccessibleDescription(QDir::toNativeSeparators(path_));
    }

    [[nodiscard]] const QString& path() const { return path_; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath shape;
        shape.addRoundedRect(frame, 10, 10);
        p.setOpacity(exists_ ? 1.0 : 0.5);
        p.fillPath(shape, (underMouse() || hasFocus()) && exists_ ? c.raised : c.panel);

        // The arrangement: lanes of clips, coloured like the playlist.
        p.save();
        p.setClipPath(shape);
        const QRectF preview(frame.left(), frame.top(), frame.width(), kPreviewHeight);
        p.fillRect(preview, c.base);
        if (summary_) {
            const double laneHeight = (preview.height() - 12.0) / ProjectSummary::kLanes;
            for (const ProjectSummary::Block& b : summary_->blocks) {
                const QRectF block(preview.left() + 8.0 + b.start * (preview.width() - 16.0),
                    preview.top() + 6.0 + b.lane * laneHeight + 1.0,
                    std::max(3.0, b.length * (preview.width() - 16.0) - 2.0), laneHeight - 3.0);
                p.setPen(Qt::NoPen);
                p.setBrush(b.pattern == ProjectSummary::kAudio ? Theme::patternColor(1)
                                                               : Theme::patternColor(b.pattern));
                p.drawRoundedRect(block, 2, 2);
            }
        }
        p.setPen(c.border);
        p.drawLine(preview.bottomLeft(), preview.bottomRight());
        p.restore();

        if (pinned_) {
            p.setPen(Qt::NoPen);
            p.setBrush(c.accent);
            p.drawEllipse(QPointF(frame.right() - 14.0, frame.top() + 14.0), 5.0, 5.0);
        }

        // Text: name and tempo, length and plugins, when, path.
        const QRectF text = frame.adjusted(14, kPreviewHeight + 10, -14, -10);
        QFont bold = font();
        bold.setBold(true);
        bold.setPointSizeF(font().pointSizeF() + 1.0);
        const QString tempo
            = summary_ ? tr("%1 BPM").arg(QLocale().toString(summary_->tempo, 'g', 4)) : QString();
        p.setFont(font());
        p.setPen(c.textDim);
        const int tempoWidth = fontMetrics().horizontalAdvance(tempo);
        p.drawText(QRectF(text.right() - tempoWidth, text.top(), tempoWidth, 22),
            Qt::AlignRight | Qt::AlignVCenter, tempo);
        p.setFont(bold);
        p.setPen(c.text);
        const QFontMetrics boldMetrics(bold);
        p.drawText(QRectF(text.left(), text.top(), text.width() - tempoWidth - 8, 22),
            Qt::AlignLeft | Qt::AlignVCenter,
            boldMetrics.elidedText(name_, Qt::ElideRight, static_cast<int>(text.width() - tempoWidth - 8)));

        p.setFont(font());
        QStringList facts;
        if (summary_) {
            facts << (summary_->bars > 0 ? counted(summary_->bars, tr("%1 bar"), tr("%1 bars"))
                                         : tr("Empty playlist"));
            facts << counted(static_cast<qint64>(summary_->plugins), tr("%1 plugin"), tr("%1 plugins"));
        }
        p.setPen(c.textDim);
        const QRectF factsRow(text.left(), text.top() + 24, text.width(), 18);
        p.drawText(factsRow, Qt::AlignLeft | Qt::AlignVCenter, facts.join(QStringLiteral("  ·  ")));
        p.setPen(exists_ ? c.textDim : c.danger);
        p.drawText(factsRow, Qt::AlignRight | Qt::AlignVCenter, when_);

        QFont small = font();
        small.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.5));
        p.setFont(small);
        p.setPen(c.textDim);
        const QString folder = QDir::toNativeSeparators(QFileInfo(path_).absolutePath());
        p.drawText(QRectF(text.left(), text.top() + 44, text.width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(small).elidedText(folder, Qt::ElideMiddle, static_cast<int>(text.width())));

        p.setOpacity(1.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(pinned_ ? c.accent : c.border, 1.0));
        p.drawPath(shape);
    }

    void enterEvent(QEnterEvent* event) override
    {
        update();
        QAbstractButton::enterEvent(event);
    }
    void leaveEvent(QEvent* event) override
    {
        update();
        QAbstractButton::leaveEvent(event);
    }

private:
    QString path_;
    std::optional<ProjectSummary> summary_;
    bool pinned_ = false;
    bool exists_ = true;
    QString name_;
    QString when_;
};

QLabel* caption(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("launcherCaption"));
    return label;
}

} // namespace

LauncherDialog::LauncherDialog(const AudioStatus& audio, double tempo, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Start - VloomDAW"));
    setModal(true);
    resize(1120, 700);
    setMinimumSize(900, 560);

    const Theme::Colors& c = Theme::colors();
    setStyleSheet(QStringLiteral(R"(
        QFrame#launcherSide { background: %1; border-right: 1px solid %2; }
        QLabel#launcherCaption { color: %3; font-weight: 600; letter-spacing: 1px; }
        QLabel#launcherTitle { font-size: 16pt; font-weight: 600; }
        QFrame#launcherStatus { background: %4; border-top: 1px solid %2; }
        QToolButton#launcherFilter { padding: 4px 12px; border: 1px solid transparent; border-radius: 6px; }
        QToolButton#launcherFilter:checked { background: %5; border-color: %2; }
    )")
            .arg(c.sidebar.name(), c.border.name(), c.textDim.name(), c.panel.name(), c.raised.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* body = new QHBoxLayout;
    body->setSpacing(0);
    body->addWidget(createSidePanel(tempo));
    body->addWidget(createRecentPanel(), 1);
    layout->addLayout(body, 1);
    layout->addWidget(createStatusBar(audio));
    rebuildCards();
    search_->setFocus();
}

double LauncherDialog::tempo() const { return tempo_->value(); }

bool LauncherDialog::showAtStartup() { return QSettings().value(kStartupKey, true).toBool(); }

QWidget* LauncherDialog::createSidePanel(double tempo)
{
    auto* side = new QFrame(this);
    side->setObjectName(QStringLiteral("launcherSide"));
    side->setFixedWidth(300);
    auto* layout = new QVBoxLayout(side);
    layout->setContentsMargins(20, 24, 20, 20);
    layout->setSpacing(10);

    layout->addWidget(caption(tr("NEW PROJECT"), side));
    auto* tempoLabel = new QLabel(tr("&Tempo"), side);
    tempo_ = new QDoubleSpinBox(side);
    tempo_->setRange(20.0, 999.0);
    tempo_->setDecimals(2);
    tempo_->setSuffix(tr(" BPM"));
    tempo_->setValue(QSettings().value(kTempoKey, tempo).toDouble());
    tempo_->setMinimumHeight(36);
    tempoLabel->setBuddy(tempo_);
    layout->addWidget(tempoLabel);
    layout->addWidget(tempo_);

    auto* create = new QPushButton(tr("&Empty project"), side);
    create->setObjectName(QStringLiteral("accentButton"));
    create->setMinimumHeight(40);
    create->setDefault(false);
    connect(create, &QPushButton::clicked, this, [this] { choose(Choice::NewProject); });
    layout->addWidget(create);

    layout->addSpacing(16);
    layout->addWidget(caption(tr("TEMPLATES"), side));
    auto* templates
        = new QLabel(tr("Templates come later: save a project with your favourite channels and mixer, "
                        "and open it to start from it."),
            side);
    templates->setWordWrap(true);
    templates->setEnabled(false);
    layout->addWidget(templates);

    layout->addStretch(1);
    auto* open = new QPushButton(tr("&Open project…"), side);
    open->setMinimumHeight(36);
    open->setToolTip(tr("Ctrl+O"));
    connect(open, &QPushButton::clicked, this, [this] { choose(Choice::BrowseOpen); });
    layout->addWidget(open);
    return side;
}

QWidget* LauncherDialog::createRecentPanel()
{
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(28, 24, 28, 16);
    layout->setSpacing(14);

    auto* header = new QHBoxLayout;
    header->setSpacing(12);
    auto* title = new QLabel(tr("Recent projects"), panel);
    title->setObjectName(QStringLiteral("launcherTitle"));
    header->addWidget(title, 1);
    filters_ = new QButtonGroup(panel);
    filters_->setExclusive(true);
    const auto addFilter = [&](const QString& text, Filter filter) {
        auto* button = new QToolButton(panel);
        button->setObjectName(QStringLiteral("launcherFilter"));
        button->setText(text);
        button->setCheckable(true);
        button->setChecked(filter == Filter::All);
        filters_->addButton(button, static_cast<int>(filter));
        header->addWidget(button);
    };
    addFilter(tr("All"), Filter::All);
    addFilter(tr("Pinned"), Filter::Pinned);
    addFilter(tr("This week"), Filter::ThisWeek);
    connect(filters_, &QButtonGroup::idClicked, this, [this] { rebuildCards(); });
    search_ = new QLineEdit(panel);
    search_->setPlaceholderText(tr("Search name or folder"));
    search_->setAccessibleName(tr("Search projects"));
    search_->setClearButtonEnabled(true);
    search_->setFixedWidth(260);
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuildCards(); });
    header->addWidget(search_);
    layout->addLayout(header);

    auto* scroll = new QScrollArea(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    cardsHost_ = new QWidget(scroll);
    cards_ = new QGridLayout(cardsHost_);
    cards_->setContentsMargins(0, 0, 0, 0);
    cards_->setSpacing(kCardSpacing);
    cards_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    scroll->setWidget(cardsHost_);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport_ = scroll->viewport();
    viewport_->installEventFilter(this); // as many columns as fit
    layout->addWidget(scroll, 1);
    return panel;
}

QWidget* LauncherDialog::createStatusBar(const AudioStatus& audio)
{
    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("launcherStatus"));
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(16, 6, 16, 6);
    layout->setSpacing(14);
    const Theme::Colors& c = Theme::colors();
    auto* dot = new QLabel(bar);
    dot->setFixedSize(8, 8);
    dot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;")
            .arg(audio.device.isEmpty() ? c.danger.name() : c.playing.name()));
    layout->addWidget(dot);
    QString text = tr("No audio output");
    if (!audio.device.isEmpty()) {
        const double ms = audio.sampleRate > 0 ? 1000.0 * audio.bufferFrames / audio.sampleRate : 0.0;
        text = tr("%1  ·  %2 kHz  ·  %3 samples  ·  %4 ms")
                   .arg(audio.device)
                   .arg(audio.sampleRate / 1000.0, 0, 'g', 4)
                   .arg(audio.bufferFrames)
                   .arg(ms, 0, 'f', 1);
    }
    layout->addWidget(new QLabel(text, bar));
    auto* settings = new QPushButton(tr("&Audio settings"), bar);
    settings->setFlat(true);
    connect(settings, &QPushButton::clicked, this, [this] { choose(Choice::AudioSettings); });
    layout->addWidget(settings);
    layout->addStretch(1);
    startup_ = new QCheckBox(tr("Show this window at &startup"), bar);
    startup_->setChecked(showAtStartup());
    connect(startup_, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue(kStartupKey, on); });
    layout->addWidget(startup_);
    return bar;
}

bool LauncherDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == viewport_ && event->type() == QEvent::Resize)
        layoutCards();
    return QDialog::eventFilter(watched, event);
}

void LauncherDialog::layoutCards()
{
    const int columns = std::max(1, (viewport_->width() + kCardSpacing) / (kCardWidth + kCardSpacing));
    if (columns == columns_ || cardWidgets_.empty())
        return;
    columns_ = columns;
    for (QWidget* card : cardWidgets_)
        cards_->removeWidget(card);
    for (std::size_t i = 0; i < cardWidgets_.size(); ++i)
        cards_->addWidget(cardWidgets_[i], static_cast<int>(i) / columns, static_cast<int>(i) % columns);
}

void LauncherDialog::rebuildCards()
{
    cardWidgets_.clear();
    while (QLayoutItem* item = cards_->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    // Pinned first, then the recent ones not pinned.
    QStringList paths = recent::pinned();
    for (const QString& path : recent::projects())
        if (!recent::isPinned(path))
            paths << path;
    const auto filter = static_cast<Filter>(filters_->checkedId());
    const QString query = search_->text().trimmed();
    const QDateTime weekAgo = QDateTime::currentDateTime().addDays(-7);

    int shown = 0;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        const bool pinned = recent::isPinned(path);
        if (filter == Filter::Pinned && !pinned)
            continue;
        if (filter == Filter::ThisWeek && (!info.exists() || info.lastModified() < weekAgo))
            continue;
        if (!query.isEmpty() && !path.contains(query, Qt::CaseInsensitive))
            continue;
        std::optional<ProjectSummary> summary;
        if (info.exists())
            summary = ProjectFile::summarize(toPath(path), kMaxPreviewBytes);
        auto* card = new ProjectCard(path, std::move(summary), pinned, cardsHost_);
        connect(card, &QAbstractButton::clicked, this, [this, path] {
            if (QFileInfo::exists(path)) // a missing one keeps its menu: remove it from the list
                choose(Choice::OpenFile, path);
        });
        connect(card, &QWidget::customContextMenuRequested, this,
            [this, card, path](const QPoint& at) { showCardMenu(path, card->mapToGlobal(at)); });
        cardWidgets_.push_back(card);
        ++shown;
    }
    columns_ = 0; // placed by layoutCards()
    layoutCards();
    if (shown == 0) {
        auto* empty = new QLabel(
            paths.isEmpty() ? tr("Projects you save or open show up here.") : tr("No project matches."),
            cardsHost_);
        empty->setEnabled(false);
        cards_->addWidget(empty, 0, 0);
    }
}

void LauncherDialog::choose(Choice choice, const QString& path)
{
    choice_ = choice;
    path_ = path;
    if (choice == Choice::NewProject)
        QSettings().setValue(kTempoKey, tempo_->value());
    accept();
}

void LauncherDialog::showCardMenu(const QString& path, const QPoint& globalPos)
{
    QMenu menu(this);
    const bool pinned = recent::isPinned(path);
    menu.addAction(pinned ? tr("Unpin") : tr("Pin"), this, [this, path, pinned] {
        recent::setPinned(path, !pinned);
        rebuildCards();
    });
    menu.addAction(tr("Show in folder"), this,
        [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())); });
    menu.addSeparator();
    menu.addAction(tr("Remove from list"), this, [this, path] {
        recent::remove(path);
        recent::setPinned(path, false);
        rebuildCards();
    });
    menu.exec(globalPos);
}

} // namespace vloomdaw
