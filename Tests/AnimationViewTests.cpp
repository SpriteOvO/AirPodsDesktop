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
    int framesTouchingRightEdge{0};
    int representativeOpaquePixels{0};
    QImage representativeFrame;
    QImage fallbackFrame;
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
        for (int y = 0; y < image.height(); ++y) {
            if (qAlpha(image.pixel(image.width() - 1, y)) != 0) {
                ++framesTouchingRightEdge;
                break;
            }
        }
        if (fallbackFrame.isNull() && frame.startTime() >= 500'000) {
            fallbackFrame = image;
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

    void KeepsPinkProductsOnAGreenMatte()
    {
        // Pink and purple renders ship on a green matte: on magenta, their whole surface reads as
        // matte spill and is bleached. The spill test must follow the matte the frame carries.
        const QColor pink{"#F2B8D8"};
        const QColor green{"#00FF00"};
        QImage image{160, 100, QImage::Format_ARGB32_Premultiplied};
        image.fill(green);
        QPainter painter{&image};
        painter.setPen(Qt::NoPen);
        painter.setBrush(pink);
        painter.drawRoundedRect(QRect{40, 20, 80, 60}, 14, 14);
        // A half-covered rim pixel, as the encoder leaves it: product mixed with green.
        painter.setBrush(QColor{
            (pink.red() + green.red()) / 2, (pink.green() + green.green()) / 2,
            (pink.blue() + green.blue()) / 2});
        painter.drawRect(QRect{38, 45, 2, 10});
        painter.end();

        Gui::Widget::Detail::KnockOutAnimationBackground(image);
        VerifyTransparentBackgroundSamples(image);
        const auto centre = qUnpremultiply(image.pixel(80, 50));
        QCOMPARE(qAlpha(centre), 255);
        QVERIFY(std::abs(qRed(centre) - pink.red()) <= 2);
        QVERIFY(std::abs(qGreen(centre) - pink.green()) <= 2);
        QVERIFY(std::abs(qBlue(centre) - pink.blue()) <= 2);
        // The green-tinted rim loses its spill rather than staying green.
        const auto rim = qUnpremultiply(image.pixel(39, 50));
        QVERIFY(qAlpha(rim) == 0 || qGreen(rim) <= (std::max)(qRed(rim), qBlue(rim)) + 2);
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
        // White-only models sit at the top level; color renders in a folder per model.
        auto videos = videoDir.entryList({"*.avi"}, QDir::Files, QDir::Name);
        for (const auto &model : videoDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            for (const auto &video :
                 QDir{videoDir.filePath(model)}.entryList({"*.avi"}, QDir::Files, QDir::Name))
            {
                videos.append(model + "/" + video);
            }
        }
        QVERIFY2(!videos.isEmpty(), "No bundled animations found");

        for (const auto &video : videos) {
            QTest::newRow(qPrintable(video)) << video;
        }
    }

    void EarbudAnimationsKeepTheirDarkParts_data()
    {
        // Earbuds and their cases never show the background through a solid opening: matte
        // enclosed by the product there means a dark part (a sensor, a mesh) was keyed away
        // during conversion, as once happened to the AirPods Pro 2 sensors. Headsets and
        // neckbands do have such openings, so only earbud renders are checked.
        QTest::addColumn<QString>("video");
        const QDir videoDir{QStringLiteral(APD_SOURCE_DIR "/Source/Resource/Video")};
        for (const auto &video : videoDir.entryList({"*.avi"}, QDir::Files, QDir::Name)) {
            QTest::newRow(qPrintable(video)) << video;
        }
        for (const auto &video :
             QDir{videoDir.filePath("Beats_Fit_Pro")}.entryList({"*.avi"}, QDir::Files, QDir::Name))
        {
            QTest::newRow(qPrintable("Beats_Fit_Pro/" + video)) << "Beats_Fit_Pro/" + video;
        }
    }

    void EarbudAnimationsKeepTheirDarkParts()
    {
        QFETCH(QString, video);
        int framesChecked = 0, framesWithOpening = 0;
        QVideoSink sink;
        connect(&sink, &QVideoSink::videoFrameChanged, this, [&](const QVideoFrame &frame) {
            const auto image = frame.toImage().convertToFormat(QImage::Format_RGB32);
            if (image.isNull()) {
                return;
            }
            ++framesChecked;
            const int w = image.width(), h = image.height();
            const QRgb matte = image.pixel(0, 0);
            const auto isMatte = [&](int x, int y) {
                const QRgb p = image.pixel(x, y);
                return (std::max)(
                           {std::abs(qRed(p) - qRed(matte)), std::abs(qGreen(p) - qGreen(matte)),
                            std::abs(qBlue(p) - qBlue(matte))}) <= 24;
            };
            // 0: product, 1: matte reachable from the border, 2: matte enclosed by product.
            std::vector<char> kind(static_cast<size_t>(w) * h, 0);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    kind[y * w + x] = isMatte(x, y) ? 2 : 0;
                }
            }
            std::vector<std::pair<int, int>> stack;
            for (int x = 0; x < w; ++x) {
                stack.emplace_back(x, 0);
                stack.emplace_back(x, h - 1);
            }
            for (int y = 0; y < h; ++y) {
                stack.emplace_back(0, y);
                stack.emplace_back(w - 1, y);
            }
            while (!stack.empty()) {
                const auto [x, y] = stack.back();
                stack.pop_back();
                if (x < 0 || y < 0 || x >= w || y >= h || kind[y * w + x] != 2) {
                    continue;
                }
                kind[y * w + x] = 1;
                stack.emplace_back(x + 1, y);
                stack.emplace_back(x - 1, y);
                stack.emplace_back(x, y + 1);
                stack.emplace_back(x, y - 1);
            }
            // Thin slivers between overlapping stems are real; a solid opening survives
            // shrinking by two pixels.
            for (int y = 2; y < h - 2; ++y) {
                for (int x = 2; x < w - 2; ++x) {
                    bool solid = true;
                    for (int dy = -2; dy <= 2 && solid; ++dy) {
                        for (int dx = -2; dx <= 2; ++dx) {
                            if (std::abs(dx) + std::abs(dy) <= 2 &&
                                kind[(y + dy) * w + x + dx] != 2)
                            {
                                solid = false;
                                break;
                            }
                        }
                    }
                    if (solid) {
                        ++framesWithOpening;
                        return;
                    }
                }
            }
        });
        QMediaPlayer player;
        player.setVideoSink(&sink);
        player.setSource(QUrl::fromLocalFile(
            QDir{QStringLiteral(APD_SOURCE_DIR "/Source/Resource/Video")}.filePath(video)));
        player.setPlaybackRate(4.0);
        player.play();
        QTRY_COMPARE_WITH_TIMEOUT(player.mediaStatus(), QMediaPlayer::EndOfMedia, 15000);
        player.stop();
        player.setVideoSink(nullptr);
        QVERIFY2(framesChecked > 0, qPrintable(video + ": no frames decoded"));
        QVERIFY2(
            framesWithOpening == 0,
            qPrintable(
                QString{"%1: %2 frames show the background through the product"}.arg(video).arg(
                    framesWithOpening)));
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
            const auto prefix = QStringLiteral(APD_BINARY_DIR "/AnimationValidation/diagnostic-") +
                                QString{video}.replace('/', '-');
            sink.failedInput.save(prefix + "-input.png");
            sink.failedOutput.save(prefix + "-output.png");
            qWarning() << "First failing frame:" << video << sink.failedTime << "us";
        }
        QCOMPARE(sink.framesWithOpaqueBackgroundSamples, 0);
        QVERIFY(!sink.representativeFrame.isNull());
        const auto stem = QString{video}.replace('/', '-').chopped(4);
        SaveComposites(stem, sink.representativeFrame);
        QVERIFY(!sink.fallbackFrame.isNull());
        const auto fallbackPath = QStringLiteral(APD_BINARY_DIR "/AnimationValidation/") + stem +
                                  QStringLiteral("-fallback.png");
        QVERIFY2(sink.fallbackFrame.save(fallbackPath), "fallback save failed");
        if (video == QStringLiteral("AirPods_Pro.avi")) {
            QCOMPARE(sink.framesTouchingRightEdge, 0);
        }

        player.setVideoSink(nullptr);
        player.setSource({});
        QCoreApplication::processEvents();
    }
};

QTEST_MAIN(AnimationViewTests)

#include "AnimationViewTests.moc"
