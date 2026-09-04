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

#include "UpdateWindow.h"

#include <algorithm>
#include <limits>
#include <QCloseEvent>
#include <QIcon>
#include <QLocale>
#include <QPushButton>
#include <QTextCursor>
#include <QTextBlockFormat>

#include "Theme.h"
#include "Utils.h"

namespace Gui {

UpdateWindow::UpdateWindow(
    Core::Update::ReleaseInfo info, QWidget *parent, DownloadFunction download)
    : QDialog{parent}, _info{std::move(info)}, _download{std::move(download)}
{
    _ui.setupUi(this);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setFixedWidth(560);
    _ui.promptTextLayout->setAlignment(Qt::AlignVCenter);
    _ui.failedTextLayout->setAlignment(Qt::AlignVCenter);
    for (auto *label : {_ui.promptTitle, _ui.downloadTitle, _ui.title}) {
        label->setMinimumHeight(20);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    }
    for (auto *label :
         {_ui.rowSubtitle, _ui.rowError, _ui.status, _ui.progressDetails, _ui.valueLabel,
          _ui.valueText, _ui.notesTitle, _ui.emptyNotesLabel, _ui.emptyNotesText})
    {
        label->setMinimumHeight(18);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    }
    _ui.status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Maximum);
    _ui.progressDetails->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Maximum);
    _ui.statusLayout->setStretch(0, 1);
    _ui.releaseNotes->setFixedHeight(96);
    _ui.releaseNotes->setPlainText(_info.changeLog);
    _ui.releaseNotes->document()->setDocumentMargin(0);
    QTextCursor cursor{_ui.releaseNotes->document()};
    cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(19.333, QTextBlockFormat::FixedHeight);
    cursor.mergeBlockFormat(format);
    for (auto *icon : {_ui.promptIcon, _ui.downloadIcon, _ui.failedIcon}) {
        icon->setPixmap(QIcon{":/Resource/Image/Icon.svg"}.pixmap(QSize{40, 40}));
    }
    connect(_ui.updateButton, &QPushButton::clicked, this, &UpdateWindow::StartDownload);
    connect(_ui.skipButton, &QPushButton::clicked, this, [this] {
        _action = Action::Skip;
        accept();
    });
    connect(_ui.laterButton, &QPushButton::clicked, this, &UpdateWindow::reject);
    connect(_ui.closeButton, &QPushButton::clicked, this, &UpdateWindow::reject);
    connect(_ui.viewButton, &QPushButton::clicked, this, [this] { _info.OpenUrl(); });
    connect(
        _ui.pushButtonDownloadManually, &QPushButton::clicked, this,
        &UpdateWindow::DownloadManually);
    connect(_ui.failedManualButton, &QPushButton::clicked, this, &UpdateWindow::DownloadManually);
    connect(this, &UpdateWindow::UpdateProgressSafely, this, &UpdateWindow::UpdateProgress);
    connect(this, &UpdateWindow::FinishedSafely, this, &UpdateWindow::OnFinished);
    connect(&Theme::Manager::Instance(), &Theme::Manager::Changed, this, &UpdateWindow::UpdateText);
    UpdateText();
    _ui.updateButton->setFocus();
}

UpdateWindow::~UpdateWindow()
{
    _downloadThread.request_stop();
    if (_downloadThread.joinable()) {
        _downloadThread.join();
    }
}

void UpdateWindow::StartDownload()
{
    if (_state != State::Prompt) {
        return;
    }
    _action = Action::Update;
    if (!_info.CanAutoUpdate()) {
        DownloadManually();
        return;
    }
    _state = State::Downloading;
    UpdateText();
    _ui.pushButtonDownloadManually->setFocus();
    emit DownloadStarted();
    _downloadThread = std::jthread{[this](std::stop_token stopToken) {
        const auto successful =
            _download(_info, [this, stopToken](size_t downloaded, size_t total) {
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

UpdateWindow::Action UpdateWindow::SelectedAction() const
{
    return _action;
}

UpdateWindow::Outcome UpdateWindow::Result() const
{
    return _result;
}

void UpdateWindow::DownloadManually()
{
    _info.OpenUrl();
    if (_state != State::Failed) {
        _result = Outcome::ManualDownload;
        _downloadThread.request_stop();
        QDialog::accept();
    }
}

void UpdateWindow::UpdateProgress(quint64 downloaded, quint64 total)
{
    if (_state != State::Downloading || _result != Outcome::KeepRunning) {
        return;
    }
    _downloaded = downloaded;
    _total = total;
    UpdateText();
}

void UpdateWindow::OnFinished(bool successful)
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
    // Keep the native window and its modal loop intact throughout every state change.
    UpdateText();
    _ui.closeButton->setFocus();
}

void UpdateWindow::UpdateText()
{
    const bool prompt = _state == State::Prompt;
    const bool failed = _state == State::Failed;
    const bool downloading = _state == State::Downloading;
    const bool preparing = downloading && _total != 0 && _downloaded >= _total;
    const bool emptyNotes = _info.changeLog.isEmpty();
    _ui.headerStack->setCurrentWidget(
        prompt   ? _ui.promptHeader
        : failed ? _ui.failedHeader
                 : _ui.downloadHeader);
    _ui.promptTitle->setText(QStringLiteral("AirPodsDesktop %1").arg(_info.version.toString()));
    _ui.rowSubtitle->setText(
        tr("Ready to install. You are using %1.").arg(Core::Update::GetLocalVersion().toString()));
    _ui.downloadTitle->setText(tr("Downloading update"));
    _ui.title->setText(tr("Unable to update"));
    _ui.rowError->setText(tr("The automatic update was stopped"));
    _ui.valueLabel->setText(prompt ? tr("Release type") : tr("New version"));
    _ui.valueText->setText(
        prompt ? (_info.isPreRelease ? tr("Preview release") : tr("Stable release"))
               : _info.version.toString());
    _ui.emptyNotesLabel->setText(_ui.notesTitle->text());
    _ui.emptyNotesText->setText(tr("No release notes for this version"));
    _ui.changeLogSection->setVisible(!failed && !emptyNotes);
    _ui.noChangeLogRow->setVisible(!failed && emptyNotes);
    _ui.failureRow->setVisible(failed);
    _ui.failureDescription->setText(
        tr("The automatic update could not be completed. Please download and install the new "
           "version manually, or close this window to continue using AirPodsDesktop."));
    _ui.footerRow->setVisible(!downloading);
    _ui.footerDivider->setVisible(!downloading);
    _ui.skipButton->setVisible(prompt);
    _ui.laterButton->setVisible(prompt);
    _ui.closeButton->setVisible(failed);
    _ui.hint->setVisible(downloading && !preparing);
    _ui.pushButtonDownloadManually->setEnabled(downloading && !preparing);
    _ui.releaseNotes->setAccessibleName(_ui.notesTitle->text());

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
        _ui.status->setText(preparing ? tr("Preparing to install...") : tr("Downloading..."));
        _ui.progressDetails->setText(tr("%1% - %2 of %3")
                                         .arg(locale.toString(static_cast<int>(ratio * 100)))
                                         .arg(formatBytes(_downloaded))
                                         .arg(formatBytes(_total)));
    }
    _ui.progressBar->setAccessibleName(_ui.status->text());
    _ui.progressBar->setAccessibleDescription(_ui.progressDetails->text());
    _ui.status->setAccessibleName(_ui.status->text());
    _ui.status->setToolTip(_ui.status->text());
    FitContent();
}

void UpdateWindow::FitContent()
{
    // A shared stack height keeps the version and release-notes rows stationary when
    // downloading starts, including translations and long version numbers.
    for (auto *button : findChildren<QPushButton *>()) {
        button->ensurePolished();
        button->setFixedSize(button->sizeHint().width(), 28);
    }
    _ui.headerStack->setMinimumHeight(0);
    _ui.headerStack->setMaximumHeight(QWIDGETSIZE_MAX);
    _ui.rootLayout->activate();
    const auto headerWidth = width() - 48 - 2;
    int headerHeight = 0;
    for (auto *page : {_ui.promptHeader, _ui.downloadHeader, _ui.failedHeader}) {
        const auto *pageLayout = page->layout();
        headerHeight = std::max(headerHeight, pageLayout->totalHeightForWidth(headerWidth));
        headerHeight = std::max(headerHeight, pageLayout->totalMinimumSize().height());
    }
    _ui.headerStack->setFixedHeight(headerHeight);
    _ui.rootLayout->invalidate();
    _ui.rootLayout->activate();
    setFixedHeight(_ui.rootLayout->totalHeightForWidth(width()));
    // Keep the numeric progress intact at the specified 560px width. Longer status
    // translations use an ellipsis; the full message remains available to assistive
    // technology and in a tooltip.
    _ui.status->setText(_ui.status->fontMetrics().elidedText(
        _ui.status->accessibleName(), Qt::ElideRight, _ui.status->width()));
}

void UpdateWindow::reject()
{
    if (_state != State::Downloading) {
        QDialog::reject();
    }
}

void UpdateWindow::closeEvent(QCloseEvent *event)
{
    if (_state == State::Downloading) {
        event->ignore();
    }
    else {
        QDialog::closeEvent(event);
    }
}

void UpdateWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        _ui.retranslateUi(this);
        UpdateText();
    }
    QDialog::changeEvent(event);
}

} // namespace Gui
