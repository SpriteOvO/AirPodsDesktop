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

#include "DownloadWindow.h"

#include <QPushButton>
#include <QMetaObject>
#include <QMessageBox>

#include <Config.h>
#include "../Application.h"

using namespace std::chrono_literals;

namespace Gui {

DownloadWindow::DownloadWindow(Core::Update::ReleaseInfo info, QWidget *parent)
    : QDialog{parent}, _info{std::move(info)}
{
    _ui.setupUi(this);

    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);

    connect(_ui.pushButtonDownloadManually, &QPushButton::clicked, this, [this]() {
        _info.OpenUrl();
        ApdApplication::QuitSafely();
    });

    connect(this, &DownloadWindow::UpdateProgressSafely, this, &DownloadWindow::UpdateProgress);
    connect(this, &DownloadWindow::OnFailedSafely, this, &DownloadWindow::OnFailed);

    _downloadThread = std::thread{[this]() { DownloadThread(); }};
}

DownloadWindow::~DownloadWindow()
{
    _destroy = true;
    if (_downloadThread.joinable()) {
        _downloadThread.join();
    }
}

void DownloadWindow::UpdateProgress(int downloaded, int total)
{
    if (total == 0) {
        return;
    }
    _ui.progressBar->setValue(downloaded);
    _ui.progressBar->setMaximum(total);
}

void DownloadWindow::OnFailed()
{
    LOG(Warn, "DownloadInstall failed. Popup latest url and quit.");

    QMessageBox::warning(
        this, Config::ProgramName,
        tr("Oops, an error occurred during the automatic update.\n"
           "Please download and install the new version manually."));

    _info.OpenUrl();
    ApdApplication::QuitSafely();
}

void DownloadWindow::DownloadThread()
{
    bool successful = Core::Update::DownloadInstall(_info, [this](size_t downloaded, size_t total) {
        UpdateProgressSafely(downloaded, total);

        if (_destroy) {
            LOG(Warn, "DownloadWindow destructor requests destroy.");
            return false;
        }
        return true;
    });

    if (!successful) {
        OnFailedSafely();
    }
}
} // namespace Gui
