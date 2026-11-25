#pragma once

#include <QWidget>
#include <QTimer>
#include <QRect>
#include <QPointer>

class QLabel;
class QScreen;
class QSpacerItem;

namespace Gui {

namespace Widget {
class ActivityIndicator;
class CloseButton;
} // namespace Widget

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
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QLabel *_title;
    QLabel *_message;
    // Spins before the message's first line while a connection is pending, as on iOS.
    Widget::ActivityIndicator *_activity;
    Widget::CloseButton *_close;
    // Push the title or the close button down so both share the first line's centre.
    QSpacerItem *_titleOffset;
    QSpacerItem *_closeOffset;
    QTimer _autoHide{this};
    QTimer _placementTimer{this};
    QPointer<QScreen> _targetScreen;
    bool _warning{false};

    void ApplyTheme();
    void AlignCloseWithTitle();
    void PlaceActivity();
    void Place();
};

} // namespace Gui
