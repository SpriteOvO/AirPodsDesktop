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

#include <algorithm>
#include <limits>
#include <QPushButton>
#include <QCloseEvent>
#include <QLocale>

#include "Utils.h"
#include "Theme.h"

namespace Gui {

DownloadWindow::DownloadWindow(Core::Update::ReleaseInfo info, QWidget *parent)
    : QDialog{parent}, _info{std::move(info)}
{
    _ui.setupUi(this);

    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    connect(_ui.pushButtonDownloadManually, &QPushButton::clicked, this, [this]() {
        _info.OpenUrl();
        if (_state == State::Downloading) {
            // Preserve the existing manual-download exit path while a transfer is active.
            _result = Outcome::ManualDownload;
            _downloadThread.request_stop();
            QDialog::accept();
        }
    });
    connect(_ui.closeButton, &QPushButton::clicked, this, &DownloadWindow::reject);

    connect(this, &DownloadWindow::UpdateProgressSafely, this, &DownloadWindow::UpdateProgress);
    connect(this, &DownloadWindow::FinishedSafely, this, &DownloadWindow::OnFinished);
    connect(
        &Theme::Manager::Instance(), &Theme::Manager::Changed, this, &DownloadWindow::UpdateText);
    UpdateText();
}

DownloadWindow::~DownloadWindow()
{
    _downloadThread.request_stop();
    if (_downloadThread.joinable()) {
        _downloadThread.join();
    }
}

void DownloadWindow::StartDownload(DownloadFunction download)
{
    if (_state != State::Ready) {
        return;
    }
    _state = State::Downloading;
    UpdateText();
    _downloadThread =
        std::jthread{[this, download = std::move(download)](std::stop_token stopToken) {
            const auto successful =
                download(_info, [this, stopToken](size_t downloaded, size_t total) {
                    if (stopToken.stop_requested()) {
                        return false;
                    }
                    emit UpdateProgressSafely(downloaded, total);
                    return true;
                });
            if (!stopToken.stop_requested()) {
                emit FinishedSafely(successful);
            }
        }};
}

DownloadWindow::Outcome DownloadWindow::Result() const
{
    return _result;
}

void DownloadWindow::UpdateProgress(quint64 downloaded, quint64 total)
{
    if (_state != State::Downloading || _result != Outcome::KeepRunning) {
        return;
    }
    _downloaded = downloaded;
    _total = total;
    UpdateText();
}

void DownloadWindow::OnFinished(bool successful)
{
    if (_state != State::Downloading || _result != Outcome::KeepRunning) {
        return;
    }
    _state = successful ? State::Finished : State::Failed;
    if (successful) {
        _result = Outcome::InstallerStarted;
        accept();
        return;
    }
    LOG(Warn, "DownloadInstall failed. Awaiting user's manual-download or close action.");
    // Keep the native window intact: changing flags here would hide the dialog and end exec().
    // reject()/closeEvent() gate closing while the worker is active.
    UpdateText();
    _ui.closeButton->setFocus();
}

void DownloadWindow::UpdateText()
{
    const auto failed = _state == State::Failed;
    _ui.title->setText(failed ? tr("Unable to update") : tr("Downloading update"));
    _ui.version->setText(tr("New version: %1").arg(_info.version.toString()));
    _ui.closeButton->setVisible(failed);
    _ui.progressBar->setVisible(!failed);
    _ui.progressDetails->setVisible(!failed);
    _ui.hint->setVisible(!failed);
    _ui.pushButtonDownloadManually->setEnabled(_state != State::Finished);
    if (failed) {
        _ui.status->setText(
            tr("The automatic update could not be completed. Please download and install the new "
               "version manually, or close this window to continue using AirPodsDesktop."));
        return;
    }

    const QLocale locale;
    const auto formatBytes = [&locale](quint64 bytes) {
        return locale.formattedDataSize(
            static_cast<qint64>(std::min(bytes, quint64{std::numeric_limits<qint64>::max()})));
    };
    if (_total == 0) {
        _ui.progressBar->setRange(0, 0);
        _ui.status->setText(tr("Waiting for download size..."));
        _ui.progressDetails->setText(tr("%1 downloaded").arg(formatBytes(_downloaded)));
    }
    else {
        const auto ratio =
            std::min(static_cast<double>(_downloaded) / static_cast<double>(_total), 1.0);
        _ui.progressBar->setRange(0, 1000);
        _ui.progressBar->setValue(static_cast<int>(ratio * 1000));
        _ui.status->setText(
            _downloaded >= _total ? tr("Preparing to install...") : tr("Downloading..."));
        _ui.progressDetails->setText(tr("%1% - %2 of %3")
                                         .arg(locale.toString(static_cast<int>(ratio * 100)))
                                         .arg(formatBytes(_downloaded))
                                         .arg(formatBytes(_total)));
    }
    _ui.progressBar->setAccessibleName(_ui.status->text());
    _ui.progressBar->setAccessibleDescription(_ui.progressDetails->text());
}

void DownloadWindow::reject()
{
    if (_state != State::Downloading) {
        QDialog::reject();
    }
}

void DownloadWindow::closeEvent(QCloseEvent *event)
{
    if (_state == State::Downloading) {
        event->ignore();
    }
    else {
        QDialog::closeEvent(event);
    }
}

void DownloadWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        _ui.retranslateUi(this);
        UpdateText();
    }
    QDialog::changeEvent(event);
}
} // namespace Gui
