// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/AboutDialog/AboutDialog.hpp"

#include "ui/Theme.hpp"

#include <vloom/engine/core/Version.hpp>

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QDesktopServices>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRadialGradient>
#include <QStackedWidget>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace vloomdaw {

namespace {

/// Custom-painted flagship studio banner with glowing harmonic waveform,
/// subtle precision grid, app emblem, version pill, and tagline.
class AboutHeroBanner final : public QWidget {
public:
    explicit AboutHeroBanner(QWidget* parent = nullptr)
        : QWidget(parent)
        , logo_(QStringLiteral(":/app/vloomdaw.png"))
    {
        setFixedHeight(148);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QRectF box = QRectF(this->rect());

        const Theme::Colors& tc = Theme::colors();
        const QColor accent = tc.accent;

        // 1. Studio backdrop gradient anchored to the active theme palette
        QLinearGradient bg(box.topLeft(), box.bottomRight());
        bg.setColorAt(0.0, tc.panel.darker(115));
        bg.setColorAt(0.48, tc.window);
        bg.setColorAt(1.0, tc.base.darker(110));
        p.fillRect(box, bg);

        // Soft warm ambient glow behind the emblem
        QColor glowPrimary = accent;
        glowPrimary.setAlpha(48);
        QColor glowSecondary = accent.darker(130);
        glowSecondary.setAlpha(24);
        QRadialGradient emblemGlow(QPointF(68.0, box.height() * 0.5), 96.0);
        emblemGlow.setColorAt(0.0, glowPrimary);
        emblemGlow.setColorAt(0.55, glowSecondary);
        emblemGlow.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(box, emblemGlow);

        // 2. Subtle precision studio grid lines on the right side
        QColor gridPen = tc.text;
        gridPen.setAlpha(12);
        p.setPen(QPen(gridPen, 1.0));
        for (double gx = 180.0; gx < box.width(); gx += 36.0)
            p.drawLine(QPointF(gx, 0.0), QPointF(gx, box.height()));
        for (double gy = 28.0; gy < box.height(); gy += 28.0)
            p.drawLine(QPointF(0.0, gy), QPointF(box.width(), gy));

        // 3. Signature decorative harmonic waveform sweeping across the right half
        const double waveStartX = box.width() * 0.48;
        const double waveEndX = box.width() + 8.0;
        const double midY = box.height() * 0.54;
        if (waveEndX > waveStartX + 20.0) {
            QPainterPath wave1;
            QPainterPath wave2;
            const int steps = 96;
            for (int i = 0; i <= steps; ++i) {
                const double t = static_cast<double>(i) / steps;
                const double x = waveStartX + t * (waveEndX - waveStartX);
                const double env = std::sin(t * 3.141592653589793) * (0.35 + 0.65 * t);
                const double y1 = midY - env * (26.0 * std::sin(t * 14.0) + 9.0 * std::cos(t * 27.0));
                const double y2 = midY - env * (16.0 * std::sin(t * 10.5 + 1.1) + 6.0 * std::sin(t * 21.0));
                if (i == 0) {
                    wave1.moveTo(x, y1);
                    wave2.moveTo(x, y2);
                } else {
                    wave1.lineTo(x, y1);
                    wave2.lineTo(x, y2);
                }
            }

            QColor w0 = accent;
            w0.setAlpha(0);
            QColor w1 = accent;
            w1.setAlpha(85);
            QColor w2 = accent.lighter(120);
            w2.setAlpha(95);
            QColor w3 = tc.playing;
            w3.setAlpha(45);

            QLinearGradient waveGrad(QPointF(waveStartX, midY), QPointF(waveEndX, midY));
            waveGrad.setColorAt(0.0, w0);
            waveGrad.setColorAt(0.35, w1);
            waveGrad.setColorAt(0.75, w2);
            waveGrad.setColorAt(1.0, w3);

            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(waveGrad, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(wave1);

            QColor s0 = tc.playing;
            s0.setAlpha(0);
            QColor s1 = tc.playing;
            s1.setAlpha(55);
            QColor s2 = accent;
            s2.setAlpha(28);
            QLinearGradient subGrad(QPointF(waveStartX, midY), QPointF(waveEndX, midY));
            subGrad.setColorAt(0.0, s0);
            subGrad.setColorAt(0.5, s1);
            subGrad.setColorAt(1.0, s2);
            p.setPen(QPen(subGrad, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(wave2);
        }

        // 4. App Emblem (76x76 rounded icon card)
        const QRectF iconRect(28.0, (box.height() - 76.0) * 0.5, 76.0, 76.0);
        QPainterPath iconClip;
        iconClip.addRoundedRect(iconRect, 16.0, 16.0);
        p.save();
        p.setClipPath(iconClip);
        if (!logo_.isNull()) {
            p.drawPixmap(iconRect.toRect(), logo_);
        } else {
            p.fillRect(iconRect, tc.raised);
        }
        p.restore();
        p.setPen(QPen(tc.border.lighter(135), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(iconRect, 16.0, 16.0);

        // 5. Wordmark, Version Badge & Studio Subtitle
        const double textX = 122.0;

        QFont brandFont = this->font();
        brandFont.setPointSizeF(20.0);
        brandFont.setWeight(QFont::Bold);
        brandFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        p.setFont(brandFont);
        p.setPen(tc.text);
        const QString titleText = QStringLiteral("VloomDAW");
        const QFontMetrics titleFm(brandFont);
        const int titleW = titleFm.horizontalAdvance(titleText);
        p.drawText(QPointF(textX, 54.0), titleText);

        // Version pill right beside VloomDAW
        QFont verFont = this->font();
        verFont.setPointSizeF(8.5);
        verFont.setWeight(QFont::Bold);
        const QFontMetrics verFm(verFont);
        const QString verText = QStringLiteral("v%1").arg(QStringLiteral(VLOOMDAW_VERSION));
        const int pillW = verFm.horizontalAdvance(verText) + 18;
        const QRectF verPill(textX + titleW + 12.0, 36.0, pillW, 21.0);
        QColor pillBorder = accent;
        pillBorder.setAlpha(175);
        QColor pillBg = accent;
        pillBg.setAlpha(36);
        p.setPen(QPen(pillBorder, 1.0));
        p.setBrush(pillBg);
        p.drawRoundedRect(verPill, 10.5, 10.5);
        p.setFont(verFont);
        p.setPen(accent.lighter(125));
        p.drawText(verPill, Qt::AlignCenter, verText);

        // Subtitle
        QFont subFont = this->font();
        subFont.setPointSizeF(9.8);
        subFont.setWeight(QFont::DemiBold);
        p.setFont(subFont);
        p.setPen(tc.text);
        p.drawText(QPointF(textX, 79.0), tr("Open-Source Digital Audio Workstation"));

        // Edition / Architecture line
        QFont metaFont = this->font();
        metaFont.setPointSizeF(8.5);
        p.setFont(metaFont);
        p.setPen(tc.textDim);
        const QString archLabel = tr("64-bit Studio Architecture  ·  960 PPQ  ·  GNU GPL v3+");
        p.drawText(QPointF(textX, 101.0), archLabel);

        // Bottom subtle separator line
        p.setPen(tc.border);
        p.drawLine(QPointF(0.0, box.height() - 0.5), QPointF(box.width(), box.height() - 0.5));
    }

private:
    QPixmap logo_;
};

QFrame* makeSpecTile(const QString& label, const QString& value, QWidget* parent)
{
    auto* tile = new QFrame(parent);
    tile->setObjectName(QStringLiteral("aboutSpecTile"));
    auto* lay = new QVBoxLayout(tile);
    lay->setContentsMargins(12, 9, 12, 9);
    lay->setSpacing(2);

    auto* k = new QLabel(label.toUpper(), tile);
    k->setObjectName(QStringLiteral("aboutSpecKey"));
    lay->addWidget(k);

    auto* v = new QLabel(value, tile);
    v->setObjectName(QStringLiteral("aboutSpecVal"));
    v->setWordWrap(true);
    lay->addWidget(v);
    return tile;
}

} // namespace

AboutDialog::AboutDialog(const AboutRuntimeInfo& runtime, QWidget* parent)
    : QDialog(parent)
    , runtime_(runtime)
{
    setWindowTitle(tr("About VloomDAW"));
    setModal(true);
    setFixedSize(560, 468);

    const Theme::Colors& c = Theme::colors();
    const QColor onAccent = c.accent.lightnessF() > 0.55 ? QColor(15, 18, 22) : QColor(248, 250, 252);
    setStyleSheet(QStringLiteral("QDialog {"
                                 "  background-color: %1;"
                                 "  color: %2;"
                                 "}"
                                 "QPushButton#aboutTabBtn {"
                                 "  background-color: transparent;"
                                 "  color: %3;"
                                 "  border: 1px solid transparent;"
                                 "  border-radius: 5px;"
                                 "  padding: 5px 14px;"
                                 "  font-weight: 600;"
                                 "  font-size: 11.5px;"
                                 "}"
                                 "QPushButton#aboutTabBtn:hover {"
                                 "  color: %2;"
                                 "  background-color: %4;"
                                 "}"
                                 "QPushButton#aboutTabBtn:checked {"
                                 "  background-color: %4;"
                                 "  color: %2;"
                                 "  border: 1px solid %5;"
                                 "}"
                                 "QFrame#aboutSpecTile {"
                                 "  background-color: %6;"
                                 "  border: 1px solid %5;"
                                 "  border-radius: 7px;"
                                 "}"
                                 "QLabel#aboutSpecKey {"
                                 "  color: %3;"
                                 "  font-size: 9.5px;"
                                 "  font-weight: 700;"
                                 "  letter-spacing: 0.6px;"
                                 "}"
                                 "QLabel#aboutSpecVal {"
                                 "  color: %2;"
                                 "  font-size: 11.5px;"
                                 "  font-weight: 600;"
                                 "}"
                                 "QFrame#aboutFeatureBar {"
                                 "  background-color: %6;"
                                 "  border: 1px solid %5;"
                                 "  border-radius: 7px;"
                                 "}"
                                 "QFrame#aboutCreditsCard {"
                                 "  background-color: %6;"
                                 "  border: 1px solid %5;"
                                 "  border-radius: 7px;"
                                 "}"
                                 "QPushButton#aboutLinkBtn {"
                                 "  background-color: %6;"
                                 "  color: %2;"
                                 "  border: 1px solid %5;"
                                 "  border-radius: 5px;"
                                 "  padding: 6px 12px;"
                                 "  font-weight: 600;"
                                 "  font-size: 11px;"
                                 "}"
                                 "QPushButton#aboutLinkBtn:hover {"
                                 "  background-color: %4;"
                                 "  border-color: %7;"
                                 "}"
                                 "QPushButton#aboutPrimaryBtn {"
                                 "  background-color: %7;"
                                 "  color: %9;"
                                 "  border: none;"
                                 "  border-radius: 5px;"
                                 "  padding: 6px 20px;"
                                 "  font-weight: 700;"
                                 "  font-size: 11.5px;"
                                 "}"
                                 "QPushButton#aboutPrimaryBtn:hover {"
                                 "  background-color: %8;"
                                 "}")
            .arg(c.base.name(), c.text.name(), c.textDim.name(), c.raised.name(), c.border.name(),
                c.panel.name(), c.accent.name(), c.accent.lighter(112).name(), onAccent.name()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 1. Hero Banner
    root->addWidget(new AboutHeroBanner(this));

    // 2. Body Area
    auto* body = new QWidget(this);
    auto* bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(20, 14, 20, 14);
    bodyLay->setSpacing(12);

    // Segmented Tab Bar: [ System & Engine ] [ License & Credits ]
    auto* tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(0, 0, 0, 0);
    tabRow->setSpacing(6);

    auto* tabGroup = new QButtonGroup(this);
    tabGroup->setExclusive(true);

    auto* overviewTab = new QPushButton(tr("System & Engine"), body);
    overviewTab->setObjectName(QStringLiteral("aboutTabBtn"));
    overviewTab->setCheckable(true);
    overviewTab->setChecked(true);

    auto* creditsTab = new QPushButton(tr("License & Credits"), body);
    creditsTab->setObjectName(QStringLiteral("aboutTabBtn"));
    creditsTab->setCheckable(true);

    tabGroup->addButton(overviewTab, 0);
    tabGroup->addButton(creditsTab, 1);
    tabRow->addWidget(overviewTab);
    tabRow->addWidget(creditsTab);
    tabRow->addStretch(1);

    auto* copyrightHint = new QLabel(tr("© 2026 Vloom Audio Contributors"), body);
    copyrightHint->setStyleSheet(QStringLiteral("color: %1; font-size: 10.5px;").arg(c.textDim.name()));
    tabRow->addWidget(copyrightHint);
    bodyLay->addLayout(tabRow);

    auto* pages = new QStackedWidget(body);
    connect(tabGroup, &QButtonGroup::idClicked, pages, &QStackedWidget::setCurrentIndex);

    // ---- Page 0: System & Engine Specifications ----
    auto* overviewPage = new QWidget(pages);
    auto* overviewLay = new QVBoxLayout(overviewPage);
    overviewLay->setContentsMargins(0, 0, 0, 0);
    overviewLay->setSpacing(10);

    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    const QString engineSpec
        = tr("vloom-engine %1  ·  32-bit Float DSP").arg(QString::fromLatin1(vloom::engineVersion()));
    const QString platformSpec = QStringLiteral("%1 (%2)  ·  Qt %3")
                                     .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
                                         QString::fromLatin1(qVersion()));
    const QString pluginSpec = runtime_.scannedPluginCount > 0
        ? tr("VST3 · VST2 · CLAP · Built-in (%1 available)").arg(runtime_.scannedPluginCount)
        : tr("VST3 · VST2 · CLAP · Built-in DSP");
    const QString audioSpec
        = runtime_.audioStatus.isEmpty() ? tr("No active output device") : runtime_.audioStatus;

    grid->addWidget(makeSpecTile(tr("Audio Engine"), engineSpec, overviewPage), 0, 0);
    grid->addWidget(makeSpecTile(tr("Plug-in Formats"), pluginSpec, overviewPage), 0, 1);
    grid->addWidget(makeSpecTile(tr("Active Audio I/O"), audioSpec, overviewPage), 1, 0);
    grid->addWidget(makeSpecTile(tr("Platform & Runtime"), platformSpec, overviewPage), 1, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    overviewLay->addLayout(grid);

    auto* featureBar = new QFrame(overviewPage);
    featureBar->setObjectName(QStringLiteral("aboutFeatureBar"));
    auto* featLay = new QVBoxLayout(featureBar);
    featLay->setContentsMargins(14, 10, 14, 10);
    featLay->setSpacing(4);
    auto* featTitle = new QLabel(tr("STUDIO ENGINE CAPABILITIES"), featureBar);
    featTitle->setObjectName(QStringLiteral("aboutSpecKey"));
    featLay->addWidget(featTitle);
    auto* featDesc = new QLabel(
        tr("Lock-free real-time mixing console with Bus / FX / Aux routing & delay compensation  ·  "
           "Out-of-process VST3, VST2 (32/64-bit bridge) & CLAP plug-in hosting  ·  Time-stretching & AI "
           "stem separation."),
        featureBar);
    featDesc->setWordWrap(true);
    featDesc->setStyleSheet(
        QStringLiteral("color: %1; font-size: 11px; line-height: 1.35;").arg(c.textDim.name()));
    featLay->addWidget(featDesc);
    overviewLay->addWidget(featureBar);
    overviewLay->addStretch(1);
    pages->addWidget(overviewPage);

    // ---- Page 1: License & Open-Source Credits ----
    auto* creditsPage = new QWidget(pages);
    auto* creditsLay = new QVBoxLayout(creditsPage);
    creditsLay->setContentsMargins(0, 0, 0, 0);
    creditsLay->setSpacing(10);

    auto* licenseCard = new QFrame(creditsPage);
    licenseCard->setObjectName(QStringLiteral("aboutCreditsCard"));
    auto* licLay = new QVBoxLayout(licenseCard);
    licLay->setContentsMargins(14, 11, 14, 11);
    licLay->setSpacing(6);

    auto* licTitle = new QLabel(tr("GNU GENERAL PUBLIC LICENSE  ·  VERSION 3 OR LATER"), licenseCard);
    licTitle->setObjectName(QStringLiteral("aboutSpecKey"));
    licLay->addWidget(licTitle);

    auto* licBody = new QLabel(
        tr("VloomDAW is free and open-source software: you can redistribute it and/or modify it "
           "under the terms of the GNU General Public License as published by the Free Software "
           "Foundation, either version 3 of the License, or (at your option) any later version."),
        licenseCard);
    licBody->setWordWrap(true);
    licBody->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(c.text.name()));
    licLay->addWidget(licBody);

    auto* ossTitle = new QLabel(tr("CORE LIBRARIES & TRADEMARKS"), licenseCard);
    ossTitle->setObjectName(QStringLiteral("aboutSpecKey"));
    ossTitle->setContentsMargins(0, 4, 0, 0);
    licLay->addWidget(ossTitle);

    auto* ossBody = new QLabel(
        tr("Powered by vloom-engine, vloom-dsp, Qt %1, FFmpeg, Signalsmith Stretch, and nlohmann/json.\n"
           "VST is a registered trademark of Steinberg Media Technologies GmbH. "
           "CLAP is developed by u-he and Bitwig.")
            .arg(QString::fromLatin1(qVersion())),
        licenseCard);
    ossBody->setWordWrap(true);
    ossBody->setStyleSheet(QStringLiteral("color: %1; font-size: 10.5px;").arg(c.textDim.name()));
    licLay->addWidget(ossBody);

    creditsLay->addWidget(licenseCard);
    creditsLay->addStretch(1);
    pages->addWidget(creditsPage);

    bodyLay->addWidget(pages, 1);
    root->addWidget(body, 1);

    // 3. Bottom Action Footer
    auto* footerSep = new QFrame(this);
    footerSep->setFrameShape(QFrame::HLine);
    footerSep->setStyleSheet(QStringLiteral("color: %1;").arg(c.border.name()));
    root->addWidget(footerSep);

    auto* footer = new QWidget(this);
    footer->setStyleSheet(QStringLiteral("background-color: %1;").arg(c.panel.name()));
    auto* footerLay = new QHBoxLayout(footer);
    footerLay->setContentsMargins(20, 10, 20, 10);
    footerLay->setSpacing(8);

    auto* ghBtn = new QPushButton(tr("GitHub"), footer);
    ghBtn->setObjectName(QStringLiteral("aboutLinkBtn"));
    ghBtn->setCursor(Qt::PointingHandCursor);
    ghBtn->setToolTip(QStringLiteral("https://github.com/vloom-audio/VloomDAW"));
    connect(ghBtn, &QPushButton::clicked, this,
        [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/vloom-audio/VloomDAW"))); });
    footerLay->addWidget(ghBtn);

    auto* issueBtn = new QPushButton(tr("Report Issue"), footer);
    issueBtn->setObjectName(QStringLiteral("aboutLinkBtn"));
    issueBtn->setCursor(Qt::PointingHandCursor);
    issueBtn->setToolTip(QStringLiteral("https://github.com/vloom-audio/VloomDAW/issues"));
    connect(issueBtn, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/vloom-audio/VloomDAW/issues")));
    });
    footerLay->addWidget(issueBtn);

    auto* copyBtn = new QPushButton(tr("Copy System Info"), footer);
    copyBtn->setObjectName(QStringLiteral("aboutLinkBtn"));
    copyBtn->setCursor(Qt::PointingHandCursor);
    copyBtn->setToolTip(tr("Copy version and audio diagnostics to clipboard"));
    connect(copyBtn, &QPushButton::clicked, this, [this, copyBtn] {
        if (QClipboard* cb = QApplication::clipboard())
            cb->setText(buildDiagnosticsText());
        copyBtn->setText(tr("Copied!"));
        QTimer::singleShot(1600, copyBtn, [copyBtn] { copyBtn->setText(tr("Copy System Info")); });
    });
    footerLay->addWidget(copyBtn);

    footerLay->addStretch(1);

    auto* closeBtn = new QPushButton(tr("Close"), footer);
    closeBtn->setObjectName(QStringLiteral("aboutPrimaryBtn"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    footerLay->addWidget(closeBtn);

    root->addWidget(footer);
}

QString AboutDialog::buildDiagnosticsText() const
{
    return QStringLiteral("VloomDAW %1\n"
                          "Engine: vloom-engine %2\n"
                          "Qt: %3\n"
                          "OS: %4 (%5)\n"
                          "Audio I/O: %6\n"
                          "Available Plug-ins: %7\n"
                          "License: GNU GPL v3.0 or later\n"
                          "Repository: https://github.com/vloom-audio/VloomDAW")
        .arg(QStringLiteral(VLOOMDAW_VERSION), QString::fromLatin1(vloom::engineVersion()),
            QString::fromLatin1(qVersion()), QSysInfo::prettyProductName(),
            QSysInfo::currentCpuArchitecture(),
            runtime_.audioStatus.isEmpty() ? QStringLiteral("None") : runtime_.audioStatus)
        .arg(runtime_.scannedPluginCount);
}

} // namespace vloomdaw
