// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/AnimationPaint.hpp"

#include "core/AudioFeatures.hpp"
#include "core/TrailFade.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"

#include <QLinearGradient>
#include <QPaintDevice>
#include <QPainter>
#include <QRadialGradient>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace vloomdaw::motion_paint {

namespace {

namespace style = editor_style;

constexpr std::size_t kSprites = 48; ///< tinted, sized glow sprites kept
constexpr int kNoiseSize = 128; ///< px of the mist texture (it tiles)

/// A glow sprite of one colour and size, most recently used first.
struct Sprite {
    QRgb colour = 0;
    int pixels = 0; ///< its width and height in device pixels
    QPixmap pixmap;
};

std::array<Sprite, kSprites>& sprites()
{
    static std::array<Sprite, kSprites> cache{};
    return cache;
}

QPixmap makeSprite(const QColor& colour, int pixels, qreal ratio)
{
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    const double half = pixels / 2.0;
    QRadialGradient gradient(QPointF(half, half), half);
    // A bright core and a long soft falloff: reads as light on a dark display.
    gradient.setColorAt(0.0, faded(colour, 1.0));
    gradient.setColorAt(0.18, faded(colour, 0.55));
    gradient.setColorAt(0.45, faded(colour, 0.16));
    gradient.setColorAt(1.0, faded(colour, 0.0));
    p.setPen(Qt::NoPen);
    p.setBrush(gradient);
    p.drawEllipse(QRectF(0, 0, pixels, pixels));
    p.end();
    QPixmap pixmap = QPixmap::fromImage(std::move(image));
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}

/// The sprite of `colour` `pixels` device pixels wide: made once, then
/// drawn unscaled (a blit, no smoothing).
QPixmap sprite(const QColor& colour, int pixels, qreal ratio)
{
    auto& cache = sprites();
    const QRgb key = colour.rgba();
    for (std::size_t i = 0; i < cache.size(); ++i) {
        if (cache[i].colour == key && cache[i].pixels == pixels && !cache[i].pixmap.isNull()
            && cache[i].pixmap.devicePixelRatio() == ratio) {
            if (i > 0)
                std::rotate(cache.begin(), cache.begin() + static_cast<std::ptrdiff_t>(i),
                    cache.begin() + static_cast<std::ptrdiff_t>(i) + 1);
            return cache[0].pixmap;
        }
    }
    std::rotate(cache.begin(), cache.end() - 1, cache.end()); // the oldest to the front, replaced
    cache[0] = Sprite{.colour = key, .pixels = pixels, .pixmap = makeSprite(colour, pixels, ratio)};
    return cache[0].pixmap;
}

/// A smooth, tileable value noise in 0 .. 1 at (x, y) of a `period`-cell lattice.
double valueNoise(const std::vector<double>& lattice, int period, double x, double y)
{
    const auto at = [&](int i, int j) {
        return lattice[static_cast<std::size_t>(
            ((j % period + period) % period) * period + ((i % period + period) % period))];
    };
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0;
    const double fy = y - y0;
    const double sx = fx * fx * (3.0 - 2.0 * fx);
    const double sy = fy * fy * (3.0 - 2.0 * fy);
    const double top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
    const double bottom = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
    return top + (bottom - top) * sy;
}

QImage makeMist()
{
    // Fixed seeds: the same mist every run.
    std::uint32_t seed = 0x2545F491U;
    const auto next = [&seed] {
        seed ^= seed << 13U;
        seed ^= seed >> 17U;
        seed ^= seed << 5U;
        return static_cast<double>(seed & 0xFFFFU) / 65535.0;
    };
    constexpr std::array<int, 4> periods{4, 8, 16, 32};
    std::array<std::vector<double>, periods.size()> lattices;
    for (std::size_t o = 0; o < periods.size(); ++o) {
        lattices[o].resize(static_cast<std::size_t>(periods[o] * periods[o]));
        for (double& v : lattices[o])
            v = next();
    }
    QImage image(kNoiseSize, kNoiseSize, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < kNoiseSize; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < kNoiseSize; ++x) {
            double sum = 0.0;
            double weight = 0.0;
            double amplitude = 1.0;
            for (std::size_t o = 0; o < periods.size(); ++o) {
                const double scale = static_cast<double>(periods[o]) / kNoiseSize;
                sum += amplitude * valueNoise(lattices[o], periods[o], x * scale, y * scale);
                weight += amplitude;
                amplitude *= 0.55;
            }
            const double n = std::clamp((sum / weight - 0.5) * 1.8 + 0.5, 0.0, 1.0);
            const int alpha = static_cast<int>(std::lround(255.0 * (0.3 + 0.7 * n)));
            line[x] = qRgba(alpha, alpha, alpha, alpha); // premultiplied white
        }
    }
    return image;
}

std::uint64_t mixBits(std::uint64_t key, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i) {
        key ^= (value >> (8U * static_cast<unsigned>(i))) & 0xFFU;
        key *= 1099511628211ULL;
    }
    return key;
}

} // namespace

QColor faded(QColor colour, double alpha)
{
    colour.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0) * colour.alphaF()));
    return colour;
}

void glowStroke(QPainter& painter, const QPainterPath& path, const QColor& colour, double intensity,
    double radius, double spacing)
{
    const bool antialiased = painter.testRenderHint(QPainter::Antialiasing);
    const QPen pen = painter.pen();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(colour, 1.0)); // 1 px: Qt's fast cosmetic path
    painter.drawPath(path);
    painter.setPen(pen);
    painter.setRenderHint(QPainter::Antialiasing, antialiased);
    if (!(intensity > 0.0) || !(radius > 0.0) || path.elementCount() == 0)
        return;
    // The halo: sprites along the line, a fixed distance apart; overlapping
    // stamps add up, so each is fainter the closer they are.
    const double step = spacing > 0.0 ? spacing : std::max(1.0, radius / 2.0);
    const double each = std::min(1.0, intensity) * std::min(1.0, step / radius);
    GlowBatch glow(painter, colour);
    QPointF last = path.elementAt(0);
    double carry = 0.0; // distance since the last stamp
    glow.dot(last, radius, each);
    for (int i = 1; i < path.elementCount(); ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        const QPointF next = element;
        if (element.isMoveTo()) {
            last = next;
            carry = 0.0;
            glow.dot(last, radius, each);
            continue;
        }
        const QPointF delta = next - last;
        const double length = std::hypot(delta.x(), delta.y());
        double along = step - carry;
        while (along <= length) {
            glow.dot(last + delta * (along / length), radius, each);
            along += step;
        }
        carry = length - (along - step);
        last = next;
    }
}

void glowLine(QPainter& painter, const QLineF& line, const QColor& colour, double intensity, double radius)
{
    painter.save();
    if (intensity > 0.0 && radius > 0.0) {
        const bool across = std::abs(line.dy()) <= std::abs(line.dx());
        const QRectF span = QRectF(line.p1(), line.p2()).normalized();
        const QRectF band
            = across ? span.adjusted(0, -radius, 0, radius) : span.adjusted(-radius, 0, radius, 0);
        QLinearGradient soft = across ? QLinearGradient(band.topLeft(), band.bottomLeft())
                                      : QLinearGradient(band.topLeft(), band.topRight());
        const double a = std::min(1.0, intensity);
        soft.setColorAt(0.0, faded(colour, 0.0));
        soft.setColorAt(0.35, faded(colour, 0.18 * a));
        soft.setColorAt(0.5, faded(colour, 0.55 * a));
        soft.setColorAt(0.65, faded(colour, 0.18 * a));
        soft.setColorAt(1.0, faded(colour, 0.0));
        painter.setCompositionMode(QPainter::CompositionMode_Plus);
        painter.fillRect(band, soft);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    painter.setPen(QPen(colour, 1.0));
    painter.drawLine(line);
    painter.restore();
}

void glowDot(QPainter& painter, QPointF centre, double radius, const QColor& colour, double intensity)
{
    if (!(intensity > 0.0) || !(radius > 0.0))
        return;
    GlowBatch glow(painter, colour);
    glow.dot(centre, radius, intensity);
}

GlowBatch::GlowBatch(QPainter& painter, const QColor& colour)
    : painter_(painter)
    , colour_(colour)
    , mode_(painter.compositionMode())
    , opacity_(painter.opacity())
    , ratio_(painter.device() != nullptr ? painter.device()->devicePixelRatio() : 1.0)
{
    painter_.setCompositionMode(QPainter::CompositionMode_Plus);
}

GlowBatch::~GlowBatch()
{
    painter_.setCompositionMode(static_cast<QPainter::CompositionMode>(mode_));
    painter_.setOpacity(opacity_);
}

void GlowBatch::dot(QPointF centre, double radius, double intensity)
{
    if (!(intensity > 0.0) || !(radius > 0.0))
        return;
    const int pixels = std::clamp(static_cast<int>(std::lround(2.0 * radius * ratio_)), 2, 256);
    const QPixmap image = sprite(colour_, pixels, ratio_);
    const double size = pixels / ratio_;
    painter_.setOpacity(opacity_ * std::min(1.0, intensity));
    painter_.drawPixmap(QPointF(centre.x() - size / 2.0, centre.y() - size / 2.0), image);
}

const QImage& mistTexture()
{
    static const QImage texture = makeMist();
    return texture;
}

void paintWaveformRibbon(QPainter& painter, std::span<const float> samples, const QRectF& rect, int columns,
    const QColor& fill, const QColor& edge, double glow)
{
    constexpr std::size_t kMostColumns = 256;
    const auto count = static_cast<std::size_t>(std::clamp(columns, 0, static_cast<int>(kMostColumns)));
    if (samples.empty() || count < 2 || rect.width() <= 0.0)
        return;
    const double mid = rect.center().y();
    const double half = rect.height() / 2.0;
    const auto lift = [](float v) {
        const double magnitude = std::abs(static_cast<double>(v));
        const double level = magnitude > 0.0 ? motion::normalisedLevel(20.0 * std::log10(magnitude)) : 0.0;
        return v < 0.0F ? -level : level;
    };
    std::array<QPointF, kMostColumns> upper{};
    std::array<QPointF, kMostColumns> lower{};
    const double width = rect.width() / static_cast<double>(count);
    for (std::size_t c = 0; c < count; ++c) {
        const std::size_t from = c * samples.size() / count;
        const std::size_t to = std::min(samples.size(), std::max(from + 1, (c + 1) * samples.size() / count));
        float low = samples[from];
        float high = samples[from];
        for (std::size_t i = from; i < to; ++i) {
            low = std::min(low, samples[i]);
            high = std::max(high, samples[i]);
        }
        const double x = rect.left() + width * (static_cast<double>(c) + 0.5);
        upper[c] = QPointF(x, mid - lift(high) * half);
        lower[c] = QPointF(x, mid - lift(low) * half);
    }
    if (fill.alpha() > 0) {
        const bool antialiased = painter.testRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::Antialiasing, false);
        for (std::size_t c = 0; c < count; ++c) // a bar a column: no path to fill
            painter.fillRect(QRectF(upper[c].x() - width / 2.0, upper[c].y(), width,
                                 std::max(1.0, lower[c].y() - upper[c].y())),
                fill);
        painter.setRenderHint(QPainter::Antialiasing, antialiased);
    }
    if (edge.alpha() == 0)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(edge, 1.0));
    painter.drawPolyline(upper.data(), static_cast<int>(count));
    painter.drawPolyline(lower.data(), static_cast<int>(count));
    if (glow > 0.0) {
        const double radius = std::clamp(width * 3.0, 4.0, 10.0);
        const std::size_t every = std::max<std::size_t>(1, static_cast<std::size_t>(radius / width));
        GlowBatch halo(painter, edge);
        for (std::size_t c = 0; c < count; c += every) {
            halo.dot(upper[c], radius, glow * 0.6);
            halo.dot(lower[c], radius, glow * 0.6);
        }
    }
    painter.restore();
}

std::uint64_t keyWith(std::uint64_t key, double value)
{
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return mixBits(key, bits);
}

// --- LayerCache ----------------------------------------------------------------

void LayerCache::draw(
    QPainter& painter, const QWidget& widget, std::uint64_t key, const std::function<void(QPainter&)>& paint)
{
    const qreal ratio = widget.devicePixelRatioF();
    std::uint64_t full = keyWith(key, static_cast<double>(widget.width()));
    full = keyWith(full, static_cast<double>(widget.height()));
    full = keyWith(full, ratio);
    full = mixBits(full, static_cast<std::uint64_t>(style::background().rgba()));
    full = mixBits(full, static_cast<std::uint64_t>(style::accent().rgba()));
    full = mixBits(full, static_cast<std::uint64_t>(style::text().rgba()));
    if (!valid_ || full != key_) {
        const QSize pixels(static_cast<int>(std::ceil(widget.width() * ratio)),
            static_cast<int>(std::ceil(widget.height() * ratio)));
        if (pixmap_.size() != pixels)
            pixmap_ = QPixmap(pixels);
        pixmap_.setDevicePixelRatio(ratio);
        pixmap_.fill(Qt::transparent);
        QPainter layer(&pixmap_);
        layer.setFont(widget.font());
        paint(layer);
        layer.end();
        key_ = full;
        valid_ = true;
    }
    painter.drawPixmap(QPointF(0, 0), pixmap_);
}

// --- TrailBuffer ---------------------------------------------------------------

TrailBuffer::TrailBuffer(double resolution)
    : resolution_(std::clamp(resolution, 0.25, 1.0))
{
}

void TrailBuffer::setArea(const QRectF& area)
{
    area_ = area;
    ensure();
}

void TrailBuffer::ensure()
{
    const QSize size(std::max(1, static_cast<int>(std::ceil(area_.width() * resolution_))),
        std::max(1, static_cast<int>(std::ceil(area_.height() * resolution_))));
    if (front_.size() == size)
        return;
    front_ = QImage(size, QImage::Format_ARGB32_Premultiplied);
    back_ = QImage(size, QImage::Format_ARGB32_Premultiplied);
    scratch_ = QImage(size, QImage::Format_ARGB32_Premultiplied);
    front_.fill(Qt::transparent);
    back_.fill(Qt::transparent);
    strength_ = 0.0;
    pendingKeep_ = 1.0;
}

void TrailBuffer::advance(double keep, const QTransform& motion, double soften)
{
    ensure();
    if (!isVisible())
        return; // all transparent: nothing to move
    // 8 bits a channel: a fade of a fraction of a per cent a frame would
    // round to nothing, so it is saved up and applied once it reaches 4 %,
    // by fadePremultiplied(), which rounds down: faint pixels end at zero
    // instead of a grey residue that QPainter's opacity would leave.
    pendingKeep_ *= std::clamp(keep, 0.0, 1.0);
    // Softening: half a buffer pixel (times `soften`) to one side, then the
    // other, every frame; the bilinear sampling of the shift blurs it.
    const double nudge = 0.5 * std::clamp(soften, 0.0, 1.0) * (flip_ ? -1.0 : 1.0);
    flip_ = !flip_;
    const QTransform toBuffer = QTransform::fromTranslate(-area_.left(), -area_.top())
        * QTransform::fromScale(resolution_, resolution_);
    const QTransform step = toBuffer.inverted() * motion * toBuffer * QTransform::fromTranslate(nudge, nudge);

    back_.fill(Qt::transparent);
    QPainter p(&back_);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setTransform(step);
    p.drawImage(QPointF(0, 0), front_);
    p.end();
    std::swap(front_, back_);
    if (pendingKeep_ < 0.96) {
        // What is left decides isVisible(): it clears when the last pixel goes.
        const auto alpha = motion::fadePremultiplied(
            std::span<std::uint32_t>(reinterpret_cast<std::uint32_t*>(front_.bits()),
                static_cast<std::size_t>(front_.sizeInBytes()) / sizeof(std::uint32_t)),
            pendingKeep_);
        strength_ = static_cast<double>(alpha) / 255.0;
        pendingKeep_ = 1.0;
    }
}

void TrailBuffer::ink(const std::function<void(QPainter&)>& paint)
{
    ensure();
    QPainter p(&front_);
    // Over, not Plus: ink laid frame after frame tends to its own colour;
    // adding would saturate each channel on its own and burn to white.
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setTransform(QTransform::fromTranslate(-area_.left(), -area_.top())
        * QTransform::fromScale(resolution_, resolution_));
    paint(p);
    p.end();
    strength_ = 1.0;
}

void TrailBuffer::paint(QPainter& painter, double opacity, double mist, QPointF drift, double fadeTop) const
{
    if (!isVisible() || !(opacity > 0.0))
        return;
    {
        QPainter p(&scratch_);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(QPointF(0, 0), front_);
        const QRectF all(QPointF(0, 0), QSizeF(scratch_.size()));
        if (mist > 0.0) {
            // Broken up by the noise, drifting: mist rather than a smear.
            p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            QBrush noise(mistTexture());
            noise.setTransform(QTransform::fromTranslate(drift.x() * resolution_, drift.y() * resolution_));
            p.setOpacity(std::min(1.0, mist));
            p.fillRect(all, noise);
            p.setOpacity(1.0);
        }
        if (fadeTop > 0.0) {
            p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            QLinearGradient top(all.topLeft(), QPointF(0, all.height() * 0.6));
            top.setColorAt(
                0.0, QColor(0, 0, 0, static_cast<int>(std::lround(255.0 * std::min(1.0, fadeTop)))));
            top.setColorAt(1.0, QColor(0, 0, 0, 0));
            p.fillRect(all, top);
        }
    }
    painter.save();
    painter.setOpacity(std::min(1.0, opacity));
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(area_, scratch_);
    painter.restore();
}

void TrailBuffer::clear()
{
    front_.fill(Qt::transparent);
    strength_ = 0.0;
    pendingKeep_ = 1.0;
}

} // namespace vloomdaw::motion_paint
