#include <algorithm>
#include <atomic>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QListWidget>
#include <QRawFont>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QTranslator>
#include <QPlainTextEdit>
#include <QProcess>
#include <QAbstractButton>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyleOptionComboBox>
#include <QThread>
#include <QUrl>
#include <QTimer>
#include <QPainter>

#include <magic_enum.hpp>

#include "Source/Core/QuickConnect.h"
#include "Source/Core/SettingsRepository.h"
#include "Source/Gui/MainWindow.h"
#include "Source/Gui/Widget/ActivityIndicator.h"
#include "Source/Gui/Widget/Battery.h"
#include "Source/Gui/Widget/AnimationView.h"
#include "Source/Gui/MainWindowPresentation.h"
#include "Source/Gui/Widget/FadeOverlay.h"
#include "Source/Gui/SettingsWindow.h"
#include "Source/Gui/Theme.h"
#include "Source/Gui/TrayIcon.h"
#include "Source/Gui/UpdateWindow.h"

class UpdateUrlRecorder : public QObject
{
    Q_OBJECT
public:
    QList<QUrl> urls;
    UpdateUrlRecorder()
    {
        QDesktopServices::setUrlHandler("https", this, "Open");
    }
    ~UpdateUrlRecorder() override
    {
        QDesktopServices::unsetUrlHandler("https");
    }
public Q_SLOTS:
    void Open(const QUrl &url)
    {
        urls.push_back(url);
    }
};

namespace {
void VerifySmoothPopupCorners(QWidget *popup, const QString &fileName)
{
    const auto outputDir = QStringLiteral(APD_BINARY_DIR "/UiValidation/");
    QVERIFY(QDir{}.mkpath(outputDir));
    const auto image = popup->grab().toImage();
    QVERIFY(image.save(outputDir + fileName));
    QVERIFY(image.hasAlphaChannel());
    const int cornerSize = qRound(10 * image.devicePixelRatio());
    // Every corner must have a transparent exterior and intermediate alpha along the curve.
    // An opaque native background or a binary region mask fails these pixel checks.
    for (const bool right : {false, true}) {
        for (const bool bottom : {false, true}) {
            const auto pixelAt = [&](int x, int y) {
                return image.pixel(
                    right ? image.width() - 1 - x : x, bottom ? image.height() - 1 - y : y);
            };
            QCOMPARE(qAlpha(pixelAt(0, 0)), 0);
            QCOMPARE(qAlpha(pixelAt(cornerSize, cornerSize)), 255);
            int blendedPixels = 0;
            for (int y = 0; y < cornerSize; ++y) {
                for (int x = 0; x < cornerSize; ++x) {
                    const int alpha = qAlpha(pixelAt(x, y));
                    blendedPixels += alpha > 0 && alpha < 255;
                }
            }
            QVERIFY(blendedPixels > 0);
        }
    }
}

Core::Update::ReleaseInfo UpdateFixture()
{
    Core::Update::ReleaseInfo info;
    info.version = QVersionNumber{2, 12, 345};
    info.url = "https://example.invalid/release";
    info.isPreRelease = true;
    info.fileName = "simulated.exe";
    info.downloadUrl = "https://example.invalid/simulated.exe";
    info.fileSize = 4000000000u;
    info.sha256 = QString(64, QChar{'0'});
    info.changeLog =
        "Improved device connections.\nFixed battery status updates.\n<b>Plain text, not HTML</b>";
    return info;
}

struct SimulatedDownload {
    std::atomic<int> stage{0};
    std::atomic<int> calls{0};

    bool Run(const Core::Update::ReleaseInfo &, const Core::Update::FnProgress &progress)
    {
        ++calls;
        for (;;) {
            const auto current = stage.load();
            if (current >= 4) {
                return current == 4;
            }
            const size_t total = current == 0 ? 0 : 4000000000u;
            const size_t downloaded = current == 0 ? 0 : current == 1 ? 3000000000u : total;
            if (!progress(downloaded, total)) {
                return false;
            }
            QThread::msleep(10);
        }
    }

    Gui::UpdateWindow::DownloadFunction Function()
    {
        return [this](const auto &info, const auto &progress) { return Run(info, progress); };
    }
};
} // namespace

class UiRenderingTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void TrayQuitExitsWithBatteryWindowVisible()
    {
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(QCoreApplication::applicationFilePath(), {"--tray-quit-probe"});
        QVERIFY(child.waitForStarted());
        if (!child.waitForFinished(5000)) {
            child.kill();
            child.waitForFinished();
            QFAIL("Tray Quit left the probe process running");
        }
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
    }

    void AudioAutoConnectIsOptionalAndIndependent()
    {
        Core::QuickConnect::Controller quickConnect{
            std::make_shared<Core::QuickConnect::NullBackend>()};
        Gui::SettingsWindow settings{[] { return 0; }, quickConnect};
        auto *automatic = settings.findChild<QCheckBox *>("cbAudioAutoConnectEnabled");
        QVERIFY2(automatic != nullptr, "Settings must expose optional audio playback auto-connect");
        QVERIFY(!automatic->isChecked());
        auto *tray = settings.findChild<QCheckBox *>("cbTrayQuickConnectEnabled");
        QVERIFY(tray != nullptr);
        automatic->setChecked(true);
        QVERIFY(!tray->isChecked());
        QVERIFY(Core::Settings::GetCurrent().audio_auto_connect_enabled);
        automatic->setChecked(false);
    }

    void ConnectionSuccessOpensBatteryWindowAndDismissesNotification()
    {
        Core::QuickConnect::Controller quickConnect{
            std::make_shared<Core::QuickConnect::NullBackend>()};
        Gui::TrayIcon tray{[] { return 0; }, quickConnect};
        Gui::MainWindow battery;
        connect(&tray, &Gui::TrayIcon::ShowMainWindowRequested, &battery, &Gui::MainWindow::Show);
        QSignalSpy opened{&tray, &Gui::TrayIcon::ShowMainWindowRequested};
        quickConnect.OutcomeChanged(Core::QuickConnect::Outcome::RequestStarted, "AirPods");
        auto *card = tray.findChild<QWidget *>("connectionNotification");
        QVERIFY(card != nullptr);
        QVERIFY(card->isVisible());
        quickConnect.OutcomeChanged(Core::QuickConnect::Outcome::Connected, "AirPods");
        QCOMPARE(opened.count(), 1);
        QVERIFY(!card->isVisible());
        QVERIFY(battery.isVisible());
    }

    void AlreadyConnectedOpensBatteryWindowWithoutNotification()
    {
        Core::QuickConnect::Controller quickConnect{
            std::make_shared<Core::QuickConnect::NullBackend>()};
        Gui::TrayIcon tray{[] { return 0; }, quickConnect};
        Gui::MainWindow battery;
        connect(&tray, &Gui::TrayIcon::ShowMainWindowRequested, &battery, &Gui::MainWindow::Show);
        QSignalSpy opened{&tray, &Gui::TrayIcon::ShowMainWindowRequested};
        quickConnect.OutcomeChanged(Core::QuickConnect::Outcome::AlreadyConnected, "AirPods");
        QCOMPARE(opened.count(), 1);
        auto *card = tray.findChild<QWidget *>("connectionNotification");
        QVERIFY(card != nullptr);
        QVERIFY(!card->isVisible());
        QVERIFY(battery.isVisible());
    }

    void TrayClicksActImmediatelyWithoutDoubleClickOpening()
    {
        Core::QuickConnect::Controller quickConnect{
            std::make_shared<Core::QuickConnect::NullBackend>()};
        quickConnect.SetEnabled(true);
        Gui::TrayIcon tray{[] { return 0; }, quickConnect};
        QSignalSpy opened{&tray, &Gui::TrayIcon::ShowMainWindowRequested};
        auto *icon = tray.findChild<QSystemTrayIcon *>();
        QVERIFY(icon != nullptr);
        icon->activated(QSystemTrayIcon::Trigger);
        auto *card = tray.findChild<QWidget *>("connectionNotification");
        QVERIFY(card != nullptr);
        QVERIFY(card->isVisible());
        QCOMPARE(opened.count(), 0);
        icon->activated(QSystemTrayIcon::DoubleClick);
        QCOMPARE(opened.count(), 0);
        quickConnect.SetEnabled(false);
        icon->activated(QSystemTrayIcon::Trigger);
        QCOMPARE(opened.count(), 1);
    }

    void SettingsDescribeConnectionAndBatterySingleClickBehavior()
    {
        QVERIFY(Gui::Theme::LoadBundledFonts());
        Gui::Theme::ApplyApplicationTypography(QLocale{"zh_CN"});
        QTranslator translator;
        QVERIFY(translator.load(
            QLocale{"zh_CN"}, "apd", "_",
            QCoreApplication::applicationDirPath() + "/translations"));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        Core::QuickConnect::Controller quickConnect{
            std::make_shared<Core::QuickConnect::NullBackend>()};
        Gui::SettingsWindow settings{[] { return 0; }, quickConnect};
        auto *description = settings.findChild<QLabel *>("lbDescTrayQuickConnect");
        QVERIFY(description != nullptr);
        auto *audioTitle = settings.findChild<QLabel *>("lbAudioAutoConnect");
        QVERIFY(audioTitle != nullptr);
        QCOMPARE(audioTitle->text(), QStringLiteral("音频开始播放时自动连接"));
        auto *audioDescription = settings.findChild<QLabel *>("lbDescAudioAutoConnect");
        QVERIFY(audioDescription != nullptr);
        QVERIFY(audioDescription->text().contains(QStringLiteral("至少等待 30 秒")));
        QCOMPARE(
            description->text(),
            QStringLiteral("单击托盘图标：未连接时连接选定的设备，已连接时显示电量信息。"
                           "连接成功后自动显示电量信息。"));
        settings.show();
        QVERIFY(QTest::qWaitForWindowExposed(&settings));
        auto *card = settings.findChild<QWidget *>("cardTrayQuickConnect");
        QVERIFY(card != nullptr);
        auto *scroll = settings.findChild<QScrollArea *>("pagesScrollArea");
        QVERIFY(scroll != nullptr);
        scroll->ensureWidgetVisible(card);
        QVERIFY(QDir{}.mkpath(QStringLiteral(APD_BINARY_DIR "/UiValidation")));
        QVERIFY(
            card->grab().save(QStringLiteral(APD_BINARY_DIR "/UiValidation/tray-setting-zh.png")));
        QCoreApplication::removeTranslator(&translator);
        Gui::Theme::ApplyApplicationTypography(QLocale{"en_US"});
    }

    void NotificationUsesCurrentLanguage()
    {
        QVERIFY(Gui::Theme::LoadBundledFonts());
        Gui::Theme::ApplyApplicationTypography(QLocale{"zh_CN"});
        QTranslator translator;
        QVERIFY(translator.load(
            QLocale{"zh_CN"}, "apd", "_",
            QCoreApplication::applicationDirPath() + "/translations"));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();
        theme.SetMode(Gui::Theme::Mode::Light);
        Core::QuickConnect::Controller quickConnect{
            std::make_shared<Core::QuickConnect::NullBackend>()};
        Gui::TrayIcon tray{[] { return 0; }, quickConnect};
        quickConnect.OutcomeChanged(Core::QuickConnect::Outcome::RequestStarted, "AirPods Pro");
        auto *card = tray.findChild<QWidget *>("connectionNotification");
        QVERIFY(card != nullptr);
        QCOMPARE(
            card->findChild<QLabel *>("notificationTitle")->text(),
            translator.translate("Gui::TrayIcon", "Quick connect"));
        QCOMPARE(
            card->findChild<QLabel *>("notificationMessage")->text(),
            translator.translate("Gui::TrayIcon", "Connecting to %1...").arg("AirPods Pro"));
        VerifySmoothPopupCorners(card, "connection-notification-zh-light.png");
        theme.SetMode(Gui::Theme::Mode::Dark);
        VerifySmoothPopupCorners(card, "connection-notification-zh-dark.png");
        quickConnect.OutcomeChanged(Core::QuickConnect::Outcome::Connected, "AirPods Pro");
        QVERIFY(!card->isVisible());
        QCoreApplication::removeTranslator(&translator);
        Gui::Theme::ApplyApplicationTypography(QLocale{"en_US"});
        theme.SetMode(originalMode);
    }

    void TrayNotificationsUseOneApplicationCard()
    {
        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();
        theme.SetMode(Gui::Theme::Mode::Light);
        Core::QuickConnect::Controller quickConnect{
            std::make_shared<Core::QuickConnect::NullBackend>()};
        Gui::TrayIcon tray{[] { return 0; }, quickConnect};
        tray.ShowMessage("Quick connect", "Connecting...");
        auto *card = tray.findChild<QWidget *>("connectionNotification");
        QVERIFY(card != nullptr);
        QVERIFY(card->isVisible());
        QVERIFY(card->testAttribute(Qt::WA_ShowWithoutActivating));
        tray.ShowMessage("Quick connect", "Connected.");
        QCOMPARE(tray.findChild<QWidget *>("connectionNotification"), card);
        auto *message = card->findChild<QLabel *>("notificationMessage");
        QVERIFY(message != nullptr);
        QCOMPARE(message->text(), QStringLiteral("Connected."));
        VerifySmoothPopupCorners(card, "connection-notification-light.png");
        auto *close = card->findChild<QAbstractButton *>("notificationClose");
        QVERIFY(close != nullptr);
        QTest::mouseClick(close, Qt::LeftButton);
        QVERIFY(!card->isVisible());
        theme.SetMode(originalMode);
    }

    void NotificationCardSharesTheTitleRowWithClose()
    {
        Core::QuickConnect::Controller quickConnect{
            std::make_shared<Core::QuickConnect::NullBackend>()};
        Gui::TrayIcon tray{[] { return 0; }, quickConnect};
        tray.ShowMessage("Quick connect", "Could not connect.", QSystemTrayIcon::Warning);
        auto *card = tray.findChild<QWidget *>("connectionNotification");
        QVERIFY(card != nullptr);
        QTRY_VERIFY(card->isVisible());
        auto *title = card->findChild<QLabel *>("notificationTitle");
        auto *message = card->findChild<QLabel *>("notificationMessage");
        auto *close = card->findChild<QAbstractButton *>("notificationClose");
        QVERIFY(title != nullptr && message != nullptr && close != nullptr);

        // The title is drawn on its first line, centred on the card, level with the close button.
        const auto lineRect = [&] {
            const auto bounds = title->fontMetrics().boundingRect(
                title->contentsRect(), title->alignment() | Qt::TextWordWrap, title->text());
            const auto line = title->fontMetrics()
                                  .boundingRect(
                                      QRect{0, 0, QWIDGETSIZE_MAX, QWIDGETSIZE_MAX},
                                      Qt::TextSingleLine, title->text())
                                  .height();
            return QRect{title->mapTo(card, bounds.topLeft()), QSize{bounds.width(), line}};
        }();
        const QRect closeRect{close->mapTo(card, QPoint{}), close->size()};
        QVERIFY2(
            qAbs(lineRect.center().y() - closeRect.center().y()) <= 2,
            qPrintable(QString{"title line centre %1, close centre %2"}
                           .arg(lineRect.center().y())
                           .arg(closeRect.center().y())));
        QVERIFY2(
            qAbs(lineRect.center().x() - card->width() / 2) <= 2,
            qPrintable(QString{"title %1..%2, label %3+%4, card width %5"}
                           .arg(lineRect.left())
                           .arg(lineRect.right())
                           .arg(title->mapTo(card, QPoint{}).x())
                           .arg(title->width())
                           .arg(card->width())));
        QVERIFY(!lineRect.intersects(closeRect));
        QVERIFY(closeRect.right() < card->width() - 12);

        // The message follows the title closely, whatever its script's line height.
        const int messageTop = message->mapTo(card, QPoint{}).y();
        QVERIFY2(
            messageTop - lineRect.bottom() <= 10,
            qPrintable(QString{"title ends at %1, message starts at %2"}
                           .arg(lineRect.bottom())
                           .arg(messageTop)));

        // The card ends one margin below the message.
        const int messageBottom = message->mapTo(card, QPoint{0, message->height()}).y();
        QVERIFY2(
            card->height() - messageBottom <= 24,
            qPrintable(QString{"%1 below the message"}.arg(card->height() - messageBottom)));
        auto *activity = card->findChild<Gui::Widget::ActivityIndicator *>("notificationActivity");
        QVERIFY(activity != nullptr);
        QVERIFY(!activity->isVisible());

        const auto scale = qEnvironmentVariable("QT_SCALE_FACTOR", "system");
        VerifySmoothPopupCorners(card, QString{"connection-notification-scale%1.png"}.arg(scale));

        // While connecting, an iOS-style spinner leads the message on its first line, the pair
        // centred on the card, without making the card taller.
        const int settledHeight = card->height();
        quickConnect.OutcomeChanged(Core::QuickConnect::Outcome::RequestStarted, "AirPods Pro");
        QTRY_VERIFY(activity->isVisible());
        const auto messageText = message->fontMetrics().boundingRect(
            message->contentsRect(), message->alignment() | Qt::TextWordWrap, message->text());
        const QRect textRect{message->mapTo(card, messageText.topLeft()), messageText.size()};
        const QRect activityRect{activity->mapTo(card, QPoint{}), activity->size()};
        const int firstLine = message->fontMetrics()
                                  .boundingRect(
                                      QRect{0, 0, QWIDGETSIZE_MAX, QWIDGETSIZE_MAX},
                                      Qt::TextSingleLine, message->text())
                                  .height();
        QVERIFY2(
            activityRect.right() < textRect.left() && textRect.left() - activityRect.right() <= 8,
            qPrintable(QString{"spinner ends at %1, text starts at %2"}
                           .arg(activityRect.right())
                           .arg(textRect.left())));
        QVERIFY2(
            qAbs(activityRect.center().y() - (textRect.top() + firstLine / 2)) <= 2,
            qPrintable(QString{"spinner centre %1, first line centre %2"}
                           .arg(activityRect.center().y())
                           .arg(textRect.top() + firstLine / 2)));
        QVERIFY2(
            qAbs((activityRect.left() + textRect.right()) / 2 - card->width() / 2) <= 2,
            qPrintable(QString{"spinner and text span %1..%2 on a card %3 wide"}
                           .arg(activityRect.left())
                           .arg(textRect.right())
                           .arg(card->width())));
        QVERIFY2(
            card->height() <= settledHeight + 2,
            qPrintable(QString{"%1 tall while connecting, %2 otherwise"}
                           .arg(card->height())
                           .arg(settledHeight)));
        const int head = activity->HeadSpoke();
        QTRY_VERIFY_WITH_TIMEOUT(activity->HeadSpoke() != head, 1000);
        VerifySmoothPopupCorners(
            card, QString{"connection-notification-pending-scale%1.png"}.arg(scale));
    }

    void QuickConnectCardTracksProgressAndResult()
    {
        Core::QuickConnect::Controller quickConnect{
            std::make_shared<Core::QuickConnect::NullBackend>()};
        Gui::TrayIcon tray{[] { return 0; }, quickConnect};
        QSignalSpy opened{&tray, &Gui::TrayIcon::ShowMainWindowRequested};
        quickConnect.OutcomeChanged(Core::QuickConnect::Outcome::RequestStarted, "AirPods");
        auto *card = tray.findChild<QWidget *>("connectionNotification");
        QVERIFY(card != nullptr);
        QVERIFY(card->isVisible());
        auto *activity = card->findChild<Gui::Widget::ActivityIndicator *>("notificationActivity");
        QVERIFY(activity != nullptr);
        QVERIFY(activity->isVisible());
        // The old success timer must not dismiss a newly started connection.
        quickConnect.OutcomeChanged(Core::QuickConnect::Outcome::Connected, "AirPods");
        quickConnect.OutcomeChanged(Core::QuickConnect::Outcome::RequestStarted, "AirPods");
        QTest::qWait(3700);
        QVERIFY(card->isVisible());
        quickConnect.OutcomeChanged(Core::QuickConnect::Outcome::Connected, "AirPods");
        QVERIFY(!card->isVisible());
        QCOMPARE(opened.count(), 2);
    }

    void NotificationAvoidsBatteryCardAndSupportsDarkTheme()
    {
        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();
        Core::QuickConnect::Controller quickConnect{
            std::make_shared<Core::QuickConnect::NullBackend>()};
        Gui::TrayIcon tray{[] { return 0; }, quickConnect};
        Gui::MainWindow battery;
        tray.ShowMessage("Quick connect", "Connected.");
        auto *card = tray.findChild<QWidget *>("connectionNotification");
        QVERIFY(card != nullptr);
        battery.Show();
        QTest::qWait(700);
        QVERIFY(!card->frameGeometry().intersects(battery.frameGeometry()));
        theme.SetMode(Gui::Theme::Mode::Dark);
        tray.ShowMessage("Quick connect", "Could not connect.", QSystemTrayIcon::Warning);
        VerifySmoothPopupCorners(card, "connection-notification-dark.png");
        QTest::qWait(3700);
        QVERIFY(card->isVisible());
        theme.SetMode(originalMode);
    }

    void MainWindowRendersAirPods5()
    {
        Core::AirPods::State state;
        state.model = Core::AirPods::Model::AirPods_5;
        state.displayName = QStringLiteral("AirPods 5");
        state.pods.left.battery = 80;
        state.pods.right.battery = 70;
        state.caseBox.battery = 50;

        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();
        const auto outputDir = QStringLiteral(APD_BINARY_DIR "/UiValidation");
        QVERIFY(QDir{}.mkpath(outputDir));
        const auto scale = qEnvironmentVariable("QT_SCALE_FACTOR", "system");

        for (const auto mode : {Gui::Theme::Mode::Light, Gui::Theme::Mode::Dark}) {
            theme.SetMode(mode);
            Gui::MainWindow window;
            window.UpdateState(state);
            window.Show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));

            auto *label = window.findChild<QLabel *>("deviceLabel");
            auto *animation = window.findChild<Gui::Widget::AnimationView *>();
            QVERIFY(label != nullptr);
            QVERIFY(animation != nullptr);
            QCOMPARE(label->text(), state.displayName);
            QTRY_VERIFY(animation->isVisible());
            QCOMPARE(animation->width(), animation->height() * 2);

            const auto animationImage = animation->grab().toImage();
            int visiblePixels = 0;
            for (int y = 0; y < animationImage.height(); y += 2) {
                for (int x = 0; x < animationImage.width(); x += 2) {
                    visiblePixels += qAlpha(animationImage.pixel(x, y)) != 0;
                }
            }
            QVERIFY(visiblePixels > 100);

            const auto suffix =
                QString{"airpods-5-%1-scale%2.png"}.arg(theme.IsDark() ? "dark" : "light", scale);
            QVERIFY(window.grab().save(outputDir + '/' + suffix));
            window.close();
        }
        theme.SetMode(originalMode);
    }

    void MainWindowLaysOutHeaderLikeIOS()
    {
        // Proportions measured from the iOS sheet, scaled to the popup's 320 px width: a 24 px
        // close button centred 35 px from the top and right edges, and a centred title whose
        // first line sits 42 px from the top; a name too long for one line wraps onto a second.
        Gui::MainWindow window;
        auto *label = window.findChild<QLabel *>("deviceLabel");
        auto *close = window.findChild<QWidget *>("closeContainer");
        auto *animation = window.findChild<QWidget *>("animationContainer");
        QVERIFY(label != nullptr && close != nullptr && animation != nullptr);
        window.Show();
        QTRY_VERIFY(window.isVisible());

        const auto scale = qEnvironmentVariable("QT_SCALE_FACTOR", "system");
        for (const auto &name :
             {QStringLiteral("AirPods 5"), QStringLiteral("湊あくあ 的 AirPods"),
              QStringLiteral("Kashionz's AirPods Pro")})
        {
            Core::AirPods::State state;
            state.model = Core::AirPods::Model::AirPods_5;
            state.displayName = name;
            window.UpdateState(state);

            // Measured like the popup does: CJK names use a taller fallback font.
            const int lineHeight =
                label->fontMetrics()
                    .boundingRect(
                        QRect{0, 0, QWIDGETSIZE_MAX, QWIDGETSIZE_MAX}, Qt::TextSingleLine, name)
                    .height();
            const qreal firstLineCentre = label->y() + lineHeight / 2.0;
            QVERIFY2(
                std::abs(firstLineCentre - 42) <= 1,
                qPrintable(QString{"%1: first line at %2"}.arg(name).arg(firstLineCentre)));
            const QRectF closeRect = close->geometry();
            QCOMPARE(closeRect.size(), QSizeF(24, 24));
            QCOMPARE(closeRect.center(), QPointF(window.width() - 35, 35));
            // Centred on the window and clear of the close button.
            QVERIFY(std::abs(label->geometry().center().x() + 0.5 - window.width() / 2.0) <= 1);
            QVERIFY(label->geometry().right() < close->geometry().left());
            // Room between the title and the device, as on iOS, even for a two-line title.
            QVERIFY2(
                animation->geometry().top() - label->geometry().bottom() >= 20,
                qPrintable(QString{"%1: title ends at %2, picture starts at %3"}
                               .arg(name)
                               .arg(label->geometry().bottom())
                               .arg(animation->geometry().top())));
            // The whole name is shown, on one or two lines.
            QCOMPARE(label->text(), name);
            QVERIFY2(label->height() < lineHeight * 5 / 2, qPrintable(name));
            QVERIFY2(label->font().pointSize() == 17, qPrintable(name));
            QCOMPARE(label->font().weight(), QFont::Bold);

            VerifySmoothPopupCorners(
                &window, QString{"main-window-title-%1-scale%2.png"}.arg(name.size()).arg(scale));
        }

        // The name from the iOS screenshot wraps exactly as it does there.
        Core::AirPods::State state;
        state.displayName = QStringLiteral("湊あくあ 的 AirPods");
        window.UpdateState(state);
        QVERIFY(label->height() > label->fontMetrics().height() * 3 / 2);

        // The popup is created at startup and usually gets its state before it is first shown;
        // the style sheet polish on that first show must not undo the title's font, and neither
        // may a theme change afterwards.
        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();
        Gui::MainWindow fresh;
        auto *freshLabel = fresh.findChild<QLabel *>("deviceLabel");
        fresh.UpdateState(state);
        // Windows switches theme while the popup has not been shown yet.
        theme.SetMode(theme.IsDark() ? Gui::Theme::Mode::Light : Gui::Theme::Mode::Dark);
        QCoreApplication::processEvents();
        fresh.Show();
        QTRY_VERIFY(fresh.isVisible());
        QCOMPARE(freshLabel->font().pointSize(), 17);
        QCOMPARE(freshLabel->font().weight(), QFont::Bold);
        QCOMPARE(freshLabel->geometry(), label->geometry());
        theme.SetMode(theme.IsDark() ? Gui::Theme::Mode::Light : Gui::Theme::Mode::Dark);
        QCoreApplication::processEvents();
        QCOMPARE(freshLabel->font().pointSize(), 17);
        QCOMPARE(freshLabel->geometry(), label->geometry());
        theme.SetMode(originalMode);
    }

    void MainWindowElidesLongDeviceNames()
    {
        Gui::MainWindow window;
        auto *label = window.findChild<QLabel *>("deviceLabel");
        QVERIFY(label != nullptr);
        QCOMPARE(label->textFormat(), Qt::PlainText);

        Core::AirPods::State state;
        state.model = Core::AirPods::Model::AirPods_4;
        state.displayName = QStringLiteral("AirPods 4");
        window.UpdateState(state);
        QCOMPARE(label->text(), state.displayName);
        QCOMPARE(label->font().pointSize(), 17);
        QCOMPARE(label->toolTip(), QString{});
        QCOMPARE(label->accessibleName(), state.displayName);

        state.displayName =
            QStringLiteral("結城さくな專用的超級無敵特別限定版 AirPods Pro 第二世代");
        window.UpdateState(state);
        QCOMPARE(label->font().pointSize(), 12);
        QVERIFY(label->text() != state.displayName);
        QVERIFY(label->text().contains(QChar{0x2026}));
        QVERIFY(!label->text().startsWith(QChar{0x2026}));
        QVERIFY(!label->text().endsWith(QChar{0x2026}));
        QVERIFY(
            label->fontMetrics().horizontalAdvance(label->text()) <= label->contentsRect().width());
        QCOMPARE(label->toolTip(), state.displayName);
        QCOMPARE(label->accessibleName(), state.displayName);

        const auto scale = qEnvironmentVariable("QT_SCALE_FACTOR", "system");
        VerifySmoothPopupCorners(
            &window, QString{"main-window-long-device-name-scale%1.png"}.arg(scale));
    }

    void MainWindowStaysWhileLidIsOpen()
    {
        Gui::MainWindow window;
        auto *autoHide = window.findChild<QTimer *>("autoHideTimer");
        auto *lidSafety = window.findChild<QTimer *>("lidSafetyTimer");
        QVERIFY(autoHide != nullptr);
        QVERIFY(lidSafety != nullptr);

        Core::AirPods::State state;
        state.model = Core::AirPods::Model::AirPods_Pro_2;
        state.displayName = QStringLiteral("AirPods Pro");
        state.pods.left.battery = Core::AirPods::Battery{90};
        state.pods.right.battery = Core::AirPods::Battery{90};
        state.caseBox.battery = Core::AirPods::Battery{80};
        state.caseBox.isBothPodsInCase = true;
        state.caseBox.isLidOpened = true;
        window.UpdateState(state);
        window.Show();
        QTRY_VERIFY(window.isVisible());

        // Lid open: no 10 s auto-hide, only the safety cap.
        QVERIFY(!autoHide->isActive());
        QVERIFY(lidSafety->isActive());

        // Pods taken out (one, then both): the case is still open, the popup stays.
        state.pods.right.isInEar = true;
        state.caseBox.isBothPodsInCase = false;
        state.caseBox.isLidOpened = false;
        window.UpdateState(state);
        QVERIFY(!autoHide->isActive());
        QVERIFY(lidSafety->isActive());
        state.pods.left.isInEar = true;
        window.UpdateState(state);
        QVERIFY(!autoHide->isActive());

        // Pods back in and the lid closed: back to the usual auto-hide.
        state.pods.left.isInEar = state.pods.right.isInEar = false;
        state.caseBox.isBothPodsInCase = true;
        state.caseBox.isLidOpened = false;
        window.UpdateState(state);
        QVERIFY(autoHide->isActive());
        QVERIFY(!lidSafety->isActive());

        // Disconnect while the lid was open also releases the popup.
        state.caseBox.isLidOpened = true;
        window.UpdateState(state);
        QVERIFY(!autoHide->isActive());
        window.Disconnect();
        QVERIFY(autoHide->isActive());
        QVERIFY(!lidSafety->isActive());

        // While the user has the popup activated, a lid change starts no timer.
        emit qApp->applicationStateChanged(Qt::ApplicationActive);
        QVERIFY(!autoHide->isActive());
        state.caseBox.isLidOpened = true;
        window.UpdateState(state);
        QVERIFY(!autoHide->isActive());
        QVERIFY(!lidSafety->isActive());
        state.caseBox.isLidOpened = false;
        window.UpdateState(state);
        QVERIFY(!autoHide->isActive());
        QVERIFY(!lidSafety->isActive());
        emit qApp->applicationStateChanged(Qt::ApplicationInactive);
        QVERIFY(autoHide->isActive());
    }

    void MainWindowDissolvesTheBatteryRow()
    {
        Gui::MainWindow window;
        auto *overlay = window.findChild<Gui::Widget::FadeOverlay *>();
        QVERIFY(overlay != nullptr);

        Core::AirPods::State state;
        state.model = Core::AirPods::Model::AirPods_Pro_2_USB_C;
        state.displayName = QStringLiteral("AirPods Pro");
        state.pods.left.battery = Core::AirPods::Battery{90};
        state.pods.right.battery = Core::AirPods::Battery{90};
        state.pods.left.isInCase = state.pods.right.isInCase = true;
        state.caseBox.isBothPodsInCase = true;
        state.caseBox.battery = Core::AirPods::Battery{80};
        window.UpdateState(state);
        window.Show();
        QTRY_VERIFY(window.isVisible());
        QVERIFY(!overlay->IsRunning());

        // One pod out: rings split, the row dissolves from the old look.
        state.pods.right.isInCase = false;
        state.caseBox.isBothPodsInCase = false;
        window.UpdateState(state);
        QVERIFY(overlay->IsRunning());
        QVERIFY(overlay->isVisible());
        QTRY_VERIFY(!overlay->IsRunning());
        QVERIFY(overlay->isHidden());
    }

    void MainWindowKeepsTurningInEveryScene()
    {
        // Like iOS there is no still picture: the turntable keeps playing while pods come and go.
        Gui::MainWindow window;
        QVERIFY(window.findChild<QTimer *>("podsOnlyTimer") == nullptr);
        auto *animation = window.findChild<Gui::Widget::AnimationView *>();
        QVERIFY(animation != nullptr);
        QSignalSpy frames{animation, &Gui::Widget::AnimationView::FramePresented};

        Core::AirPods::State state;
        state.model = Core::AirPods::Model::AirPods_5;
        state.displayName = QStringLiteral("AirPods 5");
        state.pods.left.battery = Core::AirPods::Battery{80};
        state.pods.right.battery = Core::AirPods::Battery{70};
        state.caseBox.battery = Core::AirPods::Battery{90};
        state.pods.left.isInCase = state.pods.right.isInCase = true;
        state.caseBox.isBothPodsInCase = true;
        window.UpdateState(state);
        window.Show();
        QTRY_VERIFY(window.isVisible());

        const auto keepsTurning = [&] {
            QTRY_VERIFY(animation->isVisible());
            const int before = frames.count();
            QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= before + 10, 5000);
        };
        keepsTurning();

        state.pods.right.isInCase = false;
        state.caseBox.isBothPodsInCase = false;
        window.UpdateState(state);
        keepsTurning();

        state.pods.left.isInCase = false;
        state.pods.left.isInEar = state.pods.right.isInEar = true;
        window.UpdateState(state);
        keepsTurning();
    }

    void MainWindowTitleFollowsThemeChanges()
    {
        // Windows can switch between light and dark while the app runs (a scheduled switch, or
        // the appearance setting); the popup title must follow like the rest of the popup. The
        // popup is created at startup and may not be shown until long after such a switch.
        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();
        for (const bool shownBeforeSwitch : {true, false}) {
            for (const auto [from, to] :
                 {std::pair{Gui::Theme::Mode::Dark, Gui::Theme::Mode::Light},
                  std::pair{Gui::Theme::Mode::Light, Gui::Theme::Mode::Dark}})
            {
                theme.SetMode(from);
                Gui::MainWindow window;
                Core::AirPods::State state;
                state.model = Core::AirPods::Model::AirPods_5;
                state.displayName = QStringLiteral("AirPods 5");
                window.UpdateState(state);
                if (shownBeforeSwitch) {
                    window.Show();
                    QTRY_VERIFY(window.isVisible());
                }
                theme.SetMode(to);
                QCoreApplication::processEvents();
                if (!shownBeforeSwitch) {
                    window.Show();
                    QTRY_VERIFY(window.isVisible());
                }

                auto *label = window.findChild<QLabel *>("deviceLabel");
                const auto expected = theme.Colors().mainText;
                QVERIFY2(
                    label->palette().color(QPalette::WindowText) == expected,
                    qPrintable(QString{"shown first %1: palette %2 vs %3"}
                                   .arg(shownBeforeSwitch)
                                   .arg(label->palette().color(QPalette::WindowText).name())
                                   .arg(expected.name())));

                // The darkest (light theme) or brightest (dark theme) glyph pixel is the text
                // colour.
                const auto image = window.grab(label->geometry()).toImage();
                int extreme = theme.IsDark() ? 0 : 255;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        const int gray = qGray(image.pixel(x, y));
                        extreme =
                            theme.IsDark() ? (std::max)(extreme, gray) : (std::min)(extreme, gray);
                    }
                }
                QVERIFY2(
                    std::abs(extreme - qGray(expected.rgb())) <= 40,
                    qPrintable(QString{"shown first %1: title %2 vs expected %3"}
                                   .arg(shownBeforeSwitch)
                                   .arg(extreme)
                                   .arg(qGray(expected.rgb()))));
                window.close();
            }
        }
        theme.SetMode(originalMode);
    }

    void MainWindowShowsTheHeadsetAtFullSize()
    {
        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();
        for (const auto mode : {Gui::Theme::Mode::Light, Gui::Theme::Mode::Dark}) {
            theme.SetMode(mode);
            const QString themeName = mode == Gui::Theme::Mode::Light ? "light" : "dark";

            // The headset fills most of the picture's height, like the earbuds and their case do.
            Core::AirPods::State state;
            state.model = Core::AirPods::Model::AirPods_Max;
            state.displayName = QStringLiteral("AirPods Max");
            state.pods.left.battery = Core::AirPods::Battery{80};
            Gui::MainWindow window;
            auto *animation = window.findChild<Gui::Widget::AnimationView *>();
            QSignalSpy frames{animation, &Gui::Widget::AnimationView::FramePresented};
            window.UpdateState(state);
            window.Show();
            QTRY_VERIFY(window.isVisible());
            QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 10, 5000);

            // The keyed-out corners show the surface the popup paints behind the picture. It is
            // not the palette's window colour in every theme, and decoding can move it a level.
            const auto image = animation->grab().toImage();
            const QRgb surface = image.pixel(0, 0);
            const auto isHeadset = [surface](QRgb pixel) {
                return qAlpha(pixel) > 16 && (std::max)(
                                                 {std::abs(qRed(pixel) - qRed(surface)),
                                                  std::abs(qGreen(pixel) - qGreen(surface)),
                                                  std::abs(qBlue(pixel) - qBlue(surface))}) > 8;
            };
            int top = image.height(), bottom = -1;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (isHeadset(image.pixel(x, y))) {
                        top = (std::min)(top, y);
                        bottom = (std::max)(bottom, y);
                        break;
                    }
                }
            }
            QVERIFY2(bottom >= top, qPrintable(themeName));
            QVERIFY2(
                bottom - top + 1 >= image.height() * 0.75, qPrintable(QString{"%1: %2 of %3"}
                                                                          .arg(themeName)
                                                                          .arg(bottom - top + 1)
                                                                          .arg(image.height())));

            // ...but stops well above its battery ring, as on iOS.
            Gui::Widget::Battery *ring = nullptr;
            for (auto *candidate : window.findChildren<Gui::Widget::Battery *>()) {
                if (candidate->isVisibleTo(&window)) {
                    ring = candidate;
                }
            }
            QVERIFY(ring != nullptr);
            // The grab is in device pixels; the widget geometry is in logical ones.
            const qreal headsetBottom =
                animation->mapTo(&window, QPointF{0.0, (bottom + 1) / image.devicePixelRatio()})
                    .y();
            const int ringTop = ring->mapTo(&window, QPoint{}).y();
            QVERIFY2(
                ringTop - headsetBottom >= 12,
                qPrintable(QString{"%1: headset ends at %2, ring starts at %3"}
                               .arg(themeName)
                               .arg(headsetBottom)
                               .arg(ringTop)));

            const auto scale = qEnvironmentVariable("QT_SCALE_FACTOR", "system");
            VerifySmoothPopupCorners(
                &window, QString{"main-window-airpods-max-%1-scale%2.png"}.arg(themeName, scale));
        }
        theme.SetMode(originalMode);
    }

    void MainWindowCentresRingsUnderTheDevice()
    {
        for (auto model : magic_enum::enum_values<Core::AirPods::Model>()) {
            if (model == Core::AirPods::Model::Unknown || model == Core::AirPods::Model::_Max) {
                continue;
            }
            const auto presentation = Gui::GetAnimationPresentation(model);

            Core::AirPods::State state;
            state.model = model;
            state.displayName = QStringLiteral("AirPods");
            state.pods.left.battery = Core::AirPods::Battery{80};
            state.pods.right.battery = Core::AirPods::Battery{60};
            state.caseBox.battery = Core::AirPods::Battery{90};
            // One pod out, so both pod rings and the case ring are shown where they apply.
            state.pods.left.isInCase = true;
            state.pods.right.isInCase = false;

            Gui::MainWindow window;
            window.UpdateState(state);
            window.Show();
            QTRY_VERIFY(window.isVisible());
            auto *animation = window.findChild<Gui::Widget::AnimationView *>();
            QTRY_VERIFY(animation->isVisible());
            QCoreApplication::processEvents();

            const QRect view{animation->mapTo(&window, QPoint{}), animation->size()};
            const auto expected = [&](qreal fraction) {
                return view.x() + fraction * view.width();
            };
            const auto centreOf = [&](Gui::Widget::Battery *ring) {
                return ring->mapTo(&window, ring->rect().center()).x() + 0.5;
            };

            QList<Gui::Widget::Battery *> pods;
            Gui::Widget::Battery *caseRing = nullptr;
            for (auto *ring : window.findChildren<Gui::Widget::Battery *>()) {
                if (!ring->isVisibleTo(&window)) {
                    continue;
                }
                if (ring->getBadge() == Gui::Widget::Battery::Badge::Case) {
                    caseRing = ring;
                }
                else {
                    pods.append(ring);
                }
            }
            const auto name = Helper::ToString(model);
            QVERIFY2(!pods.isEmpty(), qPrintable(name));
            qreal podsCentre = 0;
            for (auto *ring : pods) {
                podsCentre += centreOf(ring);
            }
            podsCentre /= pods.size();
            QVERIFY2(
                std::abs(podsCentre - expected(presentation.podsCenter)) <= 3,
                qPrintable(QString{"%1 pods %2 vs %3"}
                               .arg(name)
                               .arg(podsCentre)
                               .arg(expected(presentation.podsCenter))));

            if (presentation.caseCenter.has_value()) {
                QVERIFY2(caseRing != nullptr, qPrintable(name));
                QVERIFY2(
                    std::abs(centreOf(caseRing) - expected(*presentation.caseCenter)) <= 3,
                    qPrintable(QString{"%1 case %2 vs %3"}
                                   .arg(name)
                                   .arg(centreOf(caseRing))
                                   .arg(expected(*presentation.caseCenter))));
                // The rows never run into each other.
                for (auto *ring : pods) {
                    QVERIFY2(
                        ring->mapTo(&window, ring->rect().topRight()).x() <
                            caseRing->mapTo(&window, caseRing->rect().topLeft()).x() +
                                caseRing->width() / 2,
                        qPrintable(name));
                }
            }
            else {
                // A headset or a neckband: no case ring, and the rings sit under the centred
                // device; a headset has just one.
                QVERIFY2(caseRing == nullptr, qPrintable(name));
                if (Core::AirPods::HasSingleBattery(model)) {
                    QCOMPARE(pods.size(), qsizetype{1});
                }
                QVERIFY2(std::abs(podsCentre - window.width() / 2.0) <= 3, qPrintable(name));
            }
        }
    }

    void MainWindowRendersBatteryRings()
    {
        Gui::MainWindow window;
        auto rings = window.findChildren<Gui::Widget::Battery *>();
        QCOMPARE(rings.size(), 3);
        for (auto *ring : rings) {
            QCOMPARE(ring->getShape(), Gui::Widget::Battery::Shape::Ring);
        }

        Core::AirPods::State state;
        state.model = Core::AirPods::Model::AirPods_Pro_2_USB_C;
        state.displayName = QStringLiteral("AirPods Pro");
        state.pods.left.battery = Core::AirPods::Battery{98};
        state.pods.right.battery = Core::AirPods::Battery{15};
        state.pods.right.isCharging = true;
        state.caseBox.battery = Core::AirPods::Battery{100};
        state.caseBox.isCharging = true;
        state.caseBox.isBothPodsInCase = true;
        state.pods.left.isInCase = state.pods.right.isInCase = true;
        state.caseBox.isLidOpened = true;
        window.UpdateState(state);

        // Ring plus label fit the popup's 51 px battery row; the rest of the layout is untouched.
        for (auto *ring : rings) {
            QVERIFY(ring->height() <= 51);
        }
        // Both pods in the case: one merged pods ring and the case ring, neither labelled.
        int visible = 0;
        for (auto *ring : rings) {
            if (ring->isVisibleTo(&window)) {
                ++visible;
                QCOMPARE(ring->getBadge(), Gui::Widget::Battery::Badge::None);
            }
        }
        QCOMPARE(visible, 2);

        const auto scale = qEnvironmentVariable("QT_SCALE_FACTOR", "system");
        VerifySmoothPopupCorners(
            &window, QString{"main-window-battery-rings-scale%1.png"}.arg(scale));
    }

    void UpdateTextHasAntialiasedEdges_data()
    {
        QTest::addColumn<QString>("localeName");
        QTest::newRow("English") << "en_US";
        QTest::newRow("Traditional Chinese") << "zh_TW";
    }

    void UpdateTextHasAntialiasedEdges()
    {
        QFETCH(QString, localeName);
        Gui::Theme::ApplyApplicationTypography(QLocale{localeName});
        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();
        Gui::UpdateWindow window{UpdateFixture()};
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (const auto mode : {Gui::Theme::Mode::Light, Gui::Theme::Mode::Dark}) {
            theme.SetMode(mode);
            for (const auto *name : {"promptTitle", "rowSubtitle", "updateButton", "releaseNotes"})
            {
                const auto *widget = window.findChild<QWidget *>(name);
                QVERIFY(widget);
                const auto ratio = window.devicePixelRatioF();
                QPixmap textImage{QSize{400, 60} * ratio};
                textImage.setDevicePixelRatio(ratio);
                const bool dark = mode == Gui::Theme::Mode::Dark;
                textImage.fill(dark ? Qt::black : Qt::white);
                {
                    QPainter painter{&textImage};
                    painter.setFont(widget->font());
                    painter.setPen(dark ? Qt::white : Qt::black);
                    painter.drawText(QPoint{8, 30}, QStringLiteral("AirPodsDesktop 更新版本 2.12"));
                }
                const auto pixels = textImage.toImage();
                int intermediatePixels = 0;
                for (int y = 0; y < pixels.height(); ++y) {
                    for (int x = 0; x < pixels.width(); ++x) {
                        const auto pixel = pixels.pixel(x, y);
                        if (qRed(pixel) > 0 && qRed(pixel) < 255) {
                            ++intermediatePixels;
                        }
                    }
                }
                // Check smooth raster edges while allowing the system's ClearType rendering.
                QVERIFY2(intermediatePixels > 0, name);
            }
        }
        Gui::Theme::ApplyApplicationTypography(QLocale{"en_US"});
        theme.SetMode(originalMode);
    }

    void UpdatePromptActions()
    {
        UpdateUrlRecorder recorder;
        for (const auto *name : {"skipButton", "laterButton", "escape", "close"}) {
            Gui::UpdateWindow window{UpdateFixture()};
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto *notes = window.findChild<QPlainTextEdit *>("releaseNotes");
            QVERIFY(notes->isReadOnly());
            QCOMPARE(notes->toPlainText(), UpdateFixture().changeLog);
            QTest::mouseClick(window.findChild<QPushButton *>("viewButton"), Qt::LeftButton);
            QVERIFY(window.isVisible());
            QCOMPARE(recorder.urls.last(), QUrl{UpdateFixture().url});
            if (QString{name} == "escape") {
                QTest::keyClick(&window, Qt::Key_Escape);
            }
            else if (QString{name} == "close") {
                window.close();
            }
            else {
                QTest::mouseClick(window.findChild<QPushButton *>(name), Qt::LeftButton);
            }
            const auto expected =
                QString{name} == "updateButton" ? Gui::UpdateWindow::Action::Update
                : QString{name} == "skipButton" ? Gui::UpdateWindow::Action::Skip
                                                : Gui::UpdateWindow::Action::Later;
            QCOMPARE(window.SelectedAction(), expected);
            QVERIFY(!window.isVisible());
        }
    }

    void UnsupportedUpdateOpensReleaseWithoutDownloading()
    {
        UpdateUrlRecorder recorder;
        auto info = UpdateFixture();
        info.sha256.clear();
        SimulatedDownload download;
        Gui::UpdateWindow window{info, nullptr, download.Function()};
        window.show();
        window.findChild<QPushButton *>("updateButton")->click();
        QCOMPARE(download.calls.load(), 0);
        QCOMPARE(window.Result(), Gui::UpdateWindow::Outcome::ManualDownload);
        QCOMPARE(window.SelectedAction(), Gui::UpdateWindow::Action::Update);
        QCOMPARE(recorder.urls, QList<QUrl>{QUrl{info.url}});
        QVERIFY(!window.isVisible());
    }

    void DownloadProgressAndCompletion()
    {
        SimulatedDownload download;
        Gui::UpdateWindow window{UpdateFixture(), nullptr, download.Function()};
        QCOMPARE(download.calls.load(), 0);
        QSignalSpy finished{&window, &QDialog::finished};
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto nativeWindow = window.winId();
        auto *notes = window.findChild<QPlainTextEdit *>("releaseNotes");
        const auto notesPosition = notes->mapTo(&window, QPoint{});
        const auto windowPosition = window.pos();
        QTest::mouseClick(window.findChild<QPushButton *>("updateButton"), Qt::LeftButton);
        window.StartDownload();
        QCOMPARE(window.winId(), nativeWindow);
        QCOMPARE(window.SelectedAction(), Gui::UpdateWindow::Action::Update);
        QCOMPARE(notes->mapTo(&window, QPoint{}), notesPosition);
        QCOMPARE(window.pos(), windowPosition);
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *bar = window.findChild<QProgressBar *>("progressBar");
        QTRY_COMPARE(download.calls.load(), 1);
        QCOMPARE(bar->maximum(), 0);
        QTest::keyClick(&window, Qt::Key_Escape);
        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        download.stage = 1;
        QTRY_COMPARE(bar->value(), 750);
        QVERIFY(window.findChild<QLabel *>("progressDetails")->text().contains("75%"));
        window.UpdateProgressSafely(quint64{1} << 32, quint64{1} << 33);
        QCOMPARE(bar->value(), 500);
        download.stage = 0;
        QTRY_COMPARE(bar->maximum(), 0);
        const auto downloadingHeight = window.height();
        download.stage = 2;
        QTRY_COMPARE(bar->value(), 1000);
        QVERIFY(!window.findChild<QPushButton *>("pushButtonDownloadManually")->isEnabled());
        QVERIFY(window.findChild<QLabel *>("hint")->isHidden());
        QVERIFY(window.height() < downloadingHeight);
        QTest::keyClick(&window, Qt::Key_Escape);
        QVERIFY(!window.close());
        QCOMPARE(bar->accessibleName(), QString{"Preparing to install..."});
        QCOMPARE(finished.count(), 0);
        QCOMPARE(window.Result(), Gui::UpdateWindow::Outcome::KeepRunning);
        download.stage = 4;
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(window.Result(), Gui::UpdateWindow::Outcome::InstallerStarted);
        QVERIFY(!window.isVisible());
    }

    void DownloadFailureAndManualActions()
    {
        UpdateUrlRecorder recorder;
        for (const auto *action : {"closeButton", "escape", "close"}) {
            Gui::UpdateWindow window{
                UpdateFixture(), nullptr, [](const auto &, const auto &) { return false; }};
            QSignalSpy finished{&window, &QDialog::finished};
            window.StartDownload();
            window.show();
            auto *close = window.findChild<QPushButton *>("closeButton");
            QTRY_VERIFY(close->isVisible());
            QVERIFY(window.windowFlags().testFlag(Qt::WindowCloseButtonHint));
            window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            QCOMPARE(QApplication::focusWidget(), close);
            const auto focusedSize = close->size();
            window.findChild<QPushButton *>("viewButton")->setFocus();
            QCOMPARE(close->size(), focusedSize);
            close->setFocus();
            QCOMPARE(finished.count(), 0);
            QVERIFY(!window.findChild<QProgressBar *>("progressBar")->isVisible());
            const auto failureText = window.findChild<QLabel *>("failureDescription")->text();
            window.UpdateProgressSafely(100, 100);
            QCOMPARE(window.findChild<QLabel *>("failureDescription")->text(), failureText);
            const auto urlCount = recorder.urls.size();
            QTest::mouseClick(
                window.findChild<QPushButton *>("failedManualButton"), Qt::LeftButton);
            QCOMPARE(recorder.urls.size(), urlCount + 1);
            QVERIFY(window.isVisible());
            QCOMPARE(finished.count(), 0);
            QCOMPARE(window.Result(), Gui::UpdateWindow::Outcome::KeepRunning);
            if (QString{action} == "escape") {
                QTest::keyClick(&window, Qt::Key_Escape);
            }
            else if (QString{action} == "close") {
                window.close();
            }
            else {
                QTest::mouseClick(close, Qt::LeftButton);
            }
            QCOMPARE(finished.count(), 1);
            QCOMPARE(window.Result(), Gui::UpdateWindow::Outcome::KeepRunning);
            QCOMPARE(window.SelectedAction(), Gui::UpdateWindow::Action::Update);
        }
        SimulatedDownload download;
        Gui::UpdateWindow active{UpdateFixture(), nullptr, download.Function()};
        active.StartDownload();
        active.show();
        QVERIFY(QTest::qWaitForWindowExposed(&active));
        QTest::mouseClick(
            active.findChild<QPushButton *>("pushButtonDownloadManually"), Qt::LeftButton);
        QCOMPARE(active.Result(), Gui::UpdateWindow::Outcome::ManualDownload);
        QVERIFY(!active.isVisible());
    }

    void FailedDownloadStaysInModalLoop()
    {
        Gui::UpdateWindow window{
            UpdateFixture(), nullptr, [](const auto &, const auto &) { return false; }};
        bool failureWasVisible = false;
        bool userClosed = false;
        QTimer closeWhenFailed;
        closeWhenFailed.setInterval(10);
        connect(&closeWhenFailed, &QTimer::timeout, &window, [&] {
            auto *button = window.findChild<QPushButton *>("closeButton");
            if (button->isVisible()) {
                failureWasVisible = window.isVisible();
                userClosed = true;
                button->click();
            }
        });
        QTimer::singleShot(3000, &window, &QDialog::accept);
        closeWhenFailed.start();
        window.StartDownload();
        window.exec();
        QVERIFY(failureWasVisible);
        QVERIFY(userClosed);
        QCOMPARE(window.Result(), Gui::UpdateWindow::Outcome::KeepRunning);
    }

    void UpdateWindowsFollowLanguageAndThemeChanges()
    {
        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();
        Gui::UpdateWindow prompt{UpdateFixture()};
        Gui::UpdateWindow failure{
            UpdateFixture(), nullptr, [](const auto &, const auto &) { return false; }};
        failure.StartDownload();
        failure.show();
        QTRY_VERIFY(failure.findChild<QPushButton *>("closeButton")->isVisible());
        const auto englishTitle = failure.findChild<QLabel *>("title")->text();
        QTranslator translator;
        QVERIFY(translator.load(
            QLocale{"zh_TW"}, "apd", "_",
            QCoreApplication::applicationDirPath() + "/translations"));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        QCoreApplication::processEvents();
        QCOMPARE(
            failure.findChild<QLabel *>("title")->text(),
            translator.translate("Gui::UpdateWindow", "Unable to update"));
        QCOMPARE(
            prompt.findChild<QPushButton *>("updateButton")->text(),
            translator.translate("UpdateWindow", "Update now"));
        QCOMPARE(
            prompt.findChild<QPlainTextEdit *>("releaseNotes")->toPlainText(),
            UpdateFixture().changeLog);
        theme.SetMode(Gui::Theme::Mode::Dark);
        QCOMPARE(failure.palette().color(QPalette::Window), theme.Colors().windowBackground);
        theme.SetMode(Gui::Theme::Mode::Light);
        QCOMPARE(failure.palette().color(QPalette::Window), theme.Colors().windowBackground);
        QCoreApplication::removeTranslator(&translator);
        QCoreApplication::processEvents();
        QCOMPARE(failure.findChild<QLabel *>("title")->text(), englishTitle);
        QVERIFY(failure.isVisible());
        failure.close();
        theme.SetMode(originalMode);
    }

    void UpdateWindowsRendering_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<QString>("localeName");
        for (const auto *locale : {"en_US", "zh_TW"}) {
            QTest::newRow(qPrintable(QString{locale} + "-light")) << false << locale;
            QTest::newRow(qPrintable(QString{locale} + "-dark")) << true << locale;
        }
    }

    void UpdateWindowsRendering()
    {
        QFETCH(bool, dark);
        QFETCH(QString, localeName);
        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();
        QTranslator translator;
        if (localeName == "zh_TW") {
            QVERIFY(translator.load(
                QLocale{localeName}, "apd", "_",
                QCoreApplication::applicationDirPath() + "/translations"));
            QVERIFY(QCoreApplication::installTranslator(&translator));
        }
        Gui::Theme::ApplyApplicationTypography(QLocale{localeName});
        theme.SetMode(dark ? Gui::Theme::Mode::Dark : Gui::Theme::Mode::Light);
        const auto outputDir = QStringLiteral(APD_BINARY_DIR "/UiValidation");
        QDir{}.mkpath(outputDir);
        const auto suffix = QString{"-%1-%2-scale%3.png"}.arg(
            localeName, dark ? "dark" : "light", qEnvironmentVariable("QT_SCALE_FACTOR", "1"));
        const auto verifyLayout = [](QWidget &window) {
            if (window.width() != 560) {
                return false;
            }
            for (auto *button : window.findChildren<QPushButton *>()) {
                if (button->isVisible() &&
                    (!window.rect().contains(
                         QRect{button->mapTo(&window, QPoint{}), button->size()}) ||
                     button->height() != 28 ||
                     button->width() < button->fontMetrics().horizontalAdvance(button->text()) +
                                           (button->isFlat() ? 14 : 32)))
                {
                    qWarning() << button->objectName() << button->size() << button->text()
                               << button->fontMetrics().horizontalAdvance(button->text());
                    return false;
                }
            }
            auto *status = window.findChild<QLabel *>("status");
            auto *details = window.findChild<QLabel *>("progressDetails");
            if (status->isVisible() &&
                (status->fontMetrics().horizontalAdvance(status->text()) > status->width() ||
                 details->fontMetrics().horizontalAdvance(details->text()) > details->width() ||
                 status->geometry().intersects(details->geometry())))
            {
                qWarning() << status->geometry() << details->geometry();
                return false;
            }
            return true;
        };
        auto stableInfo = UpdateFixture();
        stableInfo.isPreRelease = false;
        Gui::UpdateWindow stable{stableInfo};
        stable.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stable));
        QVERIFY(stable.grab().save(outputDir + "/update-stable" + suffix));
        QVERIFY(verifyLayout(stable));
        stable.close();
        Gui::UpdateWindow prompt{UpdateFixture()};
        prompt.show();
        QVERIFY(QTest::qWaitForWindowExposed(&prompt));
        QVERIFY(verifyLayout(prompt));
        QVERIFY(prompt.grab().save(outputDir + "/update-prompt" + suffix));
        auto *notes = prompt.findChild<QPlainTextEdit *>("releaseNotes");
        prompt.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&prompt));
        notes->setFocus();
        QTest::keyClick(notes, Qt::Key_Tab);
        QCOMPARE(QApplication::focusWidget(), prompt.findChild<QPushButton *>("viewButton"));
        prompt.close();
        auto longInfo = UpdateFixture();
        longInfo.version = QVersionNumber{20260904, 12345678, 2147483647};
        longInfo.changeLog = QString{"A long line without spaces: "} + QString(600, QChar{'X'}) +
                             QString{"\nRelease note\n"}.repeated(100);
        Gui::UpdateWindow longPrompt{longInfo};
        longPrompt.show();
        QVERIFY(QTest::qWaitForWindowExposed(&longPrompt));
        QVERIFY(verifyLayout(longPrompt));
        QTRY_VERIFY(
            longPrompt.findChild<QPlainTextEdit *>("releaseNotes")->verticalScrollBar()->maximum() >
            0);
        QVERIFY(longPrompt.grab().save(outputDir + "/update-long" + suffix));
        longPrompt.close();
        auto emptyInfo = UpdateFixture();
        emptyInfo.changeLog.clear();
        emptyInfo.isPreRelease = false;
        Gui::UpdateWindow emptyPrompt{emptyInfo};
        emptyPrompt.show();
        QVERIFY(QTest::qWaitForWindowExposed(&emptyPrompt));
        QVERIFY(
            emptyPrompt.findChild<QPlainTextEdit *>("releaseNotes")->isHidden() ||
            !emptyPrompt.findChild<QPlainTextEdit *>("releaseNotes")->isVisible());
        QVERIFY(emptyPrompt.findChild<QWidget *>("noChangeLogRow")->isVisible());
        QVERIFY(verifyLayout(emptyPrompt));
        QVERIFY(emptyPrompt.grab().save(outputDir + "/update-empty" + suffix));
        emptyPrompt.close();
        SimulatedDownload download;
        download.stage = 0;
        Gui::UpdateWindow progress{UpdateFixture(), nullptr, download.Function()};
        progress.show();
        QVERIFY(QTest::qWaitForWindowExposed(&progress));
        const auto notesPosition =
            progress.findChild<QPlainTextEdit *>("releaseNotes")->mapTo(&progress, QPoint{});
        const auto nativeWindow = progress.winId();
        progress.findChild<QPushButton *>("updateButton")->click();
        QCOMPARE(progress.winId(), nativeWindow);
        QCOMPARE(
            progress.findChild<QPlainTextEdit *>("releaseNotes")->mapTo(&progress, QPoint{}),
            notesPosition);
        QTRY_COMPARE(progress.findChild<QProgressBar *>("progressBar")->maximum(), 0);
        QVERIFY(verifyLayout(progress));
        QVERIFY(progress.grab().save(outputDir + "/update-waiting" + suffix));
        download.stage = 1;
        QTRY_COMPARE(progress.findChild<QProgressBar *>("progressBar")->value(), 750);
        QVERIFY(verifyLayout(progress));
        QVERIFY(progress.grab().save(outputDir + "/update-download" + suffix));
        download.stage = 2;
        QTRY_COMPARE(progress.findChild<QProgressBar *>("progressBar")->value(), 1000);
        QVERIFY(verifyLayout(progress));
        QVERIFY(progress.grab().save(outputDir + "/update-preparing" + suffix));
        download.stage = 5;
        QTRY_VERIFY(progress.findChild<QPushButton *>("closeButton")->isVisible());
        QVERIFY(verifyLayout(progress));
        QVERIFY(progress.grab().save(outputDir + "/update-failure" + suffix));
        progress.close();
        theme.SetMode(originalMode);
        QCoreApplication::removeTranslator(&translator);
        Gui::Theme::ApplyApplicationTypography(QLocale{"en_US"});
    }

    void BundledTypographyLoadsInterFamilies()
    {
        QVERIFY(Gui::Theme::LoadBundledFonts());

        const auto families = QFontDatabase{}.families();
        QVERIFY(families.contains("Inter"));
        QVERIFY(families.contains("Inter Display"));
        QVERIFY(families.contains("Noto Sans TC"));

        QFont traditionalChineseFont{"Noto Sans TC"};
        const auto traditionalChineseSample = QString::fromUtf8(
            "\xE7\xB9\x81\xE9\xAB\x94\xE4\xB8\xAD\xE6\x96\x87\xE8\xA8\xAD\xE5\xAE\x9A");
        const auto glyphs = QRawFont::fromFont(traditionalChineseFont)
                                .glyphIndexesForString(traditionalChineseSample);
        QVERIFY(!glyphs.isEmpty());
        QVERIFY(
            std::all_of(glyphs.cbegin(), glyphs.cend(), [](quint32 glyph) { return glyph != 0; }));

        const auto applicationFont = Gui::Theme::ApplicationFont(QLocale{"zh_TW"});
        QCOMPARE(applicationFont.families().first(), QStringLiteral("Inter"));
        QCOMPARE(applicationFont.families().at(1), QStringLiteral("Noto Sans TC"));
        QCOMPARE(
            Gui::Theme::DisplayFontFamilies(QLocale{"zh_TW"}).first(),
            QStringLiteral("Inter Display"));

        struct LocaleFontCase {
            const char *locale;
            const char *bodyFallback;
            const char *displayFallback;
        };
        constexpr LocaleFontCase cases[]{
            {"zh_CN", "Microsoft YaHei UI", "Microsoft YaHei UI"},
            {"zh_TW", "Noto Sans TC", "Noto Sans TC"},
            {"ja_JP", "Yu Gothic UI", "Yu Gothic UI"},
            {"ko_KR", "Malgun Gothic", "Malgun Gothic"},
            {"de_DE", "Segoe UI Variable Text", "Inter"},
        };
        for (const auto &fontCase : cases) {
            QCOMPARE(
                Gui::Theme::ApplicationFont(QLocale{fontCase.locale}).families().at(1),
                QString::fromLatin1(fontCase.bodyFallback));
            QCOMPARE(
                Gui::Theme::DisplayFontFamilies(QLocale{fontCase.locale}).at(1),
                QString::fromLatin1(fontCase.displayFallback));
        }
    }

    void SettingsWindowSupportsKeyboardAndCompactLayout()
    {
        auto backend = std::make_shared<Core::QuickConnect::NullBackend>();
        Core::QuickConnect::Controller quickConnect{backend};

        Gui::Theme::ApplyApplicationTypography(QLocale{"zh_TW"});
        Gui::SettingsWindow window{[] { return 0; }, quickConnect};
        window.resize(640, 420);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto *languages = window.findChild<QComboBox *>("cbLanguages");
        QVERIFY(languages != nullptr);
        QCOMPARE(languages->itemText(0), QStringLiteral("English"));

        auto *navigation = window.findChild<QListWidget *>("navList");
        QVERIFY(navigation != nullptr);
        QCOMPARE(navigation->frameShape(), QFrame::NoFrame);
        QCOMPARE(navigation->focusPolicy(), Qt::StrongFocus);
        QVERIFY(!qApp->styleSheet().contains(QStringLiteral("border-left")));
        navigation->setFocus();
        QCOMPARE(QApplication::focusWidget(), navigation);
        QCOMPARE(navigation->currentRow(), 0);
        QTest::keyClick(navigation, Qt::Key_Down);
        QCOMPARE(navigation->currentRow(), 1);

        auto *appearanceMode = window.findChild<QComboBox *>("cbAppearanceMode");
        QVERIFY(appearanceMode != nullptr);
        QCOMPARE(appearanceMode->count(), 3);
        QCOMPARE(appearanceMode->sizeAdjustPolicy(), QComboBox::AdjustToContents);
        QVERIFY(!appearanceMode->accessibleName().isEmpty());

        const auto appearanceTextFits = [appearanceMode] {
            QStyleOptionComboBox option;
            option.initFrom(appearanceMode);
            option.currentText = appearanceMode->currentText();
            const auto textRect = appearanceMode->style()->subControlRect(
                QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, appearanceMode);
            const QFontMetrics metrics{appearanceMode->font()};
            for (int i = 0; i < appearanceMode->count(); ++i) {
                if (metrics.horizontalAdvance(appearanceMode->itemText(i)) > textRect.width()) {
                    return false;
                }
            }
            return true;
        };
        QVERIFY(appearanceTextFits());

        for (const auto *name : {
                 "cbLowAudioLatency",
                 "cbAutoEarDetection",
                 "cbLidPopup",
                 "cbTrayQuickConnectEnabled",
             })
        {
            auto *checkBox = window.findChild<QCheckBox *>(name);
            QVERIFY(checkBox != nullptr);
            QVERIFY(!checkBox->accessibleName().isEmpty());
            QVERIFY(checkBox->minimumWidth() >= 32);
            QVERIFY(checkBox->minimumHeight() >= 32);
        }

        auto *scrollArea = window.findChild<QScrollArea *>("pagesScrollArea");
        QVERIFY(scrollArea != nullptr);
        QVERIFY(scrollArea->widgetResizable());
        QVERIFY(!scrollArea->horizontalScrollBar()->isVisible());

        for (auto *radioButton : window.findChildren<QRadioButton *>()) {
            if (!radioButton->isVisible())
                continue;

            const auto rightEdge =
                radioButton->mapTo(scrollArea->viewport(), radioButton->rect().topRight()).x();
            QVERIFY2(
                rightEdge < scrollArea->viewport()->width(),
                radioButton->objectName().toUtf8().constData());
        }

        auto *buttonBox = window.findChild<QDialogButtonBox *>("buttonBox");
        QVERIFY(buttonBox != nullptr);
        const auto buttonBottom = buttonBox->mapTo(&window, buttonBox->rect().bottomLeft()).y();
        QVERIFY(buttonBottom < window.height());

        QTranslator traditionalChinese;
        const auto translationsDir = QCoreApplication::applicationDirPath() + "/translations";
        QVERIFY(traditionalChinese.load(QLocale{"zh_TW"}, "apd", "_", translationsDir));
        QVERIFY(QCoreApplication::installTranslator(&traditionalChinese));
        QCoreApplication::processEvents();
        QCOMPARE(navigation->item(0)->text(), QString::fromUtf8("\xE4\xB8\x80\xE8\x88\xAC"));
        QCOMPARE(navigation->item(1)->text(), QString::fromUtf8("\xE5\xA4\x96\xE8\xA7\x80"));
        QTRY_COMPARE(
            buttonBox->button(QDialogButtonBox::RestoreDefaults)->text(),
            QString::fromUtf8("\xE9\x82\x84\xE5\x8E\x9F\xE9\xA0\x90\xE8\xA8\xAD\xE5\x80\xBC"));
        QTRY_COMPARE(
            buttonBox->button(QDialogButtonBox::Close)->text(),
            QString::fromUtf8("\xE9\x97\x9C\xE9\x96\x89"));
        QVERIFY(appearanceTextFits());

        const auto outputDir = QStringLiteral(APD_BINARY_DIR "/UiValidation");
        QDir{}.mkpath(outputDir);
        QVERIFY(window.grab().save(outputDir + "/settings-current-theme.png"));

        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();

        theme.SetMode(Gui::Theme::Mode::Dark);
        QVERIFY(theme.IsDark());
        QCOMPARE(theme.Colors().windowBackground, QColor{"#1C1C1E"});
        QCOMPARE(theme.Colors().surface, QColor{"#2C2C2E"});
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(outputDir + "/settings-dark.png"));
        appearanceMode->showPopup();
        QTRY_VERIFY(QApplication::activePopupWidget() != nullptr);
        auto *comboPopup = QApplication::activePopupWidget();
        QCOMPARE(comboPopup->objectName(), QStringLiteral("apdComboPopup"));
        QVERIFY(appearanceMode->view()->viewport()->mapTo(comboPopup, QPoint{}).x() >= 4);
        QVERIFY(!appearanceMode->view()->verticalScrollBar()->isVisible());
        appearanceMode->view()->setCurrentIndex(appearanceMode->model()->index(0, 0));
        QVERIFY(appearanceMode->view()->selectionModel()->isSelected(
            appearanceMode->model()->index(0, 0)));
        QTest::qWait(100);
        VerifySmoothPopupCorners(comboPopup, "combo-popup-dark.png");
        appearanceMode->hidePopup();

        theme.SetMode(Gui::Theme::Mode::Light);
        QVERIFY(!theme.IsDark());
        QCOMPARE(theme.Colors().windowBackground, QColor{"#F5F5F7"});
        QCOMPARE(theme.Colors().surface, QColor{"#FFFFFF"});
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(outputDir + "/settings-light.png"));
        appearanceMode->showPopup();
        QTRY_VERIFY(QApplication::activePopupWidget() != nullptr);
        comboPopup = QApplication::activePopupWidget();
        QCOMPARE(comboPopup->objectName(), QStringLiteral("apdComboPopup"));
        QVERIFY(appearanceMode->view()->viewport()->mapTo(comboPopup, QPoint{}).x() >= 4);
        QVERIFY(!appearanceMode->view()->verticalScrollBar()->isVisible());
        appearanceMode->view()->setCurrentIndex(appearanceMode->model()->index(2, 0));
        QVERIFY(appearanceMode->view()->selectionModel()->isSelected(
            appearanceMode->model()->index(2, 0)));
        QTest::qWait(100);
        VerifySmoothPopupCorners(comboPopup, "combo-popup-light.png");
        appearanceMode->hidePopup();

        theme.SetMode(originalMode);
        QCoreApplication::removeTranslator(&traditionalChinese);
        Gui::Theme::ApplyApplicationTypography(QLocale{"en_US"});
    }

    void TrayMenuHasSmoothCorners()
    {
        auto backend = std::make_shared<Core::QuickConnect::NullBackend>();
        Core::QuickConnect::Controller quickConnect{backend};
        Gui::TrayIcon tray{[] { return 0; }, quickConnect};
        auto *menu = tray.findChild<QMenu *>();
        QVERIFY(menu != nullptr);
        const auto actions = menu->actions();
        const auto checkableAction =
            std::find_if(actions.cbegin(), actions.cend(), [](const QAction *action) {
                return action->isCheckable();
            });
        QVERIFY(checkableAction != actions.cend());
        (*checkableAction)->setChecked(true);
        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();
        QDir{}.mkpath(QStringLiteral(APD_BINARY_DIR "/UiValidation"));
        for (const auto mode : {Gui::Theme::Mode::Light, Gui::Theme::Mode::Dark}) {
            theme.SetMode(mode);
            tray.ShowContextMenu(QPoint{100, 100});
            QVERIFY(QTest::qWaitForWindowExposed(menu));
            for (const bool last : {false, true}) {
                if (last) {
                    menu->setActiveAction(menu->actions().last());
                }
                else {
                    QTest::keyClick(menu, Qt::Key_Down);
                }
                QVERIFY(menu->activeAction() != nullptr);
                VerifySmoothPopupCorners(
                    menu, QString{"tray-menu-%1-%2.png"}
                              .arg(theme.IsDark() ? "dark" : "light")
                              .arg(last ? "last" : "first"));
            }
            QTest::keyClick(menu, Qt::Key_Escape);
            QTRY_VERIFY(!menu->isVisible());
        }
        theme.SetMode(originalMode);
    }

    void SettingsDescriptionsFollowConsecutiveLanguageChanges()
    {
        auto backend = std::make_shared<Core::QuickConnect::NullBackend>();
        Core::QuickConnect::Controller quickConnect{backend};
        Gui::SettingsWindow window{[] { return 0; }, quickConnect};

        auto *description = window.findChild<QLabel *>("lbDescLowAudioLatency");
        QVERIFY(description != nullptr);

        constexpr auto sourceText =
            "Keeps the audio device awake while your AirPods are connected so short sounds start "
            "immediately. This may produce audible hiss and use more battery.";
        const auto translationsDir = QCoreApplication::applicationDirPath() + "/translations";

        QTranslator first;
        QVERIFY(first.load(QLocale{"zh_TW"}, "apd", "_", translationsDir));
        const auto firstDescription = first.translate("QObject", sourceText);
        QVERIFY(!firstDescription.isEmpty());
        Gui::Theme::ApplyApplicationTypography(QLocale{"zh_TW"});
        QVERIFY(QCoreApplication::installTranslator(&first));
        QCoreApplication::processEvents();
        QCOMPARE(description->text(), firstDescription);
        QCOMPARE(qApp->font().families().at(1), QStringLiteral("Noto Sans TC"));

        QTranslator second;
        QVERIFY(second.load(QLocale{"de_DE"}, "apd", "_", translationsDir));
        const auto secondDescription = second.translate("QObject", sourceText);
        QVERIFY(!secondDescription.isEmpty());
        QCoreApplication::removeTranslator(&first);
        Gui::Theme::ApplyApplicationTypography(QLocale{"de_DE"});
        QVERIFY(QCoreApplication::installTranslator(&second));
        QCoreApplication::processEvents();
        QCOMPARE(description->text(), secondDescription);
        QCOMPARE(qApp->font().families().at(1), QStringLiteral("Segoe UI Variable Text"));

        QCoreApplication::removeTranslator(&second);
        Gui::Theme::ApplyApplicationTypography(QLocale{"en_US"});
    }
};

int main(int argc, char **argv)
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app{argc, argv};
    // Production logging installs a Qt message handler during static initialization. Restore the
    // default handler so QTest assertion details remain visible in CTest and CI output.
    qInstallMessageHandler(nullptr);
    // UI interactions exercise the real settings handlers without writing the user's registry.
    Core::Settings::SetRepository(std::make_unique<Core::Settings::MemoryRepository>());
    if (app.arguments().contains("--tray-quit-probe")) {
        app.setQuitOnLastWindowClosed(false);
        Core::QuickConnect::Controller quickConnect{
            std::make_shared<Core::QuickConnect::NullBackend>()};
        Gui::TrayIcon tray{[] { return 0; }, quickConnect};
        Gui::MainWindow battery;
        battery.show();
        QAction *quit = nullptr;
        for (auto *action : tray.findChildren<QAction *>()) {
            if (action->text() == "Quit") {
                quit = action;
                break;
            }
        }
        if (!quit) {
            return 3;
        }
        QTimer::singleShot(50, quit, &QAction::trigger);
        QTimer::singleShot(1000, &app, [] { QCoreApplication::exit(2); });
        return app.exec();
    }
    Gui::Theme::ApplyApplicationTypography(QLocale{"en_US"});
    UiRenderingTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "UiRenderingTests.moc"
