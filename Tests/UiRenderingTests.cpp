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

        theme.SetMode(Gui::Theme::Mode::Light);
        QVERIFY(!theme.IsDark());
        QCOMPARE(theme.Colors().windowBackground, QColor{"#F5F5F7"});
        QCOMPARE(theme.Colors().surface, QColor{"#FFFFFF"});
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(outputDir + "/settings-light.png"));

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
