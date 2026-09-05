#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QMediaPlayer>
#include <QPainter>
#include <QTest>
#include <QVideoFrame>
#include <QVideoSink>

#include "Source/Gui/Widget/AnimationView.h"

namespace {

bool HasTransparentBackgroundSamples(const QImage &image)
{
    if (image.isNull()) {
        return false;
    }
    const auto transparent = [&image](int x, int y) { return qAlpha(image.pixel(x, y)) == 0; };
    return transparent(0, 0) && transparent(image.width() - 1, 0) &&
           transparent(0, image.height() - 1) &&
           transparent(image.width() - 1, image.height() - 1) &&
           transparent(image.width() / 2, 0) &&
           transparent(image.width() / 2, image.height() - 1) && transparent(0, image.height() / 2);
}

class CollectingSink : public QObject
{
    Q_OBJECT

public:
    CollectingSink()
    {
        connect(&_sink, &QVideoSink::videoFrameChanged, this, &CollectingSink::Present);
    }

    QVideoSink *Sink()
    {
        return &_sink;
    }

    int frameCount{0};
    int framesWithOpaqueBackgroundSamples{0};
    int representativeOpaquePixels{0};
    QImage representativeFrame;
    QImage failedInput;
    QImage failedOutput;
    qint64 failedTime{-1};

private:
    void Present(const QVideoFrame &frame)
    {
        auto image = frame.toImage();
        const auto input = image;
        if (image.isNull()) {
            return;
        }
        image = image.scaled(QSize{520, 244}, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        Gui::Widget::Detail::KnockOutAnimationBackground(image);
        ++frameCount;
        if (!HasTransparentBackgroundSamples(image)) {
            ++framesWithOpaqueBackgroundSamples;
            if (failedInput.isNull()) {
                failedInput = input;
                failedOutput = image;
                failedTime = frame.startTime();
            }
        }
        int opaquePixels = 0;
        for (int y = 0; y < image.height(); y += 4) {
            for (int x = 0; x < image.width(); x += 4) {
                opaquePixels += qAlpha(image.pixel(x, y)) != 0;
            }
        }
        if (opaquePixels > representativeOpaquePixels) {
            representativeOpaquePixels = opaquePixels;
            representativeFrame = image;
        }
        emit FrameReceived();
    }

    QVideoSink _sink;

Q_SIGNALS:
    void FrameReceived();
};

void VerifyTransparentBackgroundSamples(const QImage &image)
{
    QVERIFY(HasTransparentBackgroundSamples(image));
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
            VerifyTransparentBackgroundSamples(image);
            QCOMPARE(qAlpha(image.pixel(50, 50)), 255);
            QCOMPARE(qAlpha(image.pixel(80, 50)), 255); // enclosed matte-coloured detail survives
        }
    }

    void KeepsForegroundThatReachesAnEdgeMidpoint()
    {
        QImage image{160, 100, QImage::Format_ARGB32_Premultiplied};
        image.fill(Qt::white);
        QPainter painter{&image};
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor{"#202020"});
        painter.drawRoundedRect(QRect{100, 20, 60, 60}, 14, 14);
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRect{112, 32, 36, 36}, 8, 8);
        painter.end();

        Gui::Widget::Detail::KnockOutAnimationBackground(image);

        QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
        QCOMPARE(qAlpha(image.pixel(image.width() - 1, image.height() - 1)), 0);
        QCOMPARE(qAlpha(image.pixel(0, image.height() / 2)), 0);
        QCOMPARE(qAlpha(image.pixel(110, 50)), 255);
        QCOMPARE(qAlpha(image.pixel(140, 50)), 255);
        QCOMPARE(qAlpha(image.pixel(image.width() - 1, image.height() / 2)), 255);
    }

    void BundledAnimationsRemoveMatteBackgrounds_data()
    {
        QTest::addColumn<QString>("video");
        const QDir videoDir{QStringLiteral(APD_SOURCE_DIR "/Source/Resource/Video")};
        const auto videos = videoDir.entryList({"*.avi"}, QDir::Files, QDir::Name);
        QVERIFY2(!videos.isEmpty(), "No bundled animations found");

        for (const auto &video : videos) {
            QTest::newRow(qPrintable(video)) << video;
        }
    }

    void BundledAnimationsUseCompleteMacroblocks_data()
    {
        BundledAnimationsRemoveMatteBackgrounds_data();
    }

    void BundledAnimationsUseCompleteMacroblocks()
    {
        QFETCH(QString, video);
        QFile file{QStringLiteral(APD_SOURCE_DIR "/Source/Resource/Video/") + video};
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto header = file.read(64 * 1024);
        QCOMPARE(header.left(4), QByteArray{"RIFF"});
        QCOMPARE(header.mid(8, 4), QByteArray{"AVI "});
        const auto formatOffset = header.indexOf("strf");
        QVERIFY(formatOffset >= 0);
        QDataStream format{header.mid(formatOffset + 8)};
        format.setByteOrder(QDataStream::LittleEndian);
        quint32 headerSize;
        qint32 width, height;
        format >> headerSize >> width >> height;
        QCOMPARE(format.status(), QDataStream::Ok);
        QVERIFY(headerSize >= 40);
        QVERIFY(width > 0 && height > 0);
        // Partial MPEG-4 macroblocks can produce corrupt frames in the Windows decoder.
        QCOMPARE(width % 16, 0);
        QCOMPARE(height % 16, 0);
    }

    void BundledAnimationsRemoveMatteBackgrounds()
    {
        QFETCH(QString, video);
        const QDir videoDir{QStringLiteral(APD_SOURCE_DIR "/Source/Resource/Video")};
        CollectingSink sink;
        QMediaPlayer player;
        player.setVideoSink(sink.Sink());
        player.setSource(QUrl::fromLocalFile(videoDir.filePath(video)));
        player.setPlaybackRate(4.0);
        player.play();

        QTRY_COMPARE_WITH_TIMEOUT(player.mediaStatus(), QMediaPlayer::EndOfMedia, 15000);
        player.stop();

        QVERIFY2(sink.frameCount > 0, qPrintable(video + ": no frames decoded"));
        if (!sink.failedInput.isNull()) {
            QDir{}.mkpath(QStringLiteral(APD_BINARY_DIR "/AnimationValidation"));
            const auto prefix =
                QStringLiteral(APD_BINARY_DIR "/AnimationValidation/diagnostic-") + video;
            sink.failedInput.save(prefix + "-input.png");
            sink.failedOutput.save(prefix + "-output.png");
            qWarning() << "First failing frame:" << video << sink.failedTime << "us";
        }
        QCOMPARE(sink.framesWithOpaqueBackgroundSamples, 0);
        QVERIFY(!sink.representativeFrame.isNull());
        SaveComposites(QFileInfo{video}.completeBaseName(), sink.representativeFrame);

        player.setVideoSink(nullptr);
        player.setSource({});
        QCoreApplication::processEvents();
    }
};

QTEST_MAIN(AnimationViewTests)

#include "AnimationViewTests.moc"
