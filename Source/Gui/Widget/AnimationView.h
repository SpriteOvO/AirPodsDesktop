//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021-2022 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#pragma once

#include <mutex>

#include <QImage>
#include <QWidget>

class QVideoFrame;
class QVideoSink;

namespace Gui::Widget {

namespace Detail {

// Removes either the generated chroma matte or a legacy white matte while preserving product
// surfaces.
// Input must be ARGB32 premultiplied. Exposed for deterministic decoded-frame regression tests.
void KnockOutAnimationBackground(QImage &image);

} // namespace Detail

//
// Video output for the device animations.
//
// The animation assets use an opaque chroma matte, so this widget receives every frame through a
// `QVideoSink`, reconstructs transparent antialiased edges, and paints the result over whatever
// the parent window draws. Legacy white-matte assets remain supported as well.
//
class AnimationView : public QWidget
{
    Q_OBJECT

public:
    explicit AnimationView(QWidget *parent = nullptr);
    ~AnimationView() override;

    // Hand this to `QMediaPlayer::setVideoSink()`.
    QVideoSink *VideoSink() const;

    // Drops the last frame so nothing stale is painted the next time the widget shows.
    void Clear();

    // The poster of the video. It is what the view shows whenever no video frame is there to
    // draw: before the first one arrives, and if the video cannot be played.
    void SetFallbackImage(QImage image);
    // A picture to show in place of the video and its poster, for scenes that do not turn.
    // `SetShowStill` has no effect while there is no such picture.
    void SetStillImage(QImage image);
    void SetShowStill(bool show);
    void SetPlaybackEnabled(bool enabled);

    // Whole-view opacity, so the picture can fade while the video keeps playing.
    void SetOpacity(qreal opacity);
    qreal Opacity() const;
    // Whether a decoded video frame, rather than a still picture, is being shown.
    bool IsShowingVideo() const;

Q_SIGNALS:
    void Clicked();
    void FramePresented();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QVideoSink *_videoSink;
    std::mutex _frameMutex;
    QImage _pendingFrame;
    QImage _frame;  // processed, source resolution, ARGB32
    QImage _scaled; // `_frame` fitted to the widget
    QImage _fallback;
    QImage _still;
    bool _showStill{false};
    quint64 _generation{0};
    bool _playbackEnabled{true};
    bool _deliveryQueued{false};
    qreal _opacity{1.0};

    void InvalidatePendingFrames();
    void OnVideoFrameChanged(const QVideoFrame &frame);
    void PresentFrame(QImage frame);
    void RescaleFrame();
};

} // namespace Gui::Widget
