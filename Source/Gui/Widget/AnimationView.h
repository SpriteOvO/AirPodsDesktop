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

// Removes the matte connected to the corners while preserving near-white device surfaces.
// The Max animation opts into removing enclosed matte inside the headband as well.
// Input must be ARGB32 premultiplied. Exposed for deterministic decoded-frame regression tests.
void KnockOutAnimationBackground(QImage &image, bool removeEnclosedBackground = false);

} // namespace Detail

//
// Video output for the device animations.
//
// The animation assets are matted onto opaque white (see `Source/Resource/Video/README.md`), so
// this widget receives every frame through a `QVideoSink`, knocks out the white
// background reachable from the frame border, and paints the result over whatever the parent
// window draws. That keeps the animation usable on a dark card without re-encoding the assets.
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

    void SetRemoveEnclosedBackground(bool enabled);
    void SetFallbackImage(QImage image);
    void SetPlaybackEnabled(bool enabled);

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
    quint64 _generation{0};
    bool _playbackEnabled{true};
    bool _deliveryQueued{false};
    bool _removeEnclosedBackground{false};

    void InvalidatePendingFrames();
    void OnVideoFrameChanged(const QVideoFrame &frame);
    void PresentFrame(QImage frame);
    void RescaleFrame();
};

} // namespace Gui::Widget
