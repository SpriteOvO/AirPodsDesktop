#include <atomic>
#include <memory>
#include <thread>

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QFrame>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QTranslator>
#include <QUrl>
#include <QVBoxLayout>

#include "Source/Gui/Theme.h"
#include "Source/Gui/UpdateWindow.h"

namespace {

enum class Command { Running, Waiting, Preparing, Fail, Succeed };

Core::Update::ReleaseInfo ReleaseFixture()
{
    Core::Update::ReleaseInfo info;
    info.version = QVersionNumber{2, 12, 345};
    info.fileName = "simulated.exe";
    info.downloadUrl = "https://example.invalid/simulated.exe";
    info.fileSize = 64 * 1024 * 1024;
    info.sha256 = QString(64, QChar{'0'});
    info.url = QStringLiteral("https://example.invalid/simulated-release");
    info.changeLog = QStringLiteral("這是一筆本機模擬更新，不會下載檔案或安裝軟體。\n\n"
                                    "• 統一更新視窗與設定視窗的外觀。\n"
                                    "• 改善下載進度與失敗提示。\n\n"
                                    "請點「立即更新」，再從左側控制面板按「模擬失敗」。");
    return info;
}

Gui::UpdateWindow::DownloadFunction SlowDownload(std::shared_ptr<std::atomic<Command>> command)
{
    return [command](const auto &, const Core::Update::FnProgress &progress) {
        constexpr size_t total = 64 * 1024 * 1024;
        size_t downloaded = 0;
        int ticks = 0;
        for (;;) {
            // The real window's stop token is checked by this callback on every tick.
            const auto current = command->load();
            if (!progress(
                    current == Command::Preparing ? total : downloaded,
                    current == Command::Waiting || ticks < 10 ? 0 : total))
            {
                return false;
            }
            switch (current) {
            case Command::Fail:
                return false;
            case Command::Succeed:
                if (!progress(total, total)) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
                return true;
            case Command::Waiting:
            case Command::Preparing:
            case Command::Running:
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
            if (++ticks >= 10 && current == Command::Running) {
                downloaded = (std::min)(downloaded + total / 1000, total * 95 / 100);
            }
        }
    };
}

} // namespace

class UpdateSimulator : public QWidget
{
    Q_OBJECT

public:
    explicit UpdateSimulator(QTranslator &translator)
    {
        setWindowTitle(QStringLiteral("AirPodsDesktop 更新互動測試"));
        resize(430, 410);
        auto *layout = new QVBoxLayout{this};
        layout->setContentsMargins(24, 20, 24, 20);
        layout->setSpacing(12);
        auto *title = new QLabel{QStringLiteral("更新互動測試")};
        title->setProperty("cssClass", "pageTitle");
        title->setProperty("fontRole", "display");
        layout->addWidget(title);
        auto *description = new QLabel{
            QStringLiteral("1. 開啟更新視窗，親自點「立即更新」。\n"
                           "2. 觀看慢速下載，再按「模擬失敗」。\n"
                           "3. 在失敗視窗選擇手動下載或關閉。\n\n"
                           "下載約每秒 1%，到 95% 等待操作。所有下載、安裝與開啟網頁都是模擬。")};
        description->setWordWrap(true);
        description->setProperty("cssClass", "cardDescription");
        layout->addWidget(description);

        auto *appearance = new QComboBox;
        appearance->setAccessibleName(QStringLiteral("測試視窗外觀"));
        appearance->addItems(
            {QStringLiteral("淺色"), QStringLiteral("深色"), QStringLiteral("隨系統")});
        layout->addWidget(appearance);
        connect(
            appearance, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [](int index) {
                const Gui::Theme::Mode modes[]{
                    Gui::Theme::Mode::Light, Gui::Theme::Mode::Dark, Gui::Theme::Mode::System};
                Gui::Theme::Manager::Instance().SetMode(modes[index]);
            });

        auto *language = new QComboBox;
        language->setAccessibleName(QStringLiteral("測試語言"));
        language->addItems({QStringLiteral("繁體中文"), QStringLiteral("English")});
        layout->addWidget(language);
        connect(
            language, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [&translator](int index) {
                qApp->removeTranslator(&translator);
                if (index == 0) {
                    qApp->installTranslator(&translator);
                }
                const QLocale locale{index == 0 ? "zh_TW" : "en_US"};
                QLocale::setDefault(locale);
                Gui::Theme::ApplyApplicationTypography(locale);
            });
        _scenario = new QComboBox;
        _scenario->setAccessibleName(QStringLiteral("發行情境"));
        _scenario->addItems(
            {QStringLiteral("正式版本"), QStringLiteral("預覽版本"), QStringLiteral("無更新說明")});
        layout->addWidget(_scenario);
        _progressState = new QComboBox;
        _progressState->setAccessibleName(QStringLiteral("模擬進度狀態"));
        _progressState->addItems(
            {QStringLiteral("慢速下載"), QStringLiteral("等待下載大小"),
             QStringLiteral("停留在 100%／準備安裝")});
        layout->addWidget(_progressState);
        connect(
            _progressState, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (_command) {
                    const Command commands[]{
                        Command::Running, Command::Waiting, Command::Preparing};
                    *_command = commands[index];
                }
            });

        _start = new QPushButton{QStringLiteral("開啟更新視窗")};
        _start->setProperty("cssClass", "accent");
        _fail = new QPushButton{QStringLiteral("模擬失敗")};
        _succeed = new QPushButton{QStringLiteral("模擬成功")};
        layout->addWidget(_start);
        layout->addWidget(_fail);
        layout->addWidget(_succeed);
        EnableOutcomeButtons(false);
        auto *card = new QFrame;
        card->setProperty("cssClass", "settingCard");
        auto *cardLayout = new QVBoxLayout{card};
        cardLayout->setContentsMargins(16, 16, 16, 16);
        _status = new QLabel{QStringLiteral("準備就緒。主程式與正式設定不受影響。")};
        _status->setWordWrap(true);
        cardLayout->addWidget(_status);
        layout->addWidget(card);
        layout->addStretch();

        connect(_start, &QPushButton::clicked, this, &UpdateSimulator::ShowPrompt);
        connect(_fail, &QPushButton::clicked, this, [this] {
            *_command = Command::Fail;
            EnableOutcomeButtons(false);
        });
        connect(_succeed, &QPushButton::clicked, this, [this] {
            *_command = Command::Succeed;
            EnableOutcomeButtons(false);
        });
        QDesktopServices::setUrlHandler("https", this, "InterceptUrl");
    }

    ~UpdateSimulator() override
    {
        // Join the simulated worker before the panel and its controls are destroyed.
        delete _prompt.data();
        QDesktopServices::unsetUrlHandler("https");
    }

private Q_SLOTS:
    void InterceptUrl(const QUrl &)
    {
        _status->setText(QStringLiteral("已攔截發行頁操作：不會真的開啟瀏覽器或下載檔案。"));
    }

private:
    QPushButton *_start{}, *_fail{}, *_succeed{};
    QLabel *_status{};
    QPointer<Gui::UpdateWindow> _prompt;
    QComboBox *_scenario{}, *_progressState{};
    std::shared_ptr<std::atomic<Command>> _command;

    void EnableOutcomeButtons(bool enabled)
    {
        _fail->setEnabled(enabled);
        _succeed->setEnabled(enabled);
        _progressState->setEnabled(enabled);
    }

    void ShowBesidePanel(QDialog *dialog)
    {
        // Modeless only in this harness, so the separate failure controls remain clickable.
        dialog->setWindowModality(Qt::NonModal);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        const auto available = screen()->availableGeometry();
        const int combinedWidth = width() + 16 + dialog->width();
        if (combinedWidth <= available.width()) {
            move(
                available.left() + (available.width() - combinedWidth) / 2,
                available.top() + (available.height() - height()) / 2);
            dialog->move(frameGeometry().right() + 16, y());
        }
        dialog->show();
    }

    void ShowPrompt()
    {
        if (_prompt) {
            return;
        }
        _start->setEnabled(false);
        _scenario->setEnabled(false);
        _progressState->setCurrentIndex(0);
        _command = std::make_shared<std::atomic<Command>>(Command::Running);
        auto info = ReleaseFixture();
        info.isPreRelease = _scenario->currentIndex() == 1;
        if (_scenario->currentIndex() == 2) {
            info.changeLog.clear();
        }
        auto *prompt = new Gui::UpdateWindow{info, this, SlowDownload(_command)};
        _prompt = prompt;
        _status->setText(QStringLiteral("請在右側更新視窗點「立即更新」。"));
        connect(prompt, &Gui::UpdateWindow::DownloadStarted, this, [this] {
            EnableOutcomeButtons(true);
            _status->setText(QStringLiteral("正在原視窗模擬慢速下載，可切換狀態或手動觸發失敗。"));
        });
        connect(prompt, &Gui::UpdateWindow::FinishedSafely, this, [this](bool successful) {
            EnableOutcomeButtons(false);
            if (!successful) {
                _status->setText(
                    QStringLiteral("已模擬失敗。選擇手動下載或關閉；主程式繼續執行。"));
            }
        });
        connect(prompt, &QDialog::finished, this, [this, prompt] {
            EnableOutcomeButtons(false);
            _start->setEnabled(true);
            _scenario->setEnabled(true);
            switch (prompt->Result()) {
            case Gui::UpdateWindow::Outcome::InstallerStarted:
                _status->setText(
                    QStringLiteral("模擬成功：沒有啟動安裝程式。可再次開啟更新視窗。"));
                break;
            case Gui::UpdateWindow::Outcome::ManualDownload:
                _status->setText(
                    QStringLiteral("已模擬切換手動下載。正式流程會退出，測試工具保持開啟。"));
                break;
            case Gui::UpdateWindow::Outcome::KeepRunning:
                _status->setText(
                    prompt->SelectedAction() == Gui::UpdateWindow::Action::Skip
                        ? QStringLiteral("已模擬略過版本；沒有寫入正式設定。")
                        : QStringLiteral("視窗已關閉，程式繼續執行。可再次測試。"));
                break;
            }
        });
        ShowBesidePanel(prompt);
    }
};

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app{argc, argv};
    QTranslator translator;
    if (!translator.load(
            QLocale{"zh_TW"}, "apd", "_", QCoreApplication::applicationDirPath() + "/translations"))
    {
        return 1;
    }
    app.installTranslator(&translator);
    Gui::Theme::ApplyApplicationTypography(QLocale{"zh_TW"});
    Gui::Theme::Manager::Instance().SetMode(Gui::Theme::Mode::Light);
    QLocale::setDefault(QLocale{"zh_TW"});
    UpdateSimulator simulator{translator};
    simulator.show();
    return app.exec();
}

#include "UpdateSimulator.moc"
