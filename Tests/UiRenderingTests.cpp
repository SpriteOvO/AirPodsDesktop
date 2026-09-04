#include <algorithm>
#include <atomic>

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
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QThread>
#include <QUrl>
#include <QTimer>

#include "Source/Core/QuickConnect.h"
#include "Source/Gui/SettingsWindow.h"
#include "Source/Gui/Theme.h"
#include "Source/Gui/DownloadWindow.h"
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
Core::Update::ReleaseInfo UpdateFixture()
{
    Core::Update::ReleaseInfo info;
    info.version = QVersionNumber{2, 12, 345};
    info.url = "https://example.invalid/release";
    info.isPreRelease = true;
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

    Gui::DownloadWindow::DownloadFunction Function()
    {
        return [this](const auto &info, const auto &progress) { return Run(info, progress); };
    }
};
} // namespace

class UiRenderingTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void UpdatePromptActions()
    {
        UpdateUrlRecorder recorder;
        for (const auto *name : {"updateButton", "skipButton", "laterButton", "escape", "close"}) {
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

    void DownloadProgressAndCompletion()
    {
        SimulatedDownload download;
        Gui::DownloadWindow window{UpdateFixture()};
        QCOMPARE(download.calls.load(), 0);
        QSignalSpy finished{&window, &QDialog::finished};
        window.show();
        window.StartDownload(download.Function());
        window.StartDownload(download.Function());
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
        download.stage = 2;
        QTRY_COMPARE(bar->value(), 1000);
        QCOMPARE(window.findChild<QLabel *>("status")->text(), QString{"Preparing to install..."});
        QCOMPARE(finished.count(), 0);
        QCOMPARE(window.Result(), Gui::DownloadWindow::Outcome::KeepRunning);
        download.stage = 4;
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(window.Result(), Gui::DownloadWindow::Outcome::InstallerStarted);
        QVERIFY(!window.isVisible());
    }

    void DownloadFailureAndManualActions()
    {
        UpdateUrlRecorder recorder;
        for (const auto *action : {"closeButton", "escape", "close"}) {
            Gui::DownloadWindow window{UpdateFixture()};
            QSignalSpy finished{&window, &QDialog::finished};
            window.StartDownload([](const auto &, const auto &) { return false; });
            window.show();
            auto *close = window.findChild<QPushButton *>("closeButton");
            QTRY_VERIFY(close->isVisible());
            QVERIFY(window.windowFlags().testFlag(Qt::WindowCloseButtonHint));
            QCOMPARE(finished.count(), 0);
            QVERIFY(!window.findChild<QProgressBar *>("progressBar")->isVisible());
            const auto failureText = window.findChild<QLabel *>("status")->text();
            window.UpdateProgressSafely(100, 100);
            QCOMPARE(window.findChild<QLabel *>("status")->text(), failureText);
            const auto urlCount = recorder.urls.size();
            QTest::mouseClick(
                window.findChild<QPushButton *>("pushButtonDownloadManually"), Qt::LeftButton);
            QCOMPARE(recorder.urls.size(), urlCount + 1);
            QVERIFY(window.isVisible());
            QCOMPARE(finished.count(), 0);
            QCOMPARE(window.Result(), Gui::DownloadWindow::Outcome::KeepRunning);
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
            QCOMPARE(window.Result(), Gui::DownloadWindow::Outcome::KeepRunning);
        }
        SimulatedDownload download;
        Gui::DownloadWindow active{UpdateFixture()};
        active.StartDownload(download.Function());
        active.show();
        QVERIFY(QTest::qWaitForWindowExposed(&active));
        QTest::mouseClick(
            active.findChild<QPushButton *>("pushButtonDownloadManually"), Qt::LeftButton);
        QCOMPARE(active.Result(), Gui::DownloadWindow::Outcome::ManualDownload);
        QVERIFY(!active.isVisible());
    }

    void FailedDownloadStaysInModalLoop()
    {
        Gui::DownloadWindow window{UpdateFixture()};
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
        window.StartDownload([](const auto &, const auto &) { return false; });
        window.exec();
        QVERIFY(failureWasVisible);
        QVERIFY(userClosed);
        QCOMPARE(window.Result(), Gui::DownloadWindow::Outcome::KeepRunning);
    }

    void UpdateWindowsFollowLanguageAndThemeChanges()
    {
        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();
        Gui::UpdateWindow prompt{UpdateFixture()};
        Gui::DownloadWindow failure{UpdateFixture()};
        failure.StartDownload([](const auto &, const auto &) { return false; });
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
            translator.translate("Gui::DownloadWindow", "Unable to update"));
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
            for (auto *button : window.findChildren<QPushButton *>()) {
                if (button->isVisible() &&
                    (!window.rect().contains(
                         QRect{button->mapTo(&window, QPoint{}), button->size()}) ||
                     button->width() <
                         button->fontMetrics().horizontalAdvance(button->text()) + 24))
                {
                    return false;
                }
            }
            return true;
        };
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
        QVERIFY(!emptyPrompt.findChild<QPlainTextEdit *>("releaseNotes")->toPlainText().isEmpty());
        QVERIFY(emptyPrompt.findChild<QLabel *>("preRelease")->isHidden());
        SimulatedDownload download;
        download.stage = 1;
        Gui::DownloadWindow progress{UpdateFixture()};
        progress.StartDownload(download.Function());
        progress.show();
        QVERIFY(QTest::qWaitForWindowExposed(&progress));
        QTRY_COMPARE(progress.findChild<QProgressBar *>("progressBar")->value(), 750);
        QVERIFY(verifyLayout(progress));
        QVERIFY(progress.grab().save(outputDir + "/update-download" + suffix));
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
        QVERIFY(!appearanceMode->accessibleName().isEmpty());

        for (const auto *name : {
                 "cbLowAudioLatency",
                 "cbAutoEarDetection",
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
        QTRY_VERIFY(!comboPopup->mask().isEmpty());
        QTest::qWait(100);
        QVERIFY(comboPopup->grab().save(outputDir + "/combo-popup-dark.png"));
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
        QTRY_VERIFY(!comboPopup->mask().isEmpty());
        QTest::qWait(100);
        QVERIFY(comboPopup->grab().save(outputDir + "/combo-popup-light.png"));
        appearanceMode->hidePopup();

        theme.SetMode(originalMode);
        QCoreApplication::removeTranslator(&traditionalChinese);
        Gui::Theme::ApplyApplicationTypography(QLocale{"en_US"});
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

QTEST_MAIN(UiRenderingTests)

#include "UiRenderingTests.moc"
