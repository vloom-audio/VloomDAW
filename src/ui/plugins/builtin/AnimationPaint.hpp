// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QColor>
#include <QImage>
#include <QLineF>
#include <QPainterPath>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QTransform>

#include <cstdint>
#include <functional>
#include <span>

class QPainter;
class QWidget;

/// Drawing helpers of the animated displays (Animation.hpp), made to be
/// cheap at 60 frames a second: nothing here blurs a whole frame or works
/// per pixel on the CPU each frame. Colours come from editor_style.
///
/// What costs in Qt's raster engine (measured, release, 900 x 408): an
/// antialiased line 1 px wide takes Qt's fast cosmetic path (a 225-point
/// trace: 0.07 ms); any wider pen goes through the stroker and is 15 times
/// slower (1.1 ms), a three-layer wide glow 6 ms. Antialiased filled paths
/// cost about as much. Blitting a cached pixmap the size of the display:
/// 0.05 ms; a sprite: 3 us. So glow is sprites stamped along 1 px lines,
/// never wide strokes.
namespace vloomdaw::motion_paint {

/// `colour` at `alpha` (0 .. 1) times its own.
[[nodiscard]] QColor faded(QColor colour, double alpha);

/// A glowing line: `path` (lines only, as a polyline is) drawn 1 px wide in
/// `colour`, with a soft halo of glow sprites `radius` px stamped along it
/// every `spacing` px (0: radius / 2), added at `intensity` (0 .. 1; 0:
/// the line only). Keep the path short where it glows (a trace's newest
/// stretch, an edge): every stamp is a sprite.
void glowStroke(QPainter& painter, const QPainterPath& path, const QColor& colour, double intensity,
    double radius = 6.0, double spacing = 0.0);

/// A glowing straight line across or down the display (an edge, a marker,
/// a bar): a 1 px line of `colour` in a soft band `radius` px either side,
/// a gradient fill rather than sprites, so its length costs nothing.
/// `line` must be horizontal or vertical.
void glowLine(
    QPainter& painter, const QLineF& line, const QColor& colour, double intensity, double radius = 6.0);

/// A round glow at `centre`: a cached radial-gradient sprite of `colour`,
/// `radius` px, added at `intensity` (0 .. 1). Cheap enough for hundreds.
void glowDot(QPainter& painter, QPointF centre, double radius, const QColor& colour, double intensity);

/// Many glow sprites with the painter set up once (sparks, stamps along a
/// line): each dot() is a blit of a sprite made once for its colour and
/// size, about 1 us. Restores the painter's mode and opacity when it goes.
class GlowBatch {
public:
    GlowBatch(QPainter& painter, const QColor& colour);
    ~GlowBatch();
    GlowBatch(const GlowBatch&) = delete;
    GlowBatch& operator=(const GlowBatch&) = delete;

    /// A glow `radius` px round `centre`, added at `intensity` (0 .. 1).
    void dot(QPointF centre, double radius, double intensity);

private:
    QPainter& painter_;
    QColor colour_;
    int mode_; ///< QPainter::CompositionMode before
    double opacity_;
    qreal ratio_; ///< device pixels per px
};

/// A tileable soft noise (a few octaves of smooth value noise), made once:
/// white, its alpha 0.3 .. 1. Scroll it (a brush transform) for mist.
[[nodiscard]] const QImage& mistTexture();

/// The band of a waveform as a ribbon about `rect`'s centre line: each of
/// `columns` columns (up to 256) spans the lowest and highest sample that
/// falls in it, on a log scale (normalisedLevel()) so quiet sound still
/// shows, `rect`'s half height at 0 dB. Filled with `fill` (a bar a column,
/// not a path: cheap), edged with 1 px lines of `edge` (none if
/// transparent) that glow at `glow` (0 .. 1).
void paintWaveformRibbon(QPainter& painter, std::span<const float> samples, const QRectF& rect, int columns,
    const QColor& fill, const QColor& edge, double glow = 0.0);

/// A picture that changes rarely (a grid, the axes, a curve that follows the
/// parameters), kept in a pixmap and drawn again while the display moves.
/// Give it a key that changes with whatever it shows (the size and the theme
/// colours are added for you).
class LayerCache {
public:
    /// Draws the layer, painting it first with `paint` if `key` or the
    /// widget's size or colours changed.
    void draw(QPainter& painter, const QWidget& widget, std::uint64_t key,
        const std::function<void(QPainter&)>& paint);
    void invalidate() { valid_ = false; }

private:
    QPixmap pixmap_;
    std::uint64_t key_ = 0;
    bool valid_ = false;
};

/// Mixes `value` into a cache key (FNV-1a over its bytes).
[[nodiscard]] std::uint64_t keyWith(std::uint64_t key, double value);

/// An afterimage: what is drawn into it stays and fades, moving each frame
/// (a trail, mist that drifts and spreads). It covers `area` of a display at
/// a fraction of its resolution, so smoothing it up when drawn softens it
/// for free, and every step is a few drawImage() calls on a small image.
class TrailBuffer {
public:
    /// `resolution`: of the display's pixels (0.25 .. 1).
    explicit TrailBuffer(double resolution = 0.33);

    /// Covers `area` (display coordinates); forgets what it held if that
    /// changes size.
    void setArea(const QRectF& area);
    [[nodiscard]] QRectF area() const { return area_; }

    /// One frame: what it holds keeps `keep` (0 .. 1) of its strength,
    /// moved by `motion` (display coordinates, e.g. a step to the right and
    /// a spread about a line) and softened by `soften` (0 .. 1: a blur of
    /// up to half a buffer pixel a frame, more at lower resolution). One
    /// drawImage() of the small buffer.
    void advance(double keep, const QTransform& motion, double soften = 0.0);
    /// Draws new ink into it: `paint` gets a painter in display
    /// coordinates, drawing over what is there (SourceOver: ink laid every
    /// frame tends to its own colour and never burns to white).
    void ink(const std::function<void(QPainter&)>& paint);
    /// Draws it into `painter` at `area()`, at `opacity`. With `mist`
    /// (0 .. 1) it is broken up by mistTexture() scrolled to `drift` (px)
    /// and with `fadeTop` (0 .. 1) its top fades out (a damped, darker top).
    void paint(
        QPainter& painter, double opacity, double mist = 0.0, QPointF drift = {}, double fadeTop = 0.0) const;
    /// Empties it.
    void clear();
    /// It still holds something: inked since, or a pixel left at the last fade.
    [[nodiscard]] bool isVisible() const { return strength_ > 0.0; }

private:
    void ensure();

    double resolution_;
    QRectF area_;
    QImage front_;
    QImage back_;
    mutable QImage scratch_;
    double strength_ = 0.0;
    double pendingKeep_ = 1.0; ///< fade not applied yet (8-bit: applied in steps, so a slow fade is not lost)
    bool flip_ = false; ///< the soften offset's side this frame
};

} // namespace vloomdaw::motion_paint
