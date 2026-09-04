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

#include "Source/Gui/DownloadWindow.h"
#include "Source/Gui/Theme.h"
#include "Source/Gui/UpdateWindow.h"

namespace {

enum class Command { Running, Fail, Succeed };

Core::Update::ReleaseInfo ReleaseFixture()
{
    Core::Update::ReleaseInfo info;
    info.version = QVersionNumber{99, 0, 0};
    info.url = QStringLiteral("https://example.invalid/simulated-release");
    info.changeLog = QStringLiteral("這是一筆本機模擬更新，不會下載檔案或安裝軟體。\n\n"
                                    "• 統一更新視窗與設定視窗的外觀。\n"
                                    "• 改善下載進度與失敗提示。\n\n"
                                    "請點「立即更新」，再從左側控制面板按「模擬失敗」。");
    return info;
}

Gui::DownloadWindow::DownloadFunction SlowDownload(std::shared_ptr<std::atomic<Command>> command)
{
    return [command](const auto &, const Core::Update::FnProgress &progress) {
        constexpr size_t total = 64 * 1024 * 1024;
        size_t downloaded = 0;
        int ticks = 0;
        for (;;) {
            // The real window's stop token is checked by this callback on every tick.
            if (!progress(downloaded, ticks < 10 ? 0 : total)) {
                return false;
            }
            switch (command->load()) {
            case Command::Fail:
                return false;
            case Command::Succeed:
                if (!progress(total, total)) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
                return true;
            case Command::Running:
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
            if (++ticks >= 10) {
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
    UpdateSimulator()
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
        delete _download.data();
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
    QPointer<Gui::DownloadWindow> _download;
    std::shared_ptr<std::atomic<Command>> _command;

    void EnableOutcomeButtons(bool enabled)
    {
        _fail->setEnabled(enabled);
        _succeed->setEnabled(enabled);
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
        if (_prompt || _download) {
            return;
        }
        _start->setEnabled(false);
        _status->setText(QStringLiteral("請在右側更新視窗點「立即更新」。"));
        auto *prompt = new Gui::UpdateWindow{ReleaseFixture(), this};
        _prompt = prompt;
        connect(prompt, &QDialog::finished, this, [this, prompt] {
            const auto action = prompt->SelectedAction();
            if (action == Gui::UpdateWindow::Action::Update) {
                ShowDownload();
            }
            else {
                _start->setEnabled(true);
                _status->setText(
                    action == Gui::UpdateWindow::Action::Skip
                        ? QStringLiteral("已模擬略過版本；沒有寫入正式設定。")
                        : QStringLiteral("已關閉提示。可再次開啟更新視窗。"));
            }
        });
        ShowBesidePanel(prompt);
    }

    void ShowDownload()
    {
        _command = std::make_shared<std::atomic<Command>>(Command::Running);
        auto *download = new Gui::DownloadWindow{ReleaseFixture(), this};
        _download = download;
        connect(download, &Gui::DownloadWindow::FinishedSafely, this, [this](bool successful) {
            EnableOutcomeButtons(false);
            if (!successful) {
                _status->setText(
                    QStringLiteral("已模擬失敗。請在右側選擇手動下載或關閉；主程式繼續執行。"));
            }
        });
        connect(download, &QDialog::finished, this, [this, download] {
            EnableOutcomeButtons(false);
            _start->setEnabled(true);
            switch (download->Result()) {
            case Gui::DownloadWindow::Outcome::InstallerStarted:
                _status->setText(
                    QStringLiteral("模擬成功：沒有啟動安裝程式。可再次開啟更新視窗。"));
                break;
            case Gui::DownloadWindow::Outcome::ManualDownload:
                _status->setText(
                    QStringLiteral("已模擬切換手動下載。正式流程會退出，測試工具保持開啟。"));
                break;
            case Gui::DownloadWindow::Outcome::KeepRunning:
                _status->setText(QStringLiteral("失敗視窗已關閉，程式繼續執行。可再次測試。"));
                break;
            }
        });
        _status->setText(QStringLiteral("正在模擬慢速下載。隨時按「模擬失敗」或「模擬成功」。"));
        EnableOutcomeButtons(true);
        download->StartDownload(SlowDownload(_command));
        ShowBesidePanel(download);
    }
};

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
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
    UpdateSimulator simulator;
    simulator.show();
    return app.exec();
}

#include "UpdateSimulator.moc"
