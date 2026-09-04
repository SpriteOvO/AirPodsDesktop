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

#include <thread>
#include <QDialog>

#include "../Core/Update.h"
#include "ui_UpdateWindow.h"

namespace Gui {

class UpdateWindow : public QDialog
{
    Q_OBJECT

public:
    enum class Action { Update, Skip, Later };
    enum class Outcome { KeepRunning, InstallerStarted, ManualDownload };
    using DownloadFunction =
        std::function<bool(const Core::Update::ReleaseInfo &, const Core::Update::FnProgress &)>;

    explicit UpdateWindow(
        Core::Update::ReleaseInfo info, QWidget *parent = nullptr,
        DownloadFunction download = Core::Update::DownloadInstall);
    ~UpdateWindow() override;
    void StartDownload();
    Action SelectedAction() const;
    Outcome Result() const;

Q_SIGNALS:
    void DownloadStarted();
    void UpdateProgressSafely(quint64 downloaded, quint64 total);
    void FinishedSafely(bool successful);

protected:
    void changeEvent(QEvent *event) override;
    void reject() override;
    void closeEvent(QCloseEvent *event) override;

private:
    Ui::UpdateWindow _ui;
    Core::Update::ReleaseInfo _info;
    DownloadFunction _download;
    std::jthread _downloadThread;
    enum class State { Prompt, Downloading, Failed, Finished };
    State _state{State::Prompt};
    Action _action{Action::Later};
    Outcome _result{Outcome::KeepRunning};
    quint64 _downloaded{0}, _total{0};

    void UpdateText();
    void UpdateProgress(quint64 downloaded, quint64 total);
    void OnFinished(bool successful);
    void DownloadManually();
    void FitContent();
};

} // namespace Gui
