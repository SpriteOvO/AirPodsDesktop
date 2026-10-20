#pragma once

#include <QWidget>
#include <QTimer>
#include <QRect>
#include <QPointer>

class QLabel;
class QProgressBar;
class QPushButton;
class QScreen;

namespace Gui {

// A single non-activating card whose content is replaced as a connection progresses.
class NotificationWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NotificationWindow(QWidget *parent = nullptr);
    void Show(
        const QString &title, const QString &message, bool warning, bool pending,
        const QRect &trayGeometry);

protected:
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QLabel *_title;
    QLabel *_message;
    QProgressBar *_progress;
    QPushButton *_close;
    QTimer _autoHide{this};
    QTimer _placementTimer{this};
    QPointer<QScreen> _targetScreen;
    bool _warning{false};

    void ApplyTheme();
    void Place();
};

} // namespace Gui
