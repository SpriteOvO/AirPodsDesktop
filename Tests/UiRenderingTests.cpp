#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
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

#include "Source/Core/QuickConnect.h"
#include "Source/Gui/SettingsWindow.h"
#include "Source/Gui/Theme.h"

class UiRenderingTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
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

        const auto applicationFont = Gui::Theme::ApplicationFont();
        QCOMPARE(applicationFont.families().first(), QStringLiteral("Inter"));
        QCOMPARE(Gui::Theme::DisplayFontFamilies().first(), QStringLiteral("Inter Display"));
    }

    void SettingsWindowSupportsKeyboardAndCompactLayout()
    {
        auto backend = std::make_shared<Core::QuickConnect::NullBackend>();
        Core::QuickConnect::Controller quickConnect{backend};

        qApp->setFont(Gui::Theme::ApplicationFont());
        Gui::Theme::Manager::Instance().ApplyToApplication();
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

        const auto outputDir = QStringLiteral(APD_BINARY_DIR "/UiValidation");
        QDir{}.mkpath(outputDir);
        QVERIFY(window.grab().save(outputDir + "/settings-current-theme.png"));

        auto &theme = Gui::Theme::Manager::Instance();
        const auto originalMode = theme.CurrentMode();

        theme.SetMode(Gui::Theme::Mode::Dark);
        QVERIFY(theme.IsDark());
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(outputDir + "/settings-dark.png"));

        theme.SetMode(Gui::Theme::Mode::Light);
        QVERIFY(!theme.IsDark());
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(outputDir + "/settings-light.png"));

        theme.SetMode(originalMode);
        QCoreApplication::removeTranslator(&traditionalChinese);
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
        QVERIFY(QCoreApplication::installTranslator(&first));
        QCoreApplication::processEvents();
        QCOMPARE(description->text(), firstDescription);

        QTranslator second;
        QVERIFY(second.load(QLocale{"de_DE"}, "apd", "_", translationsDir));
        const auto secondDescription = second.translate("QObject", sourceText);
        QVERIFY(!secondDescription.isEmpty());
        QCoreApplication::removeTranslator(&first);
        QVERIFY(QCoreApplication::installTranslator(&second));
        QCoreApplication::processEvents();
        QCOMPARE(description->text(), secondDescription);

        QCoreApplication::removeTranslator(&second);
    }
};

QTEST_MAIN(UiRenderingTests)

#include "UiRenderingTests.moc"
