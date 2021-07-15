//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021 SpriteOvO
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
#include <atomic>
#include <QDialog>

#include "../Core/Update.h"
#include "../Utils.h"
#include "ui_DownloadWindow.h"

namespace Gui {

class DownloadWindow : public QDialog {
    Q_OBJECT

public:
    DownloadWindow(const Core::Update::Info &info, QWidget *parent = nullptr);
    ~DownloadWindow();

public Q_SLOTS:
    void UpdateProgress(int downloaded, int total);
    void OnFailed();

private:
    Ui::DownloadWindow _ui;

    Core::Update::Info _info;
    std::atomic<bool> _destroy{false};
    std::thread _downloadThread;

    UTILS_QT_DISABLE_ESC_QUIT(DownloadWindow, QDialog);
};
} // namespace Gui
