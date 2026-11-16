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
#include <QLinearGradient>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
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
constexpr int kCardWidth = 238;
constexpr int kCardHeight = 178;
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

/// Custom-painted Vloom brand banner at the top of the Start Hub's studio column.
class LauncherBrandBanner final : public QWidget {
public:
    explicit LauncherBrandBanner(QWidget* parent = nullptr)
        : QWidget(parent)
        , logo_(QStringLiteral(":/app/vloomdaw.png"))
    {
        setFixedHeight(68);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(box, 10.0, 10.0);

        QLinearGradient bg(box.topLeft(), box.bottomRight());
        bg.setColorAt(0.0, c.panel.lighter(108));
        bg.setColorAt(1.0, c.base);
        p.fillPath(clip, bg);

        QColor accentGlow = c.accent;
        accentGlow.setAlpha(28);
        QLinearGradient glow(box.topLeft(), QPointF(box.left() + 140.0, box.bottom()));
        glow.setColorAt(0.0, accentGlow);
        glow.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillPath(clip, glow);

        const QRectF iconRect(12.0, (box.height() - 44.0) * 0.5, 44.0, 44.0);
        QPainterPath iconClip;
        iconClip.addRoundedRect(iconRect, 10.0, 10.0);
        p.save();
        p.setClipPath(iconClip);
        if (!logo_.isNull())
            p.drawPixmap(iconRect.toRect(), logo_);
        else
            p.fillRect(iconRect, c.raised);
        p.restore();
        p.setPen(QPen(c.border.lighter(125), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(iconRect, 10.0, 10.0);

        const double textX = 68.0;
        QFont brandFont = font();
        brandFont.setPointSizeF(font().pointSizeF() + 4.5);
        brandFont.setWeight(QFont::Bold);
        brandFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
        p.setFont(brandFont);
        p.setPen(c.text);
        const QString brand = QStringLiteral("VLOOM");
        const int brandW = QFontMetrics(brandFont).horizontalAdvance(brand);
        p.drawText(QPointF(textX, 31.0), brand);

        // Version pill
        QFont verFont = font();
        verFont.setPointSizeF(std::max(7.5, font().pointSizeF() - 1.5));
        verFont.setWeight(QFont::Bold);
        const QString ver = QStringLiteral("v%1").arg(QStringLiteral(VLOOMDAW_VERSION));
        const int verW = QFontMetrics(verFont).horizontalAdvance(ver) + 14;
        const QRectF verRect(textX + brandW + 8.0, 16.0, verW, 18.0);
        QColor pillBg = c.accent;
        pillBg.setAlpha(36);
        QColor pillBorder = c.accent;
        pillBorder.setAlpha(155);
        p.setPen(QPen(pillBorder, 1.0));
        p.setBrush(pillBg);
        p.drawRoundedRect(verRect, 9.0, 9.0);
        p.setFont(verFont);
        p.setPen(c.accent.lighter(120));
        p.drawText(verRect, Qt::AlignCenter, ver);

        // Subtitle
        QFont subFont = font();
        subFont.setPointSizeF(std::max(8.0, font().pointSizeF() - 1.0));
        p.setFont(subFont);
        p.setPen(c.textDim);
        p.drawText(QPointF(textX, 50.0), tr("Studio Hub  ·  Digital Audio Workstation"));

        p.setPen(QPen(c.border, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(clip);
    }

private:
    QPixmap logo_;
};

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
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath shape;
        shape.addRoundedRect(frame, 10, 10);
        const bool hovered = (underMouse() || hasFocus()) && exists_;
        p.setOpacity(exists_ ? 1.0 : 0.5);
        p.fillPath(shape, hovered ? c.raised : c.panel);

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
        p.setPen(QPen(hovered || pinned_ ? c.accent : c.border, hovered ? 1.4 : 1.0));
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

LauncherDialog::LauncherDialog(const AudioStatus& audio, double tempo, QWidget* parent, bool hasOpenProject)
    : QDialog(parent)
{
    setWindowTitle(tr("Start - VloomDAW"));
    setModal(true);
    resize(1140, 710);
    setMinimumSize(920, 580);

    const Theme::Colors& c = Theme::colors();
    setStyleSheet(QStringLiteral(R"(
        QFrame#launcherSide { background: %1; border-right: 1px solid %2; }
        QFrame#launcherCard {
            background: %4;
            border: 1px solid %2;
            border-radius: 10px;
        }
        QFrame#launcherSpecPill {
            background: %6;
            border: 1px solid %2;
            border-radius: 6px;
        }
        QLabel#launcherCaption {
            color: %3;
            font-size: 8.5pt;
            font-weight: 700;
            letter-spacing: 0.8px;
        }
        QLabel#launcherTitle { font-size: 15pt; font-weight: 700; }
        QLabel#launcherCountBadge {
            background: %4;
            color: %3;
            border: 1px solid %2;
            border-radius: 10px;
            padding: 2px 9px;
            font-size: 8.5pt;
            font-weight: 600;
        }
        QFrame#launcherStatus { background: %4; border-top: 1px solid %2; }
        QToolButton#launcherFilter {
            padding: 4px 12px;
            border: 1px solid transparent;
            border-radius: 6px;
            font-weight: 600;
        }
        QToolButton#launcherFilter:hover { background: %5; }
        QToolButton#launcherFilter:checked { background: %5; border-color: %2; }
        QToolButton#launcherTempoPreset {
            background: %6;
            color: %3;
            border: 1px solid %2;
            border-radius: 5px;
            padding: 3px 0px;
            font-size: 8.5pt;
            font-weight: 600;
        }
        QToolButton#launcherTempoPreset:hover {
            background: %5;
            color: %8;
            border-color: %7;
        }
        QDoubleSpinBox { background: %6; border: 1px solid %2; border-radius: 6px; padding: 3px 8px; }
        QDoubleSpinBox:focus { border-color: %7; }
    )")
            .arg(c.sidebar.name(), c.border.name(), c.textDim.name(), c.panel.name(), c.raised.name(),
                c.base.name(), c.accent.name(), c.text.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* body = new QHBoxLayout;
    body->setSpacing(0);
    body->addWidget(createSidePanel(tempo, audio));
    body->addWidget(createRecentPanel(), 1);
    layout->addLayout(body, 1);
    layout->addWidget(createStatusBar(hasOpenProject));
    rebuildCards();
    search_->setFocus();
}

double LauncherDialog::tempo() const { return tempo_->value(); }

bool LauncherDialog::showAtStartup() { return QSettings().value(kStartupKey, true).toBool(); }

QWidget* LauncherDialog::createSidePanel(double tempo, const AudioStatus& audio)
{
    const Theme::Colors& c = Theme::colors();

    auto* side = new QFrame(this);
    side->setObjectName(QStringLiteral("launcherSide"));
    side->setFixedWidth(332);
    auto* layout = new QVBoxLayout(side);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    // 1. Vloom Studio Brand Header
    layout->addWidget(new LauncherBrandBanner(side));

    // 2. Session Creation & Open Card
    auto* sessionCard = new QFrame(side);
    sessionCard->setObjectName(QStringLiteral("launcherCard"));
    auto* sessionLay = new QVBoxLayout(sessionCard);
    sessionLay->setContentsMargins(14, 13, 14, 14);
    sessionLay->setSpacing(9);

    sessionLay->addWidget(caption(tr("START A SESSION"), sessionCard));

    auto* tempoRow = new QHBoxLayout;
    tempoRow->setSpacing(8);
    auto* tempoLabel = new QLabel(tr("&Tempo"), sessionCard);
    tempo_ = new QDoubleSpinBox(sessionCard);
    tempo_->setRange(20.0, 999.0);
    tempo_->setDecimals(2);
    tempo_->setSuffix(tr(" BPM"));
    tempo_->setValue(QSettings().value(kTempoKey, tempo).toDouble());
    tempo_->setMinimumHeight(32);
    tempoLabel->setBuddy(tempo_);
    tempoRow->addWidget(tempoLabel);
    tempoRow->addWidget(tempo_, 1);
    sessionLay->addLayout(tempoRow);

    // Quick-tap studio BPM presets
    auto* presetRow = new QHBoxLayout;
    presetRow->setSpacing(6);
    for (const int bpm : {90, 120, 140, 160}) {
        auto* btn = new QToolButton(sessionCard);
        btn->setObjectName(QStringLiteral("launcherTempoPreset"));
        btn->setText(QString::number(bpm));
        btn->setToolTip(tr("Set initial project tempo to %1 BPM").arg(bpm));
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QToolButton::clicked, this, [this, bpm] { tempo_->setValue(bpm); });
        presetRow->addWidget(btn);
    }
    sessionLay->addLayout(presetRow);

    sessionLay->addSpacing(2);

    auto* create
        = new QPushButton(Theme::icon(QStringLiteral("plus")), tr("&New Empty Project"), sessionCard);
    create->setObjectName(QStringLiteral("accentButton"));
    create->setMinimumHeight(40);
    create->setCursor(Qt::PointingHandCursor);
    create->setDefault(false);
    connect(create, &QPushButton::clicked, this, [this] { choose(Choice::NewProject); });
    sessionLay->addWidget(create);

    auto* open = new QPushButton(Theme::icon(QStringLiteral("folder")), tr("&Open Project…"), sessionCard);
    open->setMinimumHeight(36);
    open->setCursor(Qt::PointingHandCursor);
    open->setToolTip(tr("Browse for an existing .vloom project file (Ctrl+O)"));
    connect(open, &QPushButton::clicked, this, [this] { choose(Choice::BrowseOpen); });
    sessionLay->addWidget(open);

    layout->addWidget(sessionCard);

    // 3. Audio Setup Card (Vloom's clean studio hardware card)
    auto* setupCard = new QFrame(side);
    setupCard->setObjectName(QStringLiteral("launcherCard"));
    auto* setupLay = new QVBoxLayout(setupCard);
    setupLay->setContentsMargins(14, 13, 14, 14);
    setupLay->setSpacing(8);

    setupLay->addWidget(caption(tr("AUDIO SETUP"), setupCard));

    const QString rawDevice = audio.device.isEmpty() ? tr("No active output device") : audio.device;
    auto* devLabel = new QLabel(setupCard);
    devLabel->setToolTip(rawDevice);
    devLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(c.text.name()));
    devLabel->setText(devLabel->fontMetrics().elidedText(rawDevice, Qt::ElideRight, 264));
    setupLay->addWidget(devLabel);

    auto* specsRow = new QHBoxLayout;
    specsRow->setSpacing(6);
    const auto makeSpecBox = [&](const QString& text) {
        auto* box = new QFrame(setupCard);
        box->setObjectName(QStringLiteral("launcherSpecPill"));
        auto* boxLay = new QHBoxLayout(box);
        boxLay->setContentsMargins(8, 5, 8, 5);
        auto* lbl = new QLabel(text, box);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet(
            QStringLiteral("color: %1; font-size: 8.5pt; font-weight: 600;").arg(c.textDim.name()));
        boxLay->addWidget(lbl);
        return box;
    };
    if (!audio.device.isEmpty() && audio.sampleRate > 0) {
        const double ms = 1000.0 * audio.bufferFrames / audio.sampleRate;
        specsRow->addWidget(makeSpecBox(tr("%1 kHz").arg(audio.sampleRate / 1000.0, 0, 'g', 4)), 1);
        specsRow->addWidget(makeSpecBox(tr("%1 smp · %2 ms").arg(audio.bufferFrames).arg(ms, 0, 'f', 1)), 1);
    } else {
        specsRow->addWidget(makeSpecBox(tr("— kHz")), 1);
        specsRow->addWidget(makeSpecBox(tr("— samples")), 1);
    }
    setupLay->addLayout(specsRow);

    auto* cfgAudioBtn
        = new QPushButton(Theme::icon(QStringLiteral("sliders")), tr("Configure &Audio Device…"), setupCard);
    cfgAudioBtn->setMinimumHeight(34);
    cfgAudioBtn->setCursor(Qt::PointingHandCursor);
    connect(cfgAudioBtn, &QPushButton::clicked, this, [this] { choose(Choice::AudioSettings); });
    setupLay->addWidget(cfgAudioBtn);

    layout->addWidget(setupCard);
    layout->addStretch(1);
    return side;
}

QWidget* LauncherDialog::createRecentPanel()
{
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(26, 20, 26, 16);
    layout->setSpacing(14);

    auto* header = new QHBoxLayout;
    header->setSpacing(10);
    auto* title = new QLabel(tr("Recent Projects"), panel);
    title->setObjectName(QStringLiteral("launcherTitle"));
    header->addWidget(title);

    countBadge_ = new QLabel(QStringLiteral("0"), panel);
    countBadge_->setObjectName(QStringLiteral("launcherCountBadge"));
    header->addWidget(countBadge_);
    header->addStretch(1);

    filters_ = new QButtonGroup(panel);
    filters_->setExclusive(true);
    const auto addFilter = [&](const QString& text, Filter filter) {
        auto* button = new QToolButton(panel);
        button->setObjectName(QStringLiteral("launcherFilter"));
        button->setText(text);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
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
    search_->setFixedWidth(240);
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

QWidget* LauncherDialog::createStatusBar(bool hasOpenProject)
{
    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("launcherStatus"));
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(18, 8, 18, 8);
    layout->setSpacing(14);

    startup_ = new QCheckBox(tr("Show Start Hub when VloomDAW &launches"), bar);
    startup_->setChecked(showAtStartup());
    connect(startup_, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue(kStartupKey, on); });
    layout->addWidget(startup_);

    layout->addStretch(1);

    auto* hint
        = new QLabel(tr("Right-click a project card to pin, reveal in Explorer, or remove from list"), bar);
    hint->setStyleSheet(QStringLiteral("color: %1; font-size: 8.5pt;").arg(Theme::colors().textDim.name()));
    layout->addWidget(hint);

    auto* closeBtn = new QPushButton(hasOpenProject ? tr("Back to &Project") : tr("E&xit VloomDAW"), bar);
    closeBtn->setMinimumWidth(128);
    closeBtn->setMinimumHeight(30);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(closeBtn);
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
        if (!isLocalPath(path))
            continue;
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
    if (countBadge_ != nullptr)
        countBadge_->setText(QString::number(shown));

    columns_ = 0; // placed by layoutCards()
    layoutCards();
    if (shown == 0) {
        auto* empty = new QLabel(paths.isEmpty()
                ? tr("Projects you save or open will appear here with visual arrangement previews.")
                : tr("No project matches the current filter or search."),
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
