// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/mixer/MixerStrip.hpp"

#include "core/Decibels.hpp"
#include "ui/Theme.hpp"
#include "ui/mixer/MixerSlotRack.hpp"
#include "ui/plugins/PluginDrag.hpp"
#include "ui/widgets/ColorMenu/ColorMenu.hpp"
#include "ui/widgets/Fader/Fader.hpp"
#include "ui/widgets/Knob/Knob.hpp"
#include "ui/widgets/LevelMeter/LevelMeter.hpp"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr int kInsertWidth = 68;
constexpr int kInsertWideWidth = 88;
constexpr int kMasterWidth = 76;
constexpr int kMasterWideWidth = 96;
constexpr int kMinStripHeight = 250;
constexpr int kMinFaderHeight = 76;

QString panText(double percent)
{
    if (std::abs(percent) < 0.5)
        return MixerStrip::tr("Centre");
    return MixerStrip::tr("%1% %2").arg(QString::number(std::abs(percent), 'f', 0),
        percent < 0 ? MixerStrip::tr("left") : MixerStrip::tr("right"));
}

} // namespace

MixerStrip::MixerStrip(Kind kind, const QString& name, const QString& number, QWidget* parent)
    : QFrame(parent)
    , kind_(kind)
{
    setObjectName(QStringLiteral("mixerStrip"));
    setProperty("master", kind == Kind::Master); // styled differently in Theme
    setFixedWidth(kind == Kind::Master ? kMasterWidth : kInsertWidth);
    setMinimumHeight(kMinStripHeight);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 7, 4, 3);
    layout->setSpacing(2);

    name_ = new QLabel(this);
    name_->setObjectName(QStringLiteral("stripName"));
    name_->setAlignment(Qt::AlignCenter);
    name_->setFixedHeight(17);
    layout->addWidget(name_);
    // Takes the strip's width whatever the text: a long name is cut, not widening it.
    name_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    name_->installEventFilter(this); // re-cut on resize / font; inserts: double-click to rename

    pan_ = new Knob(tr("PAN"), -100.0, 100.0, 0.0, this);
    pan_->setDescription(tr("Pan"));
    pan_->setValueText(panText);
    pan_->installEventFilter(this);
    connect(pan_, &Knob::valueChanged, this, [this](double percent) { emit panChanged(percent / 100.0); });
    layout->addWidget(pan_, 0, Qt::AlignHCenter);

    {
        // Compactable FX rack: shows all 9 slots when tall, or shrinks down to
        // 4 wheel-scrollable slots when the mixer is collapsed vertically.
        fxRack_ = new MixerSlotRack(this);
        fxRack_->setFixedWidth(kind == Kind::Master ? 66 : 60);
        fxRack_->installEventFilter(this);
        connect(fxRack_, &MixerSlotRack::bypassToggled, this, [this](std::size_t slot, bool bypassed) {
            emit clicked();
            emit effectBypassToggled(slot, bypassed);
        });
        connect(fxRack_, &MixerSlotRack::slotClicked, this, [this](std::size_t slot) {
            emit clicked();
            emit effectClicked(slot);
        });
        connect(fxRack_, &MixerSlotRack::slotMenuRequested, this, [this](std::size_t slot, const QPoint& at) {
            emit clicked();
            emit effectMenuRequested(slot, at);
        });
        connect(fxRack_, &MixerSlotRack::pluginDropped, this, &MixerStrip::pluginDroppedOnSlot);
        layout->addWidget(fxRack_, 0, Qt::AlignHCenter);
        setAcceptDrops(true);
    }

    auto* levels = new QHBoxLayout;
    levels->setSpacing(4);
    meter_ = new LevelMeter(this);
    meter_->setMinimumHeight(kMinFaderHeight);
    meter_->installEventFilter(this);
    fader_ = new Fader(this);
    fader_->setMinimumHeight(kMinFaderHeight);
    fader_->installEventFilter(this);
    connect(fader_, &Fader::valueChanged, this, [this](double position) {
        showDb(position);
        emit faderChanged(position);
    });
    levels->addStretch();
    levels->addWidget(meter_);
    levels->addWidget(fader_);
    levels->addStretch();
    layout->addLayout(levels, 1);

    db_ = new QLabel(this);
    db_->setObjectName(QStringLiteral("stripDb"));
    db_->setAlignment(Qt::AlignCenter);
    db_->setFixedHeight(15);
    layout->addWidget(db_);
    showDb(fader_->value());

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    const auto addToggle = [&](const QString& text, const QString& objectName, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setObjectName(objectName);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setFixedHeight(19);
        b->installEventFilter(this);
        buttons->addWidget(b);
        return b;
    };
    mute_ = addToggle(QStringLiteral("M"), QStringLiteral("muteButton"), QString());
    connect(mute_, &QToolButton::toggled, this, &MixerStrip::muteToggled);
    if (kind == Kind::Insert) {
        solo_ = addToggle(QStringLiteral("S"), QStringLiteral("soloButton"), QString());
        connect(solo_, &QToolButton::toggled, this, &MixerStrip::soloToggled);
    }
    layout->addLayout(buttons);

    number_ = new QLabel(this);
    number_->setObjectName(QStringLiteral("stripNumber"));
    number_->setAlignment(Qt::AlignCenter);
    number_->setFixedHeight(13);
    layout->addWidget(number_);

    route_ = new QLabel(kind == Kind::Insert ? tr("→ Master") : tr("Main Out"), this);
    route_->setObjectName(QStringLiteral("stripRoute"));
    route_->setAlignment(Qt::AlignCenter);
    route_->setFixedHeight(12);
    route_->setToolTip(kind == Kind::Insert ? tr("Signal routed to Master") : tr("Master audio output"));
    layout->addWidget(route_);
    setLabel(name, number);
    syncFxSlotCount();
}

QSize MixerStrip::minimumSizeHint() const { return {width(), kMinStripHeight}; }

QSize MixerStrip::sizeHint() const { return {width(), 364}; }

void MixerStrip::syncFxSlotCount()
{
    if (fxRack_ == nullptr)
        return;
    // Fixed non-FX/non-fader height: margins (10) + 7 gaps (14) + name (17) + pan + db (15)
    // + buttons (19) + number (13) + route (12) = 100 + pan.
    const int panH = pan_ != nullptr ? pan_->sizeHint().height() : 39;
    const int chromeAndMinFader = 102 + panH + kMinFaderHeight;
    const int availForFx = height() - chromeAndMinFader;
    const int visibleSlots = std::clamp((availForFx - 2) / MixerSlotRack::kSlotHeight,
        MixerSlotRack::kMinVisibleSlots, static_cast<int>(MixerModel::kMaxEffects));
    fxRack_->setVisibleSlotCount(visibleSlots);
}

void MixerStrip::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    syncFxSlotCount();
}

void MixerStrip::setSelected(bool selected)
{
    if (selected_ == selected)
        return;
    selected_ = selected;
    setProperty("selected", selected);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void MixerStrip::setValues(double fader, double pan, bool muted, bool soloed)
{
    const QSignalBlocker blockFader(fader_);
    const QSignalBlocker blockPan(pan_);
    const QSignalBlocker blockMute(mute_);
    fader_->setValue(fader);
    pan_->setValue(pan * 100.0);
    mute_->setChecked(muted);
    if (solo_ != nullptr) {
        const QSignalBlocker blockSolo(solo_);
        solo_->setChecked(soloed);
    }
    showDb(fader_->value());
}

void MixerStrip::setEffects(const std::vector<PluginSlot>& effects)
{
    effectCount_ = 0;
    for (const PluginSlot& s : effects) {
        if (!s.name.empty())
            ++effectCount_;
    }
    if (fxRack_ != nullptr)
        fxRack_->setEffects(effects);
}

void MixerStrip::setWideMode(bool wide)
{
    if (wideMode_ == wide)
        return;
    wideMode_ = wide;
    const int stripW = (kind_ == Kind::Master) ? (wide ? kMasterWideWidth : kMasterWidth)
                                               : (wide ? kInsertWideWidth : kInsertWidth);
    setFixedWidth(stripW);
    if (fxRack_ != nullptr)
        fxRack_->setFixedWidth(stripW - 8);
    elideName();
    updateGeometry();
    update();
}

void MixerStrip::setFxRackVisible(bool visible)
{
    if (fxRack_ == nullptr)
        return;
    fxRack_->setVisible(visible);
    if (visible)
        syncFxSlotCount();
}

void MixerStrip::setSourceFlags(bool hasAudio, bool hasInstrument)
{
    hasAudioSource_ = hasAudio;
    hasInstrumentSource_ = hasInstrument;
}

void MixerStrip::setEffectCount(std::size_t count) { effectCount_ = count; }

void MixerStrip::dragEnterEvent(QDragEnterEvent* event)
{
    if (droppedPlugin(event->mimeData()))
        event->acceptProposedAction();
}

void MixerStrip::dropEvent(QDropEvent* event)
{
    const auto plugin = droppedPlugin(event->mimeData());
    if (!plugin)
        return;
    event->acceptProposedAction();
    emit pluginDropped(*plugin);
}

void MixerStrip::setColor(std::optional<std::uint32_t> color)
{
    if (color == color_)
        return;
    color_ = color;
    update();
}

void MixerStrip::mousePressEvent(QMouseEvent* event)
{
    emit clicked();
    if (event->button() == Qt::LeftButton && event->position().y() <= 8.0) {
        emit colorPickRequested(mapToGlobal(QPoint(0, 8)));
        return;
    }
    QFrame::mousePressEvent(event);
}

void MixerStrip::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);
    const Theme::Colors& c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Fender Studio-style active channel highlight wash + crisp inner outline.
    if (selected_) {
        QColor tint = color_ ? toQColor(*color_) : c.accent;
        tint.setAlpha(24);
        p.setPen(Qt::NoPen);
        p.setBrush(tint);
        p.drawRoundedRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0), 5.0, 5.0);

        QColor ring = color_ ? toQColor(*color_).lighter(120) : c.accent;
        ring.setAlpha(205);
        p.setPen(QPen(ring, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.6, 0.6, -0.6, -0.6), 5.5, 5.5);
    }

    p.setPen(Qt::NoPen);
    QColor bar = color_ ? toQColor(*color_) : (selected_ ? c.accent : c.textDim);
    if (!color_ && !selected_)
        bar.setAlpha(70);
    p.setBrush(bar);
    p.drawRoundedRect(QRectF(4, 2, width() - 8, selected_ ? 4.5 : 4.0), 1.5, 1.5);
}

bool MixerStrip::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress)
        emit clicked();

    if (watched == name_) {
        switch (event->type()) {
        case QEvent::MouseButtonDblClick:
            if (solo_ != nullptr) { // inserts only
                emit renameRequested();
                return true;
            }
            break;
        case QEvent::Resize:
        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::Polish:
            elideName();
            break;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void MixerStrip::elideName()
{
    const QString shown
        = name_->fontMetrics().elidedText(fullName_, Qt::ElideRight, name_->contentsRect().width());
    if (shown != name_->text())
        name_->setText(shown);
}

void MixerStrip::setLabel(const QString& name, const QString& number)
{
    fullName_ = name;
    elideName();
    name_->setToolTip(name);
    number_->setText(number);
    const QString label = solo_ == nullptr ? name : tr("%1 (%2)").arg(name, number);
    fader_->setAccessibleName(tr("%1 volume").arg(label));
    pan_->setAccessibleName(tr("%1 pan").arg(label));
    const QString muteTip = tr("Mute %1").arg(label);
    mute_->setToolTip(muteTip);
    mute_->setAccessibleName(muteTip);
    if (solo_ != nullptr) {
        const QString soloTip = tr("Solo %1 (silences every insert that is not solo)").arg(label);
        solo_->setToolTip(soloTip);
        solo_->setAccessibleName(soloTip);
    }
}

void MixerStrip::setSilenced(bool silenced)
{
    if (property("silenced").toBool() == silenced)
        return;
    setProperty("silenced", silenced);
    style()->unpolish(this);
    style()->polish(this);
    for (QLabel* label : {name_, db_, number_, route_}) {
        label->style()->unpolish(label);
        label->style()->polish(label);
    }
}

void MixerStrip::showDb(double position)
{
    db_->setText(QString::fromStdString(formatDb(faderToGain(position))));
}

} // namespace vloomdaw
