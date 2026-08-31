add_executable(
    AirPodsDomainTests

    AirPodsDomainTests.cpp
)

target_link_libraries(
    AirPodsDomainTests

    PRIVATE
    apd_core
    apd_presentation
    Qt5::Test
)

target_include_directories(AirPodsDomainTests PRIVATE "${CMAKE_SOURCE_DIR}")

# The qrc is linked into the application target, so resource checks read the source tree instead.
target_compile_definitions(
    AirPodsDomainTests PRIVATE APD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME AirPodsDomainTests COMMAND AirPodsDomainTests)

add_executable(QuickConnectTests QuickConnectTests.cpp)

target_link_libraries(
    QuickConnectTests

    PRIVATE
    apd_core
    Qt5::Test
    Qt5::Widgets
)

target_include_directories(QuickConnectTests PRIVATE "${CMAKE_SOURCE_DIR}")

add_test(NAME QuickConnectTests COMMAND QuickConnectTests)

if (WIN32)
    add_test(
        NAME QuickConnectWindowsBackendContract
        COMMAND
            "${CMAKE_COMMAND}"
            "-DSOURCE_FILE=${CMAKE_SOURCE_DIR}/Source/Core/QuickConnect_win.cpp"
            "-DCONTROLLER_SOURCE_FILE=${CMAKE_SOURCE_DIR}/Source/Core/QuickConnect.cpp"
            "-DSETTINGS_SOURCE_FILE=${CMAKE_SOURCE_DIR}/Source/Gui/SettingsWindow.cpp"
            "-DTRAY_SOURCE_FILE=${CMAKE_SOURCE_DIR}/Source/Gui/TrayIcon.cpp"
            "-DSETTINGS_WINDOW_SOURCE_FILE=${CMAKE_SOURCE_DIR}/Source/Gui/SettingsWindow.cpp"
            -P "${CMAKE_SOURCE_DIR}/Tests/QuickConnectWindowsBackendContract.cmake"
    )
endif()
