#
# AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
# Copyright (C) 2021 SpriteOvO
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#

cmake_minimum_required(VERSION 3.20)

include(FetchContent)

project(
    AirPodsDesktop
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "AirPods desktop user experience enhancement program"
    HOMEPAGE_URL "https://github.com/SpriteOvO/AirPodsDesktop"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

##################################################
# Build options
#

set(APD_BUILD_TESTS OFF CACHE BOOL "Build tests.")
set(APD_ENABLE_CONSOLE OFF CACHE BOOL "Enable console.")
set(APD_GENERATE_INSTALLER OFF CACHE BOOL "Generate installer after build.")
set(APD_QT_PATH CACHE PATH "Qt 5 path. Can be empty if you have set it in the PATH env variable.")
set(APD_QT_DEPLOY ON CACHE BOOL "Run Qt deployment tool after build")

##################################################

set(APD_BINARY_OUT_DIR "${CMAKE_BINARY_DIR}/Binary")
set(
    APD_OUTPUT_DIRECTORIES

    CMAKE_RUNTIME_OUTPUT_DIRECTORY
    CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG
    CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE
    CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
    CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO

    CMAKE_LIBRARY_OUTPUT_DIRECTORY
    CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG
    CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE
    CMAKE_LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL
    CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO

    CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO
)
foreach (OUTPUT_DIRECTORY ${APD_OUTPUT_DIRECTORIES})
    set(${OUTPUT_DIRECTORY} ${APD_BINARY_OUT_DIR})
endforeach()

##################################################
# Global options
#

if (MSVC)
    # /MP: Multi-processor compilation
    add_compile_options("/MP")
endif()

##################################################
# Third party
#

# Qt
#
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${APD_QT_PATH})
set(APD_QT_COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets)
foreach (QT_COMPONENT ${APD_QT_COMPONENTS})
    set(APD_QT_LIBRARIES ${APD_QT_LIBRARIES} Qt5::${QT_COMPONENT})
endforeach()
find_package(Qt5 COMPONENTS ${APD_QT_COMPONENTS} REQUIRED)

# spdlog
#
add_compile_definitions(SPDLOG_WCHAR_TO_UTF8_SUPPORT)
message("Fetching 'spdlog'...")
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY "https://github.com/gabime/spdlog.git"
    GIT_TAG "v1.8.5"
)
FetchContent_MakeAvailable(spdlog)
message("Fetch 'spdlog' done.")

# cxxopts
#
message("Fetching 'cxxopts'...")
FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY "https://github.com/jarro2783/cxxopts.git"
    GIT_TAG "v2.2.1"
)
FetchContent_MakeAvailable(cxxopts)
message("Fetch 'cxxopts' done.")

# cpr
#
set(CPR_BUILD_TESTS ${APD_BUILD_TESTS})
set(CPR_BUILD_TESTS_SSL OFF)
set(CURL_ZLIB OFF CACHE STRING "" FORCE)
message("Fetching 'cpr'...")
FetchContent_Declare(
    cpr
    GIT_REPOSITORY "https://github.com/whoshuu/cpr.git"
    GIT_TAG "92009ee4bd6377837420e4e2399f24ec7c5a765a"
)
FetchContent_MakeAvailable(cpr)
message("Fetch 'cpr' done.")

# json
#
message("Fetching 'json'...")
FetchContent_Declare(
    json
    GIT_REPOSITORY "https://github.com/nlohmann/json.git"
    GIT_TAG "v3.9.1"
)
FetchContent_MakeAvailable(json)
message("Fetch 'json' done.")

##################################################

set(
    APD_CODE_FILES

    "Source/Main.cpp"
    "Source/Logger.cpp"
    "Source/Assert.cpp"
    "Source/Application.cpp"

    "Source/Gui/SysTray.cpp"
    "Source/Gui/InfoWindow.cpp"
    "Source/Gui/SelectWindow.cpp"
    "Source/Gui/DownloadWindow.cpp"
    "Source/Gui/SettingsWindow.cpp"
    "Source/Gui/Widget/Battery.cpp"

    "Source/Core/Update.cpp"
    "Source/Core/AirPods.cpp"
    "Source/Core/AppleCP.cpp"
    "Source/Core/Settings.cpp"
    "Source/Core/LowAudioLatency.cpp"
)

set(ADD_EXECUTABLE_ARG)
set(APD_COMPILE_DEFINITIONS)

if (MSVC)
    set(
        APD_CODE_FILES ${APD_CODE_FILES}

        "Source/Core/Bluetooth_win.cpp"
        "Source/Core/GlobalMedia_win.cpp"

        "Source/Resource/Resource.rc"
    )
    if (APD_ENABLE_CONSOLE)
        set(APD_COMPILE_DEFINITIONS ${APD_COMPILE_DEFINITIONS} APD_ENABLE_CONSOLE)
    elseif()
        set(ADD_EXECUTABLE_ARG WIN32)
    endif()

    # Workaround for: "_MSC_VER" is undefined in .rc files
    #
    set_source_files_properties("Source/Resource/Resource.rc" PROPERTIES COMPILE_FLAGS "/d_MSC_VER")
endif()

qt5_add_resources(APD_CODE_FILES "Source/Resource/Resource.qrc")

add_executable(
    ${PROJECT_NAME} ${ADD_EXECUTABLE_ARG}

    ${APD_CODE_FILES}
)

target_compile_definitions(
    ${PROJECT_NAME} PRIVATE

    $<$<CONFIG:Debug>:APD_DEBUG>
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
    ${APD_COMPILE_DEFINITIONS}
)

##################################################

if (MSVC)
    if (APD_QT_DEPLOY)
        add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND "${Qt5_DIR}/../../../bin/windeployqt" "${APD_BINARY_OUT_DIR}/${PROJECT_NAME}.exe"
            COMMENT "Deploying Qt..."
        )
    endif()
endif()

##################################################
# Installer generate
#

if (APD_GENERATE_INSTALLER)
    set(CPACK_PACKAGE_VENDOR ${PROJECT_NAME})
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/Installer")
    set(CPACK_PACKAGE_VERSION_MAJOR ${AirPodsDesktop_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${AirPodsDesktop_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${AirPodsDesktop_VERSION_PATCH})
    set(CPACK_PACKAGE_INSTALL_DIRECTORY ${PROJECT_NAME})
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_PACKAGE_EXECUTABLES "${PROJECT_NAME}" ${PROJECT_NAME})

    set(ICON_FILEPATH "${CMAKE_CURRENT_SOURCE_DIR}/Source/Resource/Image/Icon.ico")
    set(SHORTCUT_NAME "AirPods Desktop")

    if (MSVC)
        set(CPACK_GENERATOR NSIS)
        set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
        set(CPACK_NSIS_URL_INFO_ABOUT ${CMAKE_PROJECT_HOMEPAGE_URL})
        set(CPACK_NSIS_EXECUTABLES_DIRECTORY "./")
        set(CPACK_NSIS_MUI_FINISHPAGE_RUN "./${PROJECT_NAME}.exe")
        set(CPACK_NSIS_BRANDING_TEXT "${PROJECT_NAME} installer")
        string(REPLACE "/" "\\\\" ICON_FILEPATH ${ICON_FILEPATH})
        set(CPACK_NSIS_MUI_ICON ${ICON_FILEPATH})

        # Create and remove shortcut
        #
        # Workaround for: https://gitlab.kitware.com/cmake/cmake/-/issues/15982
        #                 (I can't believe this is an unsolved issue 5 years old lol..)
        #
        set(
            CPACK_NSIS_EXTRA_INSTALL_COMMANDS
            "CreateShortCut \\\"$DESKTOP\\\\${SHORTCUT_NAME}.lnk\\\" \\\"$INSTDIR\\\\${PROJECT_NAME}.exe\\\""
        )
        set(
            CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
            "Delete \\\"$DESKTOP\\\\${SHORTCUT_NAME}.lnk\\\""
        )
    endif()

    # Prevent CMake from adding "include" and "lib" directories
    #
    install(
        DIRECTORY "${APD_BINARY_OUT_DIR}/"
        DESTINATION "./"
        COMPONENT ${PROJECT_NAME}
        PATTERN "*.ilk" EXCLUDE
        PATTERN "*.exp" EXCLUDE
        PATTERN "*.lib" EXCLUDE
        PATTERN "*.pdb" EXCLUDE
    )
    set(CPACK_COMPONENTS_ALL ${PROJECT_NAME})

    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${CMAKE_CPACK_COMMAND}" "-C" "$<CONFIGURATION>"
        COMMENT "Generating installer..."
    )
endif()
include(CPack) # We always need variable "CPACK_SYSTEM_NAME"

##################################################
# Configure
#

configure_file("Source/Config.h.in" "Source/Config.h")
target_include_directories(${PROJECT_NAME} PUBLIC "${PROJECT_BINARY_DIR}/Source")

##################################################

target_link_libraries(
    ${PROJECT_NAME}
    
    ${APD_QT_LIBRARIES}
    spdlog::spdlog
    cxxopts
    cpr::cpr
    nlohmann_json::nlohmann_json
)
