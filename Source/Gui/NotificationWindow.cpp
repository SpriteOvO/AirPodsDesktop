#include "NotificationWindow.h"

#include <QApplication>
#include <QCursor>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QSpacerItem>
#include <QVBoxLayout>

#include "MainWindow.h"
#include "MainWindowPresentation.h"
#include "Theme.h"
#include "Widget/ActivityIndicator.h"
#include "Widget/CloseButton.h"

namespace Gui {

namespace {

// The close button sits in the top-right corner as on the battery card; the title shares its
// row and stays centred on the card.
constexpr int kTopMargin = 20;
constexpr int kSideMargin = 20;
constexpr int kBottomMargin = 22;
constexpr int kTitleGap = 8;
constexpr qreal kCornerRadius = 24.0;
constexpr int kActivitySize = 16;
constexpr int kActivityGap = 6;

int FirstLineHeight(const QLabel *label)
{
    // The laid-out line, not the primary font alone: CJK text uses a taller fallback font.
    return label->fontMetrics()
        .boundingRect(
            QRect{0, 0, QWIDGETSIZE_MAX, QWIDGETSIZE_MAX}, Qt::TextSingleLine, label->text())
        .height();
}

} // namespace

NotificationWindow::NotificationWindow(QWidget *parent)
    : QWidget{
          parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                      Qt::WindowDoesNotAcceptFocus},
      _title{new QLabel{this}}, _message{new QLabel{this}},
      _activity{new Widget::ActivityIndicator{kActivitySize, _message}},
      _close{new Widget::CloseButton{this}},
      _titleOffset{new QSpacerItem{0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed}},
      _closeOffset{new QSpacerItem{0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed}}
{
    setObjectName("connectionNotification");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setProperty(Theme::Manager::kSkipDwmProperty, true);
    setFixedWidth(320);

    _title->setObjectName("notificationTitle");
    _message->setObjectName("notificationMessage");
    for (auto *label : {_title, _message}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignCenter);
    }
    auto titleFont = _title->font();
    titleFont.setPointSize(15);
    titleFont.setWeight(QFont::DemiBold);
    _title->setFont(titleFont);
    _title->setProperty("fontRole", "display");
    _title->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    _close->setObjectName("notificationClose");
    connect(_close, &Widget::CloseButton::clicked, this, &QWidget::hide);

    _activity->setObjectName("notificationActivity");
    _activity->hide();
    // The indicator is placed by hand beside the centred text, so follow the label's size.
    _message->installEventFilter(this);

    auto *titleColumn = new QVBoxLayout;
    titleColumn->setSpacing(0);
    titleColumn->addItem(_titleOffset);
    titleColumn->addWidget(_title);
    titleColumn->addStretch();
    auto *closeColumn = new QVBoxLayout;
    closeColumn->setSpacing(0);
    closeColumn->addItem(_closeOffset);
    closeColumn->addWidget(_close);
    closeColumn->addStretch();

    // Mirrors the close button and its gap, so the title is centred on the card rather than
    // beside the button. A box layout adds no spacing next to a spacer, so the gap is explicit.
    auto *header = new QHBoxLayout;
    header->setSpacing(0);
    header->addSpacing(Widget::CloseButton::kSize + kTitleGap);
    header->addLayout(titleColumn, 1);
    header->addSpacing(kTitleGap);
    header->addLayout(closeColumn);

    auto *layout = new QVBoxLayout{this};
    layout->setContentsMargins(kSideMargin, kTopMargin, kSideMargin, kBottomMargin);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addSpacing(6);
    layout->addWidget(_message);
    _autoHide.setSingleShot(true);
    connect(&_autoHide, &QTimer::timeout, this, &QWidget::hide);
    _placementTimer.setInterval(100);
    connect(&_placementTimer, &QTimer::timeout, this, &NotificationWindow::Place);
    connect(
        &Theme::Manager::Instance(), &Theme::Manager::Changed, this,
        &NotificationWindow::ApplyTheme);
    ApplyTheme();
}

void NotificationWindow::Show(
    const QString &title, const QString &message, bool warning, bool pending,
    const QRect &trayGeometry)
{
    _autoHide.stop();
    // Resolve the cursor fallback once, so overflow-tray notifications do not follow the mouse.
    if (!trayGeometry.isEmpty() || !isVisible() || _targetScreen == nullptr) {
        _targetScreen = QGuiApplication::screenAt(
            trayGeometry.isEmpty() ? QCursor::pos() : trayGeometry.center());
    }
    if (_targetScreen == nullptr) {
        _targetScreen = screen();
    }
    _warning = warning;
    _title->setText(title);
    _message->setText(message);
    // Room for the indicator on the left keeps the indicator and the text centred as a pair.
    _message->setContentsMargins(pending ? kActivitySize + kActivityGap : 0, 0, 0, 0);
    _activity->setVisible(pending);
    ApplyTheme();
    AlignCloseWithTitle();
    layout()->activate();
    // Height for the fixed width: the plain size hint guesses the wrapped labels' line count,
    // and any surplus would open a gap under the title.
    resize(width(), hasHeightForWidth() ? heightForWidth(width()) : sizeHint().height());
    layout()->activate();
    PlaceActivity();
    Place();
    show();
    _placementTimer.start();
    if (!pending) {
        _autoHide.start(warning ? 8000 : 3500);
    }
}

void NotificationWindow::Place()
{
    auto *targetScreen = _targetScreen.data();
    if (targetScreen == nullptr) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    const auto available = targetScreen->availableGeometry();
    auto target = PopupPosition(available, size(), {24, 24});
    QRect card{target, size()};
    for (auto *window : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(window) == nullptr || !window->isVisible()) {
            continue;
        }
        const auto battery = window->frameGeometry();
        // Also avoid the battery card's destination while its slide-in is running.
        const auto batteryTarget = PopupPosition(available, window->size(), {24, 24});
        if (window->screen() != targetScreen ||
            (!card.intersects(battery) && !card.intersects(QRect{batteryTarget, window->size()})))
        {
            continue;
        }
        target.setY(batteryTarget.y() - height() - 12);
        if (target.y() < available.top()) {
            target =
                QPoint{batteryTarget.x() - width() - 12, available.bottom() + 1 - height() - 24};
        }
        target.setX(qMax(available.left(), target.x()));
        target.setY(qMax(available.top(), target.y()));
        break;
    }
    if (pos() != target) {
        move(target);
    }
}

void NotificationWindow::ApplyTheme()
{
    const auto &colors = Theme::Manager::Instance().Colors();
    _title->setStyleSheet(
        QString{"color: %1; background: transparent;"}.arg(colors.mainText.name()));
    _message->setStyleSheet(
        QString{"color: %1; background: transparent;"}.arg(
            (_warning ? colors.errorText : colors.mainTextSecondary).name()));
    _activity->SetColor(colors.mainTextSecondary);
    _close->update();
    update();
}

void NotificationWindow::AlignCloseWithTitle()
{
    const int offset = FirstLineHeight(_title) - Widget::CloseButton::kSize;
    _titleOffset->changeSize(0, qMax(0, -offset) / 2, QSizePolicy::Minimum, QSizePolicy::Fixed);
    _closeOffset->changeSize(0, qMax(0, offset) / 2, QSizePolicy::Minimum, QSizePolicy::Fixed);
    layout()->invalidate();
}

void NotificationWindow::PlaceActivity()
{
    if (_activity->isHidden()) {
        return;
    }
    const auto text = _message->fontMetrics().boundingRect(
        _message->contentsRect(), _message->alignment() | Qt::TextWordWrap, _message->text());
    _activity->move(
        text.left() - kActivityGap - kActivitySize,
        text.top() + (FirstLineHeight(_message) - kActivitySize) / 2);
}

bool NotificationWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == _message && event->type() == QEvent::Resize) {
        PlaceActivity();
    }
    return QWidget::eventFilter(watched, event);
}

void NotificationWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::Manager::Instance().Colors().mainSurface);
    painter.drawRoundedRect(QRectF{rect()}, kCornerRadius, kCornerRadius);
}

void NotificationWindow::hideEvent(QHideEvent *event)
{
    _autoHide.stop();
    _placementTimer.stop();
    QWidget::hideEvent(event);
}

} // namespace Gui
