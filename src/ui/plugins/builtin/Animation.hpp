// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/AudioFeatures.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <cstdint>
#include <vector>

/// The animation layer of the built-in editors' displays: one frame clock
/// for all of them, the user's setting, and the interface a display
/// implements to move with the sound. docs/builtin-editors.md, "Animation",
/// says how an editor uses it; AnimationPaint.hpp has the drawing helpers.
namespace vloomdaw {

/// Whether the displays animate: the user's "Animate plugin displays"
/// (Settings → General, on unless switched off) and the system's
/// reduced-motion preference (Windows: "Animation effects" off; macOS:
/// "Reduce motion"). Off, every display draws its static picture. The
/// environment variable VLOOMDAW_IGNORE_REDUCED_MOTION makes it ignore the
/// system's preference (offscreen renders, measurements).
class AnimationSettings : public QObject {
    Q_OBJECT

public:
    static AnimationSettings& instance();

    /// The user's setting (AppSettings::animatePluginDisplays).
    void setUserEnabled(bool on);
    [[nodiscard]] bool userEnabled() const { return user_; }
    /// Asks the system again whether it prefers reduced motion; emits
    /// changed() if that changes what enabled() says.
    void refreshSystem();
    [[nodiscard]] bool systemPrefersReducedMotion() const { return reduced_; }
    /// The displays animate.
    [[nodiscard]] bool enabled() const { return user_ && !reduced_; }

signals:
    void changed(bool enabled);

private:
    AnimationSettings();
    bool user_ = true;
    bool reduced_ = false;
};

/// The one clock every animated display moves by: a 60 Hz timer that runs
/// only while some client wants frames (an editor that is shown, not
/// minimised, with sound or something still moving), and the real time
/// between two frames.
class FrameClock : public QObject {
    Q_OBJECT

public:
    static constexpr int kIntervalMs = 16;
    /// The longest frame time handed on (after a stall), s.
    static constexpr double kMaxFrameSeconds = 0.1;

    /// What gets frames.
    class Client {
    public:
        virtual ~Client() = default;
        /// One frame, `dt` seconds after the last (0 .. kMaxFrameSeconds).
        virtual void frame(double dt) = 0;
    };

    /// What the animated displays cost: for a debug overlay and the tests.
    struct Stats {
        std::uint64_t ticks = 0; ///< frames the clock gave
        std::uint64_t paints = 0; ///< paints of animated displays (AnimationPaintTimer)
        double tickSeconds = 0.0; ///< spent in the clients' frame()
        double paintSeconds = 0.0; ///< spent painting animated displays
    };

    static FrameClock& instance();
    /// Removes `client` if the clock exists (safe while the application quits).
    static void leave(Client* client);

    void add(Client* client);
    void remove(Client* client);
    [[nodiscard]] bool contains(const Client* client) const;
    [[nodiscard]] bool isRunning() const { return timer_.isActive(); }

    [[nodiscard]] const Stats& stats() const { return stats_; }
    void resetStats() { stats_ = Stats{}; }
    /// A paint of an animated display took `seconds` (AnimationPaintTimer).
    void addPaint(double seconds)
    {
        ++stats_.paints;
        stats_.paintSeconds += seconds;
    }

private:
    FrameClock();
    void tick();

    QTimer timer_;
    QElapsedTimer elapsed_;
    std::vector<Client*> clients_;
    std::vector<Client*> calling_; ///< a copy for each tick: clients may leave during it
    Stats stats_;
};

/// Times a paintEvent() into FrameClock's stats: put one at its top.
class AnimationPaintTimer {
public:
    AnimationPaintTimer() { timer_.start(); }
    ~AnimationPaintTimer()
    {
        FrameClock::instance().addPaint(static_cast<double>(timer_.nsecsElapsed()) * 1e-9);
    }
    AnimationPaintTimer(const AnimationPaintTimer&) = delete;
    AnimationPaintTimer& operator=(const AnimationPaintTimer&) = delete;

private:
    QElapsedTimer timer_;
};

/// A display that moves with the sound. Implement advance() (and paint
/// what it worked out in paintEvent()) and hand the display to
/// BuiltinEditorWindow::animate(): the shell then calls it once a frame
/// while the window shows and sound runs, and switches it off (static) when
/// the user or the system says so.
class Animated {
public:
    virtual ~Animated() = default;

    /// The display animates now. Off: paint the static picture only.
    [[nodiscard]] bool animationOn() const { return on_; }

    /// One frame: move by `dt` seconds with `audio`, call update() (or
    /// update(rect)) for what changed, and return whether anything still
    /// moves (a trail fading, particles alive). The shell lets the clock
    /// idle once the sound has settled and every display returns false.
    virtual bool advance(const motion::AudioFeatures& audio, double dt) = 0;
    /// The clock goes idle (the sound settled, the window hid): drop what
    /// is left of the motion and repaint the calm picture.
    virtual void rest() { }
    /// animationOn() changed. The default does nothing; a display usually
    /// drops its motion and repaints.
    virtual void animationSwitched(bool /*on*/) { }

    /// The shell's switch (BuiltinEditorWindow); calls animationSwitched().
    void setAnimationOn(bool on)
    {
        if (on == on_)
            return;
        on_ = on;
        animationSwitched(on);
    }

private:
    bool on_ = false;
};

} // namespace vloomdaw
