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

#include "Theme.h"

#include <array>
#include <cmath>
#include <optional>

#include <QAbstractNativeEventFilter>
#include <QPointer>
#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QMenu>
#include <QSettings>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include "../Logger.h"

#if defined APD_OS_WIN
    #include "../Core/OS/Windows.h"
    #include <dwmapi.h>
    #include <winrt/Windows.UI.ViewManagement.h>
#endif

namespace Gui::Theme {

namespace {

#if defined APD_OS_WIN
constexpr DWORD kDwmwaUseImmersiveDarkModeBefore20H1 = 19;
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmwaWindowCornerPreference = 33;
constexpr DWORD kDwmwcpRoundSmall = 3;
#endif

constexpr auto kDefaultAccent = "#0067C0";

const QStringList &BodyFontFamilies()
{
    static const QStringList families{
        "Inter",        "Noto Sans TC",          "Segoe UI Variable Text",
        "Segoe UI",     "Microsoft JhengHei UI", "Microsoft YaHei UI",
        "Yu Gothic UI", "Malgun Gothic",
    };
    return families;
}

struct SystemTheme {
    bool appsDark{false};
    bool systemDark{false};
    QColor accent;
    std::optional<QColor> accentForLight, accentForDark;

    bool operator==(const SystemTheme &) const = default;
};

QColor Mix(const QColor &a, const QColor &b, qreal amount)
{
    return QColor::fromRgbF(
        a.redF() + (b.redF() - a.redF()) * amount, a.greenF() + (b.greenF() - a.greenF()) * amount,
        a.blueF() + (b.blueF() - a.blueF()) * amount);
}

qreal LinearColorChannel(qreal channel)
{
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

qreal RelativeLuminance(const QColor &color)
{
    return 0.2126 * LinearColorChannel(color.redF()) + 0.7152 * LinearColorChannel(color.greenF()) +
           0.0722 * LinearColorChannel(color.blueF());
}

QColor AccessibleForeground(const QColor &background)
{
    // WCAG contrast ratio against black/white. One of the two is always at least 4.58:1.
    const auto luminance = RelativeLuminance(background);
    const auto blackContrast = (luminance + 0.05) / 0.05;
    const auto whiteContrast = 1.05 / (luminance + 0.05);
    return blackContrast >= whiteContrast ? QColor{Qt::black} : QColor{Qt::white};
}

SystemTheme ReadSystemTheme()
{
    SystemTheme result;

#if defined APD_OS_WIN
    QSettings personalize{
        R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
        QSettings::NativeFormat};

    result.appsDark = personalize.value("AppsUseLightTheme", 1).toInt() == 0;
    result.systemDark = personalize.value("SystemUsesLightTheme", 1).toInt() == 0;

    try {
        using namespace winrt::Windows::UI::ViewManagement;

        Core::OS::Windows::Winrt::Initialize();

        UISettings uiSettings;
        const auto toQColor = [](const winrt::Windows::UI::Color &color) {
            return QColor{color.R, color.G, color.B};
        };

        result.accent = toQColor(uiSettings.GetColorValue(UIColorType::Accent));
        result.accentForLight = toQColor(uiSettings.GetColorValue(UIColorType::AccentDark1));
        result.accentForDark = toQColor(uiSettings.GetColorValue(UIColorType::AccentLight2));
    }
    catch (const Core::OS::Windows::Winrt::Exception &ex) {
        LOG(Warn, "UISettings failed, fall back to DWM colorization. {}", Helper::ToString(ex));
    }

    if (!result.accent.isValid()) {
        DWORD colorization = 0;
        BOOL opaque = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&colorization, &opaque))) {
            result.accent = QColor{
                (int)((colorization >> 16) & 0xFF), (int)((colorization >> 8) & 0xFF),
                (int)(colorization & 0xFF)};
        }
    }
#endif

    if (!result.accent.isValid()) {
        result.accent = QColor{kDefaultAccent};
    }

    return result;
}

SystemTheme ApplyMode(SystemTheme system, Mode mode)
{
    switch (mode) {
    case Mode::System:
        break;
    case Mode::Light:
        system.appsDark = false;
        break;
    case Mode::Dark:
        system.appsDark = true;
        break;
    }
    return system;
}

Palette BuildPalette(const SystemTheme &system)
{
    Palette p;
    p.dark = system.appsDark;

    if (!p.dark) {
        p.windowBackground = QColor{"#F3F3F3"};
        p.surface = QColor{"#FBFBFB"};
        p.surfaceSecondary = QColor{"#EBEBEB"};
        p.cardBorder = QColor{"#E5E5E5"};
        p.separator = QColor{"#E5E5E5"};
        p.text = QColor{"#1B1B1B"};
        p.textSecondary = QColor{"#5F5F5F"};
        p.textDisabled = QColor{"#9F9F9F"};
        p.controlFill = QColor{"#FDFDFD"};
        p.controlHover = QColor{"#F0F0F0"};
        p.controlPressed = QColor{"#E6E6E6"};
        p.controlBorder = QColor{"#D2D2D2"};
        p.popupSurface = QColor{"#F9F9F9"};
        p.popupBorder = QColor{"#DCDCDC"};

        p.mainSurface = QColor{"#FFFFFF"};
        p.mainText = QColor{"#1B1B1B"};
        p.mainTextSecondary = QColor{"#5E5E5E"};
        p.mainCloseBg = QColor{"#EEEEEF"};
        p.mainCloseHover = QColor{"#E4E4E5"};
        p.mainClosePressed = QColor{"#DADADB"};
        p.mainCloseGlyph = QColor{"#838387"};
        p.batteryBorder = QColor{"#8E8E93"};

        p.accent = system.accentForLight.value_or(system.accent.darker(110));
    }
    else {
        p.windowBackground = QColor{"#202020"};
        p.surface = QColor{"#2B2B2B"};
        p.surfaceSecondary = QColor{"#1B1B1B"};
        p.cardBorder = QColor{"#383838"};
        p.separator = QColor{"#3A3A3A"};
        p.text = QColor{"#FFFFFF"};
        p.textSecondary = QColor{"#C5C5C5"};
        p.textDisabled = QColor{"#767676"};
        p.controlFill = QColor{"#343434"};
        p.controlHover = QColor{"#3D3D3D"};
        p.controlPressed = QColor{"#2C2C2C"};
        p.controlBorder = QColor{"#484848"};
        p.popupSurface = QColor{"#2C2C2C"};
        p.popupBorder = QColor{"#414141"};

        p.mainSurface = QColor{"#1C1C1E"};
        p.mainText = QColor{"#F2F2F7"};
        p.mainTextSecondary = QColor{"#A1A1A6"};
        p.mainCloseBg = QColor{"#2C2C2E"};
        p.mainCloseHover = QColor{"#3A3A3C"};
        p.mainClosePressed = QColor{"#48484A"};
        p.mainCloseGlyph = QColor{"#A1A1A6"};
        p.batteryBorder = QColor{"#636366"};

        p.accent = system.accentForDark.value_or(system.accent.lighter(135));
    }

    // Shared iOS battery colours
    p.batteryNormal = QColor{"#34C759"};
    p.batteryAlarm = QColor{"#FF3B30"};

    // Windows 11 renders hover/pressed as the accent at 90% / 80% opacity over the surface.
    p.accentHover = Mix(p.accent, p.surface, 0.10);
    p.accentPressed = Mix(p.accent, p.surface, 0.20);
    p.accentDisabled = p.dark ? QColor{"#434343"} : QColor{"#C8C8C8"};
    p.accentText = AccessibleForeground(p.accent);

    return p;
}

QString ColorName(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

} // namespace

//////////////////////////////////////////////////

bool LoadBundledFonts()
{
    static const bool loaded = [] {
        constexpr std::array paths{
            ":/Resource/Font/Inter/Inter-Regular.ttf",
            ":/Resource/Font/Inter/Inter-Medium.ttf",
            ":/Resource/Font/Inter/Inter-SemiBold.ttf",
            ":/Resource/Font/Inter/InterDisplay-Medium.ttf",
            ":/Resource/Font/Inter/InterDisplay-SemiBold.ttf",
            ":/Resource/Font/NotoSansTC/NotoSansTC-Variable.ttf",
        };

        bool allLoaded = true;
        for (const auto *path : paths) {
            if (QFontDatabase::addApplicationFont(path) < 0) {
                LOG(Warn, "Failed to load bundled font '{}'", path);
                allLoaded = false;
            }
        }
        return allLoaded;
    }();
    return loaded;
}

QFont ApplicationFont()
{
    LoadBundledFonts();

    QFont font;
    font.setFamilies(BodyFontFamilies());
    font.setPointSize(9);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

const QStringList &DisplayFontFamilies()
{
    static const QStringList families{
        "Inter Display",
        "Noto Sans TC",
        "Inter",
        "Segoe UI Variable Display",
        "Segoe UI Variable",
        "Segoe UI",
        "Microsoft JhengHei UI",
        "Microsoft YaHei UI",
        "Yu Gothic UI",
        "Malgun Gothic",
    };
    return families;
}

//////////////////////////////////////////////////

class Manager::NativeFilter : public QAbstractNativeEventFilter
{
public:
    explicit NativeFilter(Manager &owner) : _owner{owner} {}

    bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) override
    {
#if defined APD_OS_WIN
        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
            return false;
        }

        const auto *msg = static_cast<MSG *>(message);
        bool themeMayHaveChanged = false;

        switch (msg->message) {
        case WM_SETTINGCHANGE:
            if (msg->lParam != 0 &&
                lstrcmpiW(reinterpret_cast<LPCWSTR>(msg->lParam), L"ImmersiveColorSet") == 0)
            {
                themeMayHaveChanged = true;
            }
            break;
        case WM_DWMCOLORIZATIONCOLORCHANGED:
        case WM_THEMECHANGED:
            themeMayHaveChanged = true;
            break;
        default:
            break;
        }

        if (themeMayHaveChanged) {
            _owner.OnSystemThemeChanged();
        }
#endif
        return false;
    }

private:
    Manager &_owner;
};

class Manager::Impl
{
public:
    SystemTheme system;
    Mode mode{Mode::System};
    Palette palette;
    std::unique_ptr<NativeFilter> nativeFilter;
    // A hidden top-level window that guarantees the process receives the `WM_SETTINGCHANGE`
    // broadcast even while every real window is still unopened. `QApplication` deletes every
    // remaining top-level widget on shutdown, so it is only observed, never owned.
    QPointer<QWidget> listener;
    QTimer debounce;
};

//////////////////////////////////////////////////

Manager &Manager::Instance()
{
    // Owned by the application so it is torn down with it, before Qt itself goes away.
    static Manager *instance = new Manager{};
    return *instance;
}

Manager::Manager() : QObject{qApp}, _impl{std::make_unique<Impl>()}
{
    _impl->system = ReadSystemTheme();
    _impl->palette = BuildPalette(ApplyMode(_impl->system, _impl->mode));

    _impl->listener = new QWidget{nullptr, Qt::Tool | Qt::FramelessWindowHint};
    _impl->listener->setObjectName("ThemeListener");
    _impl->listener->setAttribute(Qt::WA_DontShowOnScreen);
    _impl->listener->createWinId();

    _impl->debounce.setSingleShot(true);
    _impl->debounce.setInterval(150);
    connect(&_impl->debounce, &QTimer::timeout, this, [this] { Refresh(false); });

    _impl->nativeFilter = std::make_unique<NativeFilter>(*this);
    qApp->installNativeEventFilter(_impl->nativeFilter.get());
    qApp->installEventFilter(this);

    LOG(Info, "Theme: appsDark: '{}', systemDark: '{}', accent: '{}'", _impl->system.appsDark,
        _impl->system.systemDark, ColorName(_impl->system.accent));
}

Manager::~Manager()
{
    if (qApp != nullptr) {
        qApp->removeNativeEventFilter(_impl->nativeFilter.get());
        qApp->removeEventFilter(this);
    }
    delete _impl->listener.data();
}

const Palette &Manager::Colors() const
{
    return _impl->palette;
}

bool Manager::IsDark() const
{
    return _impl->palette.dark;
}

bool Manager::IsSystemDark() const
{
    return _impl->system.systemDark;
}

QColor Manager::Accent() const
{
    return _impl->palette.accent;
}

Mode Manager::CurrentMode() const
{
    return _impl->mode;
}

void Manager::SetMode(Mode mode)
{
    if (_impl->mode == mode) {
        return;
    }

    _impl->mode = mode;
    Refresh(true);
}

QPalette Manager::QtPalette() const
{
    const auto &p = _impl->palette;

    QPalette palette;

    palette.setColor(QPalette::Window, p.windowBackground);
    palette.setColor(QPalette::WindowText, p.text);
    palette.setColor(QPalette::Base, p.surface);
    palette.setColor(QPalette::AlternateBase, p.surfaceSecondary);
    palette.setColor(QPalette::Text, p.text);
    palette.setColor(QPalette::Button, p.controlFill);
    palette.setColor(QPalette::ButtonText, p.text);
    palette.setColor(QPalette::BrightText, p.dark ? Qt::black : Qt::white);
    palette.setColor(QPalette::Highlight, p.accent);
    palette.setColor(QPalette::HighlightedText, p.accentText);
    palette.setColor(QPalette::ToolTipBase, p.popupSurface);
    palette.setColor(QPalette::ToolTipText, p.text);
    palette.setColor(QPalette::Link, p.accent);
    palette.setColor(QPalette::LinkVisited, p.accent);
    palette.setColor(QPalette::PlaceholderText, p.textSecondary);

    // Fusion draws frames and separators with these
    palette.setColor(QPalette::Light, p.controlHover);
    palette.setColor(QPalette::Midlight, p.controlBorder);
    palette.setColor(QPalette::Mid, p.cardBorder);
    palette.setColor(QPalette::Dark, p.dark ? QColor{"#171717"} : QColor{"#C8C8C8"});
    palette.setColor(QPalette::Shadow, p.dark ? Qt::black : QColor{"#A0A0A0"});

    palette.setColor(QPalette::Disabled, QPalette::WindowText, p.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, p.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, p.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Base, p.windowBackground);
    palette.setColor(QPalette::Disabled, QPalette::Button, p.windowBackground);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, p.accentDisabled);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, p.textDisabled);

    return palette;
}

QString Manager::StyleSheet() const
{
    const auto &p = _impl->palette;

    // `image: url()` cannot take data URIs in Qt 5, so glyphs ship as qrc SVGs in two tints.
    const auto onAccentGlyph = p.accentText == Qt::black ? "Dark" : "Light";
    const auto onSurfaceGlyph = p.dark ? "Light" : "Dark";

    QString sheet = R"(
/* ---------- Buttons ---------- */
QPushButton {
    min-height: 30px;
    padding: 0 16px;
    border-radius: 5px;
    border: 1px solid @controlBorder;
    background: @controlFill;
    color: @text;
}
QPushButton:hover { background: @controlHover; }
QPushButton:pressed { background: @controlPressed; color: @textSecondary; }
QPushButton:disabled { background: @windowBackground; color: @textDisabled; border-color: @cardBorder; }
QPushButton:default, QPushButton[cssClass="accent"] {
    background: @accent;
    color: @accentText;
    border: 1px solid @accent;
}
QPushButton:default:hover, QPushButton[cssClass="accent"]:hover {
    background: @accentHover; border-color: @accentHover;
}
QPushButton:default:pressed, QPushButton[cssClass="accent"]:pressed {
    background: @accentPressed; border-color: @accentPressed; color: @accentText;
}
QPushButton:default:disabled, QPushButton[cssClass="accent"]:disabled {
    background: @accentDisabled; border-color: @accentDisabled; color: @textDisabled;
}

/* ---------- Check box ---------- */
QCheckBox { spacing: 8px; }
QCheckBox::indicator {
    width: 18px; height: 18px;
    border-radius: 4px;
    border: 1px solid @controlBorder;
    background: @controlFill;
}
QCheckBox::indicator:hover { border-color: @textSecondary; background: @controlHover; }
QCheckBox::indicator:checked {
    border-color: @accent;
    background: @accent;
    image: url(:/Resource/Image/Theme/CheckMark@onAccentGlyph.svg);
}
QCheckBox::indicator:checked:hover { background: @accentHover; border-color: @accentHover; }
QCheckBox::indicator:disabled { border-color: @cardBorder; background: @windowBackground; }
QCheckBox::indicator:checked:disabled { background: @accentDisabled; border-color: @accentDisabled; }

/* ---------- Radio button ---------- */
QRadioButton { spacing: 8px; }
QRadioButton::indicator {
    width: 18px; height: 18px;
    border-radius: 10px;
    border: 1px solid @controlBorder;
    background: @controlFill;
}
QRadioButton::indicator:hover { border-color: @textSecondary; background: @controlHover; }
QRadioButton::indicator:checked {
    width: 10px; height: 10px;
    border: 5px solid @accent;
    background: @accentText;
}
QRadioButton::indicator:checked:hover { width: 12px; height: 12px; border-width: 4px; }
QRadioButton::indicator:checked:disabled { border-color: @accentDisabled; }

/* ---------- Slider ---------- */
QSlider::groove:horizontal {
    height: 4px;
    border-radius: 2px;
    background: @controlBorder;
}
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 8px; height: 8px;
    margin: -8px 0;
    border-radius: 9px;
    border: 5px solid @controlFill;
    background: @accent;
}
QSlider::handle:horizontal:hover { width: 10px; height: 10px; border-width: 4px; }
QSlider::handle:horizontal:pressed { width: 6px; height: 6px; border-width: 6px; }
QSlider::sub-page:horizontal:disabled { background: @accentDisabled; }

/* ---------- Combo box ---------- */
QComboBox {
    min-height: 28px;
    padding: 0 32px 0 10px;
    border-radius: 5px;
    border: 1px solid @controlBorder;
    background: @controlFill;
    color: @text;
}
QComboBox:hover { background: @controlHover; }
QComboBox:on { background: @controlPressed; }
QComboBox:disabled { background: @windowBackground; color: @textDisabled; border-color: @cardBorder; }
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: center right;
    width: 28px;
    border: none;
}
QComboBox::down-arrow {
    image: url(:/Resource/Image/Theme/ChevronDown@onSurfaceGlyph.svg);
    width: 12px; height: 12px;
}
QComboBox QAbstractItemView {
    background: @popupSurface;
    border: 1px solid @popupBorder;
    border-radius: 6px;
    padding: 4px;
    outline: 0;
    color: @text;
    selection-background-color: @controlHover;
    selection-color: @text;
}
QComboBox QAbstractItemView::item {
    min-height: 28px;
    padding: 0 8px;
    border-radius: 4px;
}
QComboBox QAbstractItemView::item:hover { background: @controlHover; }

/* ---------- Lists ---------- */
QListWidget {
    background: @surface;
    border: 1px solid @cardBorder;
    border-radius: 6px;
    padding: 4px;
    outline: 0;
}
QListWidget::item {
    min-height: 32px;
    padding: 0 8px;
    border-radius: 4px;
    color: @text;
}
QListWidget::item:hover { background: @controlHover; }
QListWidget::item:selected { background: @controlHover; color: @text; }

/* Settings navigation pane */
QWidget#navPane { background: @surfaceSecondary; }
QListWidget#navList {
    background: transparent;
    border: none;
    padding: 0;
}
QListWidget#navList::item {
    min-height: 36px;
    padding-left: 12px;
    margin: 2px 0;
    border-radius: 5px;
    border-left: 3px solid transparent;
}
QListWidget#navList::item:hover { background: @controlHover; }
QListWidget#navList::item:selected {
    background: @controlHover;
    border-left: 3px solid @accent;
}
QListWidget#navList:focus::item:selected { background: @controlPressed; }

/* ---------- Settings cards ---------- */
QFrame[cssClass="settingCard"] {
    background: @surface;
    border: 1px solid @cardBorder;
    border-radius: 6px;
}
QLabel[cssClass="cardTitle"] {
    font-family: "Inter", "Noto Sans TC", "Microsoft JhengHei UI";
    font-size: 10pt;
    font-weight: 500;
    color: @text;
    background: transparent;
}
QLabel[cssClass="cardDescription"] { color: @textSecondary; background: transparent; }
QLabel[cssClass="pageTitle"] {
    font-family: "Inter Display", "Noto Sans TC", "Microsoft JhengHei UI";
    font-size: 20pt;
    font-weight: 600;
    color: @text;
}
QLabel[cssClass="appTitle"] {
    font-family: "Inter Display", "Noto Sans TC", "Microsoft JhengHei UI";
    font-size: 14pt;
    font-weight: 600;
    color: @text;
}

/* ---------- Text areas ---------- */
QTextEdit, QPlainTextEdit {
    background: @surface;
    border: 1px solid @controlBorder;
    border-radius: 5px;
    padding: 4px;
    selection-background-color: @accent;
    selection-color: @accentText;
}
QTextBrowser#tbCredits { background: transparent; border: none; padding: 0; }

/* ---------- Group box ---------- */
QGroupBox {
    border: 1px solid @cardBorder;
    border-radius: 6px;
    margin-top: 12px;
    padding-top: 8px;
    background: @surface;
}
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: @textSecondary; }

/* ---------- Progress bar ---------- */
QProgressBar {
    border: none;
    border-radius: 3px;
    background: @controlBorder;
    max-height: 6px;
    min-height: 6px;
}
QProgressBar::chunk { background: @accent; border-radius: 3px; }

/* ---------- Scroll bars ---------- */
QScrollBar:vertical { width: 10px; background: transparent; margin: 2px; }
QScrollBar:horizontal { height: 10px; background: transparent; margin: 2px; }
QScrollBar::handle:vertical { min-height: 24px; border-radius: 3px; background: @controlBorder; }
QScrollBar::handle:horizontal { min-width: 24px; border-radius: 3px; background: @controlBorder; }
QScrollBar::handle:hover { background: @textSecondary; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

/* ---------- Tool tip ---------- */
QToolTip {
    background: @popupSurface;
    color: @text;
    border: 1px solid @popupBorder;
    border-radius: 4px;
    padding: 6px 8px;
}

/* ---------- Menu ---------- */
QMenu {
    background: @popupSurface;
    border: 1px solid @popupBorder;
    border-radius: 8px;
    padding: 4px;
}
QMenu::item {
    padding: 7px 28px 7px 12px;
    border-radius: 4px;
    background: transparent;
    color: @text;
}
QMenu::item:selected { background: @controlHover; }
QMenu::item:disabled { color: @textDisabled; }
QMenu::separator { height: 1px; background: @separator; margin: 4px 8px; }
QMenu::indicator { width: 16px; height: 16px; margin-left: 6px; }
QMenu::indicator:non-exclusive:checked { image: url(:/Resource/Image/Theme/CheckMark@onSurfaceGlyph.svg); }

/* ---------- Dialog button box ---------- */
QDialogButtonBox { dialogbuttonbox-buttons-have-icons: 0; }
)";

    const std::pair<QString, QString> replacements[] = {
        {"@windowBackground", ColorName(p.windowBackground)},
        {"@surfaceSecondary", ColorName(p.surfaceSecondary)},
        {"@surface", ColorName(p.surface)},
        {"@cardBorder", ColorName(p.cardBorder)},
        {"@separator", ColorName(p.separator)},
        {"@textSecondary", ColorName(p.textSecondary)},
        {"@textDisabled", ColorName(p.textDisabled)},
        {"@text", ColorName(p.text)},
        {"@controlFill", ColorName(p.controlFill)},
        {"@controlHover", ColorName(p.controlHover)},
        {"@controlPressed", ColorName(p.controlPressed)},
        {"@controlBorder", ColorName(p.controlBorder)},
        {"@accentHover", ColorName(p.accentHover)},
        {"@accentPressed", ColorName(p.accentPressed)},
        {"@accentDisabled", ColorName(p.accentDisabled)},
        {"@accentText", ColorName(p.accentText)},
        {"@accent", ColorName(p.accent)},
        {"@popupSurface", ColorName(p.popupSurface)},
        {"@popupBorder", ColorName(p.popupBorder)},
        {"@onAccentGlyph", onAccentGlyph},
        {"@onSurfaceGlyph", onSurfaceGlyph},
    };

    // Longer names are listed before their prefixes (e.g. `@textSecondary` before `@text`)
    for (const auto &[key, value] : replacements) {
        sheet.replace(key, value);
    }

    return sheet;
}

void Manager::ApplyToApplication()
{
    qApp->setPalette(QtPalette());
    qApp->setStyleSheet(StyleSheet());

    for (auto *widget : QApplication::topLevelWidgets()) {
        ApplyToWindow(widget);
    }
}

void Manager::ApplyToWindow(QWidget *topLevel)
{
#if defined APD_OS_WIN
    if (topLevel == nullptr || !topLevel->isWindow() ||
        topLevel->property(kSkipDwmProperty).toBool())
    {
        return;
    }

    auto *window = topLevel->windowHandle();
    if (window == nullptr) {
        return;
    }

    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd == nullptr) {
        return;
    }

    const auto type = topLevel->windowFlags() & Qt::WindowType_Mask;
    const bool isPopup = type == Qt::Popup || type == Qt::ToolTip;

    if (!isPopup) {
        BOOL dark = _impl->palette.dark ? TRUE : FALSE;
        if (FAILED(DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark)))) {
            DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkModeBefore20H1, &dark, sizeof(dark));
        }
    }
    else if (Core::OS::Windows::System::Is11OrGreater()) {
        // Menus, combo popups and tool tips paint a rounded border in the stylesheet; DWM
        // clips the window itself so no square corners peek out.
        DWORD preference = kDwmwcpRoundSmall;
        DwmSetWindowAttribute(hwnd, kDwmwaWindowCornerPreference, &preference, sizeof(preference));
    }
#endif
}

void Manager::Refresh(bool force)
{
    const auto system = ReadSystemTheme();
    if (!force && system == _impl->system) {
        return;
    }

    LOG(Info, "Theme changed: appsDark: '{}', systemDark: '{}', accent: '{}'", system.appsDark,
        system.systemDark, ColorName(system.accent));

    _impl->system = system;
    _impl->palette = BuildPalette(ApplyMode(system, _impl->mode));

    ApplyToApplication();

#if defined APD_OS_WIN
    for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *window = widget->windowHandle(); window != nullptr && widget->isVisible()) {
            SetWindowPos(
                reinterpret_cast<HWND>(window->winId()), nullptr, 0, 0, 0, 0,
                SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
#endif

    emit Changed();
}

void Manager::OnSystemThemeChanged()
{
    // Windows sends several notifications per change; coalesce them.
    _impl->debounce.start();
}

bool Manager::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Show) {
        if (auto *widget = qobject_cast<QWidget *>(watched);
            widget != nullptr && widget->isWindow())
        {
            ApplyToWindow(widget);
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace Gui::Theme
