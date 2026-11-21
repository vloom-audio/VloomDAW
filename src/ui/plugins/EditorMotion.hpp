// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/AudioFeatures.hpp"
#include "ui/plugins/builtin/Animation.hpp"

#include <QElapsedTimer>
#include <QObject>

#include <memory>
#include <span>
#include <vector>

class QTimer;
class QWidget;

namespace vloom {
class PluginInstance;
}

namespace vloomdaw {

/// What BuiltinEditorWindow::animate() does, for an editor window that is
/// not built on that shell (the equalizer, the synthesizer): the displays
/// added get advance() once a frame (FrameClock) while `window` shows, is
/// not minimised, "Animate plugin displays" is on and the plugin's scope
/// tap carries sound; the clock is left once the sound has settled and a
/// slow listening timer brings it back. docs/builtin-editors.md, "Animation".
class EditorMotion final : public QObject, private FrameClock::Client {
public:
    /// `manageScope`: switch the instance's scope tap on while the window
    /// shows and the displays animate (false: the editor does it itself).
    EditorMotion(std::shared_ptr<vloom::PluginInstance> instance, double sampleRate, QWidget* window,
        bool manageScope);
    ~EditorMotion() override;
    EditorMotion(const EditorMotion&) = delete;
    EditorMotion& operator=(const EditorMotion&) = delete;

    /// Makes `display` (a widget of the window) move with the sound.
    void add(Animated* display);
    /// What the sound is doing this frame.
    [[nodiscard]] const motion::AudioFeatures& audio() const { return features_; }
    /// The displays get frames now.
    [[nodiscard]] bool isAnimating() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void frame(double dt) override;
    /// Joins or leaves the frame clock (and starts or stops listening) as
    /// the window, the sound and the setting say.
    void updateClock();
    /// The slow timer while the clock idles: sound again wakes the displays.
    void listen();
    void settingChanged(bool enabled);
    [[nodiscard]] bool wanted() const;
    /// What the plugin put out in the last `seconds`; `running`: new audio came.
    std::span<const float> readNewest(double seconds, std::vector<float>& tail, bool& running);

    std::shared_ptr<vloom::PluginInstance> instance_;
    double sampleRate_;
    QWidget* window_;
    bool manageScope_;
    std::vector<Animated*> displays_;
    motion::AudioFeatures features_;
    bool awake_ = false; ///< sound ran lately, or something still moves
    double sinceAudio_ = 1.0; ///< s since a frame got new audio
    std::vector<float> scope_; ///< kScopeFrames: the newest output
    std::vector<float> frameTail_; ///< the end of the last frame's read
    std::vector<float> listenTail_; ///< the end of the last listening read
    QTimer* listener_ = nullptr;
    QElapsedTimer sinceListen_;
};

} // namespace vloomdaw
