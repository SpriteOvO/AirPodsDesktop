#include <QAbstractVideoSurface>
#include <QDir>
#include <QMediaPlayer>
#include <QPainter>
#include <QTest>
#include <QVideoFrame>
#include <QVideoSurfaceFormat>

#include "Source/Gui/Widget/AnimationView.h"

namespace {

bool HasTransparentBorder(const QImage &image)
{
    if (image.isNull()) {
        return false;
    }
    const auto transparent = [&image](int x, int y) { return qAlpha(image.pixel(x, y)) == 0; };
    return transparent(0, 0) && transparent(image.width() - 1, 0) &&
           transparent(0, image.height() - 1) &&
           transparent(image.width() - 1, image.height() - 1) &&
           transparent(image.width() / 2, 0) &&
           transparent(image.width() / 2, image.height() - 1) &&
           transparent(0, image.height() / 2) &&
           transparent(image.width() - 1, image.height() / 2);
}

class CollectingSurface : public QAbstractVideoSurface
{
    Q_OBJECT

public:
    using QAbstractVideoSurface::QAbstractVideoSurface;

    int frameCount{0};
    int framesWithOpaqueBorder{0};
    QImage firstFrame;

    QList<QVideoFrame::PixelFormat>
    supportedPixelFormats(QAbstractVideoBuffer::HandleType type) const override
    {
        if (type != QAbstractVideoBuffer::NoHandle) {
            return {};
        }
        return {
            QVideoFrame::Format_ARGB32,
            QVideoFrame::Format_RGB32,
            QVideoFrame::Format_BGRA32,
            QVideoFrame::Format_BGR32,
            QVideoFrame::Format_RGB24,
            QVideoFrame::Format_BGR24,
            QVideoFrame::Format_RGB565,
            QVideoFrame::Format_YUV420P,
            QVideoFrame::Format_YV12,
            QVideoFrame::Format_NV12,
            QVideoFrame::Format_NV21,
            QVideoFrame::Format_UYVY,
            QVideoFrame::Format_YUYV,
            QVideoFrame::Format_ARGB32_Premultiplied,
            QVideoFrame::Format_BGRA32_Premultiplied,
        };
    }

    bool present(const QVideoFrame &frame) override
    {
        auto image = frame.image();
        if (image.isNull()) {
            return false;
        }
        image = image.scaled(QSize{520, 244}, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        Gui::Widget::Detail::KnockOutAnimationBackground(image);
        ++frameCount;
        if (!HasTransparentBorder(image)) {
            ++framesWithOpaqueBorder;
        }
        if (firstFrame.isNull()) {
            firstFrame = image;
        }
        emit FrameReceived();
        return true;
    }

Q_SIGNALS:
    void FrameReceived();
};

void VerifyTransparentBorder(const QImage &image)
{
    QVERIFY(HasTransparentBorder(image));
}

void SaveComposites(const QString &name, const QImage &frame)
{
    const auto outputDir = QStringLiteral(APD_BINARY_DIR "/AnimationValidation");
    QDir{}.mkpath(outputDir);

    for (const auto &[suffix, background] : {
             std::pair{QStringLiteral("light"), QColor{"#FFFFFF"}},
             std::pair{QStringLiteral("dark"), QColor{"#1C1C1E"}},
         })
    {
        QImage result{frame.size(), QImage::Format_RGB32};
        result.fill(background);
        QPainter painter{&result};
        painter.drawImage(QPoint{}, frame);
        QVERIFY2(result.save(outputDir + '/' + name + '-' + suffix + ".png"), "save failed");
    }
}

} // namespace

class AnimationViewTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void RemovesLightAndDarkMattes()
    {
        for (const auto matte : {QColor{"#FFFFFF"}, QColor{"#000000"}}) {
            QImage image{160, 100, QImage::Format_ARGB32_Premultiplied};
            image.fill(matte);
            QPainter painter{&image};
            painter.setPen(Qt::NoPen);
            painter.setBrush(matte == Qt::white ? QColor{"#202020"} : QColor{"#E0E0E0"});
            painter.drawRoundedRect(QRect{40, 20, 80, 60}, 14, 14);
            painter.setBrush(matte);
            painter.drawEllipse(QRect{70, 40, 20, 20});
            painter.end();

            Gui::Widget::Detail::KnockOutAnimationBackground(image);
            VerifyTransparentBorder(image);
            QCOMPARE(qAlpha(image.pixel(50, 50)), 255);
            QCOMPARE(qAlpha(image.pixel(80, 50)), 255); // enclosed matte-coloured detail survives
        }
    }

    void BundledAnimationsHaveTransparentBorders()
    {
        const QDir videoDir{QStringLiteral(APD_SOURCE_DIR "/Source/Resource/Video")};
        const auto videos = videoDir.entryList({"*.avi"}, QDir::Files, QDir::Name);
        QVERIFY2(!videos.isEmpty(), "No bundled animations found");

        for (const auto &video : videos) {
            CollectingSurface surface;
            QMediaPlayer player;
            player.setMuted(true);
            player.setVideoOutput(&surface);
            player.setMedia(QUrl::fromLocalFile(videoDir.filePath(video)));
            player.setPlaybackRate(4.0);
            player.play();

            QTRY_COMPARE_WITH_TIMEOUT(player.mediaStatus(), QMediaPlayer::EndOfMedia, 15000);
            player.stop();

            QVERIFY2(surface.frameCount > 0, qPrintable(video + ": no frames decoded"));
            QCOMPARE(surface.framesWithOpaqueBorder, 0);
            SaveComposites(QFileInfo{video}.completeBaseName(), surface.firstFrame);

            player.setVideoOutput(static_cast<QAbstractVideoSurface *>(nullptr));
            player.setMedia({});
            surface.stop();
            QCoreApplication::processEvents();
        }
    }
};

QTEST_MAIN(AnimationViewTests)

#include "AnimationViewTests.moc"
