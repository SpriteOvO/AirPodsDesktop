#
# AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
# Copyright (C) 2021-2022 SpriteOvO
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

# Keep dependencies with older cmake_minimum_required declarations usable with CMake 4.
if (CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()
if (POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

set(APD_GITHUB_OWNER "SpriteOvO" CACHE STRING "GitHub repository owner used by the updater.")
set(
    APD_GITHUB_REPOSITORY
    "AirPodsDesktop"
    CACHE STRING
    "GitHub repository name used by the updater."
)
set(
    APD_RELEASE_TAG_PREFIX
    "v"
    CACHE STRING
    "Prefix the release tags of the configured repository use (\"v\" for v1.2.3, empty for 1.2.3)."
)

project(
    AirPodsDesktop
    VERSION 0.5.2 # Don't forget to bump the version in `vcpkg.json` as well
    LANGUAGES C CXX
    DESCRIPTION "AirPods desktop user experience enhancement program"
    HOMEPAGE_URL "https://github.com/${APD_GITHUB_OWNER}/${APD_GITHUB_REPOSITORY}"
)

if (NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "AirPodsDesktop requires 64-bit Windows and MSVC 2022.")
endif()
if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "AirPodsDesktop no longer supports Win32. Configure with -A x64.")
endif()
if (NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "AirPodsDesktop supports the x64 ABI only; ARM64 is not supported.")
endif()
if (MSVC_VERSION LESS 1930)
    message(FATAL_ERROR "AirPodsDesktop requires Visual Studio 2022 or newer.")
endif()

include(FetchContent)
include(CMake/Utils.cmake)

if (MSVC)
    # windeployqt cannot always discover Visual Studio from non-developer shells. Ask CMake for the
    # redistributable DLLs explicitly and copy them beside the executable before packaging.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS TRUE)
    include(InstallRequiredSystemLibraries)
endif()

if(MSVC AND ${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.20.4")
    #
    # Workaround
    #
    # The current C++20 <format> is defective, and some ABI-breaking changes will be applied soon.
    # So we can't use it in the MSVC stable ABI C++20 (compiler option `/std:c++20`).
    # The way to use <format> now is to specify the `/std:c++latest` option to MSVC.
    #
    # But since CMake 3.20.4, when the value of `CMAKE_CXX_STANDARD` is `20`, the specified option
    # is no longer `/std:c++latest` but `/std:c++20`.
    #
    # There are two workarounds:
    # 1. Set `CMAKE_CXX_STANDARD` to `23`, which makes CMake specify `/std:c++latest` to the compiler.
    # 2. Modify the compiler option specified when `CMAKE_CXX_STANDARD` is `20`.
    #
    # Here we choose the second one, because I think it is relatively cleaner.
    #
    # TODO: Remove this workaround when <format> is available in `/std:c++20`.
    #
    # For more details:
    # https://github.com/microsoft/STL/issues/1814
    # https://gitlab.kitware.com/cmake/cmake/-/commit/3aaf1d91bf353559aef7ec56ad35924383ed613a
    #
    set(CMAKE_CXX20_STANDARD_COMPILE_OPTION "-std:c++latest")
    set(CMAKE_CXX20_EXTENSION_COMPILE_OPTION "-std:c++latest")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(VCPKG_FEATURE_FLAGS "versions")

##################################################
# Build options
#

set(APD_BUILD_TESTS OFF CACHE BOOL "Build first-party tests.")
set(APD_ENABLE_CONSOLE OFF CACHE BOOL "Enable console.")
set(APD_GENERATE_INSTALLER OFF CACHE BOOL "Generate installer after build.")
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
    
    add_compile_options(
        "/MP"           # Multi-processor compilation

        "/await"        # Enable the legacy pre-C++20 coroutine.
                        # Workaround for cppwinrt v2.0.200609.3 in C++20.
                        # For more information, see https://github.com/microsoft/cppwinrt/issues/866
    )
endif()

##################################################
# Third party
#

# Qt
#
set(APD_QT_COMPONENTS Core Gui Widgets Svg Multimedia LinguistTools)
find_package(Qt6 6.8 COMPONENTS ${APD_QT_COMPONENTS} REQUIRED)
if (APD_BUILD_TESTS)
    find_package(Qt6 6.8 COMPONENTS Test REQUIRED)
endif()

# spdlog
#
add_compile_definitions(SPDLOG_WCHAR_TO_UTF8_SUPPORT SPDLOG_WCHAR_FILENAMES)
# 
# TODO: Commented out since I don't how to enable `SPDLOG_WCHAR_FILENAMES` for vcpkg, I have tried
#       `set(SPDLOG_WCHAR_FILENAMES ON)` and `add_compile_definitions(SPDLOG_WCHAR_FILENAMES)`,
#       but still not working
#
# find_package(spdlog CONFIG)
if (spdlog_FOUND)
    message("Found 'spdlog' (${spdlog_VERSION}).")
else()
    message("Fetching 'spdlog'...")
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY "https://github.com/gabime/spdlog.git"
        GIT_TAG "100f30043f33277122e0991c83845a2617172ffd" # v1.8.5
    )
    FetchContent_MakeAvailable(spdlog)
    message("Fetch 'spdlog' done.")
endif()

# cxxopts
#
find_package(cxxopts CONFIG)
if (cxxopts_FOUND)
    message("Found 'cxxopts' (${cxxopts_VERSION}).")
else()
    message("Fetching 'cxxopts'...")
    FetchContent_Declare(
        cxxopts
        GIT_REPOSITORY "https://github.com/jarro2783/cxxopts.git"
        GIT_TAG "302302b30839505703d37fb82f536c53cf9172fa" # v2.2.1
    )
    FetchContent_MakeAvailable(cxxopts)
    message("Fetch 'cxxopts' done.")

    add_library(cxxopts::cxxopts ALIAS cxxopts)
endif()

# cpr
#
find_package(cpr 1.7.2 CONFIG)
if (cpr_FOUND)
    message("Found 'cpr' (${cpr_VERSION}).")
else()
    set(CPR_BUILD_TESTS OFF)
    set(CPR_BUILD_TESTS_SSL OFF)
    set(CURL_ZLIB OFF CACHE STRING "" FORCE)
    message("Fetching 'cpr'...")
    FetchContent_Declare(
        cpr
        GIT_REPOSITORY "https://github.com/whoshuu/cpr.git"
        GIT_TAG "beb9e98806bb84bcc130a2cebfbcbbc6ce62b335" # v1.7.2
    )
    FetchContent_MakeAvailable(cpr)
    message("Fetch 'cpr' done.")
endif()

# json
#
find_package(nlohmann_json CONFIG)
if (nlohmann_json_FOUND)
    message("Found 'nlohmann_json' (${nlohmann_json_VERSION}).")
else()
    message("Fetching 'nlohmann_json'...")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY "https://github.com/nlohmann/json.git"
        GIT_TAG "db78ac1d7716f56fc9f1b030b715f872f93964e4" # v3.9.1
    )
    FetchContent_MakeAvailable(nlohmann_json)
    message("Fetch 'nlohmann_json' done.")
endif()

# SingleApplication
#
set(QAPPLICATION_CLASS "QApplication" CACHE STRING "Inheritance class for SingleApplication")
set(QT_DEFAULT_MAJOR_VERSION 6 CACHE STRING "Qt major version used by dependencies" FORCE)
message("Fetching 'SingleApplication'...")
FetchContent_Declare(
    SingleApplication
    GIT_REPOSITORY "https://github.com/itay-grudev/SingleApplication.git"
    GIT_TAG "a218603d76a55dddb65b84f2b49ecaa8efc074ea" # v3.5.6
)
FetchContent_MakeAvailable(SingleApplication)
message("Fetch 'SingleApplication' done.")

# magic_enum
#
find_package(magic_enum CONFIG)
if (magic_enum_FOUND)
    message("Found 'magic_enum' (${magic_enum_VERSION}).")
else()
    message("Fetching 'magic_enum'...")
    FetchContent_Declare(
        magic_enum
        GIT_REPOSITORY "https://github.com/Neargye/magic_enum.git"
        GIT_TAG "3d1f6a5a2a3fbcba077e00ad0ccc2dd9fefc2ca7" # v0.7.3
    )
    FetchContent_MakeAvailable(magic_enum)
    message("Fetch 'magic_enum' done.")
endif()

#
# Boost libraries
#

# stacktrace
#
if (MSVC)
    set(APD_STACKTRACE_COMPONENT stacktrace_windbg)
endif()

# Find boost-stacktrace using pkg-config or manually
find_path(BOOST_STACKTRACE_INCLUDE_DIR boost/stacktrace.hpp)
include_directories(${BOOST_STACKTRACE_INCLUDE_DIR})

# Find boost-pfr
find_path(BOOST_PFR_INCLUDE_DIR boost/pfr.hpp)
include_directories(${BOOST_PFR_INCLUDE_DIR})

# Create interface targets for boost libraries
if(NOT TARGET Boost::stacktrace_windbg)
    add_library(Boost::stacktrace_windbg INTERFACE IMPORTED)
    set_target_properties(Boost::stacktrace_windbg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${BOOST_STACKTRACE_INCLUDE_DIR}")
endif()

if(NOT TARGET Boost::pfr)
    add_library(Boost::pfr INTERFACE IMPORTED)
    set_target_properties(Boost::pfr PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${BOOST_PFR_INCLUDE_DIR}")
endif()

##################################################

set(
    APD_SUPPORT_CODE_FILES

    "Source/Opts.cpp"
    "Source/Logger.cpp"
    "Source/Assert.cpp"
    "Source/Error.cpp"
)

set(
    APD_DOMAIN_CODE_FILES

    "Source/Core/AirPodsAdvertisement.cpp"
    "Source/Core/AirPodsStateManager.cpp"
    "Source/Core/AppleCP.cpp"
)

set(
    APD_CORE_CODE_FILES

    "Source/Core/Debug.cpp"
    "Source/Core/Update.cpp"
    "Source/Core/AirPods.cpp"
    "Source/Core/QuickConnect.cpp"
    "Source/Core/Settings.cpp"
    "Source/Core/SettingsRepository.cpp"
    "Source/Core/LowAudioLatency.cpp"
)

set(
    APD_PRESENTATION_CODE_FILES

    "Source/Gui/MainWindowPresentation.cpp"
)

set(
    APD_GUI_CODE_FILES

    "Source/Gui/AnimationPlayback.cpp"

    "Source/Gui/TrayIcon.cpp"
    "Source/Gui/TaskbarStatus.cpp"
    "Source/Gui/MainWindow.cpp"
    "Source/Gui/SelectWindow.cpp"
    "Source/Gui/UpdateWindow.cpp"
    "Source/Gui/SettingsWindow.cpp"
    "Source/Gui/Widget/Battery.cpp"
    "Source/Gui/Widget/AnimationView.cpp"
    "Source/Gui/Theme.cpp"
)

set(APD_APP_CODE_FILES
    "Source/Main.cpp"
    "Source/Application.cpp"
)

set(ADD_EXECUTABLE_ARG)

if (WIN32)
    list(APPEND APD_CORE_CODE_FILES
        "Source/Core/AutoStart_win.cpp"
        "Source/Core/Bluetooth_win.cpp"
        "Source/Core/GlobalMedia_win.cpp"
        "Source/Core/QuickConnect_win.cpp"
    )
    list(APPEND APD_APP_CODE_FILES
        "Source/Resource/Application.manifest"
        "Source/Resource/Resource.rc"
    )
    if (NOT APD_ENABLE_CONSOLE)
        set(ADD_EXECUTABLE_ARG WIN32)
    endif()

    # Workaround for: "_MSC_VER" is undefined in .rc files
    #
    set_source_files_properties("Source/Resource/Resource.rc" PROPERTIES COMPILE_FLAGS "/d_MSC_VER")
endif()

##################################################
# Qt configurations
#

# Resource

set(APD_QRC_FILE "Source/Resource/Resource.qrc")

#
# Translation
#

#
# Workaround: Well, another ancient CMake or Qt bug
#             CMake will delete all translation files when you rebuild, not just update them
#             So `qt5_create_translation` doesn't work
#             We have to add a TARGET ourselves to execute the `lupdate` cli
#
# See also: https://bugreports.qt.io/browse/QTBUG-31860 (reported 8 years ago)
#           https://stackoverflow.com/a/60848939
#

set(APD_TRANSLATION_LOCALES bg_BG de_DE fr_FR it_IT ja_JP ko_KR pt_BR pt_PT ru_RU tr_TR uk_UA zh_CN zh_TW)
set(APD_TS_DIR "${CMAKE_SOURCE_DIR}/Source/Resource/Translation/")
set(APD_QM_DIR "${APD_BINARY_OUT_DIR}/translations/")

set(APD_TS_FILES ${APD_TRANSLATION_LOCALES})
list(TRANSFORM APD_TS_FILES PREPEND "${APD_TS_DIR}/apd_")
list(TRANSFORM APD_TS_FILES APPEND ".ts")

set_source_files_properties(${APD_TS_FILES} PROPERTIES OUTPUT_LOCATION ${APD_QM_DIR})

#
# Create or update .ts files
#

add_custom_target(
    APD_CREATE_UPDATE_TS
    COMMAND $<TARGET_FILE:Qt6::lupdate> -recursive "${CMAKE_SOURCE_DIR}/Source/"
                                            -ts ${APD_TS_FILES}
                                            -no-obsolete
                                            -locations none
    WORKING_DIRECTORY ${APD_TS_DIR}
)

#
# Add .ts files as .qm files
#

qt_add_translation(
    APD_QM_FILES
    ${APD_TS_FILES}
)

# One owner for the generated translations, also when building a test target on its own.
add_custom_target(apd_translations DEPENDS ${APD_QM_FILES})

##################################################

add_library(apd_support STATIC ${APD_SUPPORT_CODE_FILES})

add_library(apd_domain STATIC ${APD_DOMAIN_CODE_FILES})

add_library(apd_core STATIC ${APD_CORE_CODE_FILES})

add_library(apd_presentation STATIC ${APD_PRESENTATION_CODE_FILES})

add_library(apd_gui STATIC ${APD_GUI_CODE_FILES})

set(
    APD_INTERNAL_TARGETS
    apd_support
    apd_domain
    apd_core
    apd_presentation
    apd_gui
)

set_target_properties(${APD_INTERNAL_TARGETS} SingleApplication PROPERTIES DEBUG_POSTFIX "d")

foreach (APD_TARGET ${APD_INTERNAL_TARGETS})
    target_include_directories(${APD_TARGET} PUBLIC "${PROJECT_BINARY_DIR}/Source")
endforeach()

target_include_directories(
    apd_gui PUBLIC

    "${PROJECT_BINARY_DIR}/Source"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/apd_gui_autogen/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/apd_gui_autogen/include_$<CONFIG>>"
)

target_link_libraries(
    apd_support

    PUBLIC
    Qt6::Core
    Qt6::Gui
    spdlog::spdlog
    cxxopts::cxxopts
    magic_enum::magic_enum
    Boost::${APD_STACKTRACE_COMPONENT}
)

target_link_libraries(
    apd_domain

    PUBLIC
    apd_support
)

target_link_libraries(
    apd_core

    PUBLIC
    apd_domain
    Qt6::Multimedia
    cpr::cpr
    nlohmann_json::nlohmann_json
    magic_enum::magic_enum
    Boost::pfr
)

if (WIN32)
    target_link_libraries(apd_core PRIVATE Ole32 Uuid Ksuser)
endif()

target_link_libraries(
    apd_presentation

    PUBLIC
    apd_domain
)

target_link_libraries(
    apd_gui

    PUBLIC
    apd_core
    apd_presentation
    Qt6::Widgets
    Qt6::Svg
)

if (WIN32)
    target_link_libraries(apd_gui PRIVATE dwmapi)
endif()

add_executable(
    ${PROJECT_NAME} ${ADD_EXECUTABLE_ARG}

    ${APD_APP_CODE_FILES}
    ${APD_QRC_FILE}
)

add_dependencies(${PROJECT_NAME} apd_translations)

foreach (APD_TARGET ${APD_INTERNAL_TARGETS} ${PROJECT_NAME})
    set(APD_HEADER_DEFINITION_VISIBILITY PRIVATE)
    if (APD_TARGET IN_LIST APD_INTERNAL_TARGETS)
        set(APD_HEADER_DEFINITION_VISIBILITY PUBLIC)
    endif()

    target_compile_definitions(
        ${APD_TARGET} ${APD_HEADER_DEFINITION_VISIBILITY}

        $<$<CONFIG:Debug>:APD_DEBUG>
    )
    target_compile_definitions(${APD_TARGET} PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

    if (WIN32)
        target_compile_definitions(
            ${APD_TARGET} ${APD_HEADER_DEFINITION_VISIBILITY}
            APD_OS_WIN
            WIN32_LEAN_AND_MEAN
            NOMINMAX
        )
    endif()
    if (MSVC)
        target_compile_definitions(${APD_TARGET} PRIVATE APD_MSVC)
    endif()
    if (APD_ENABLE_CONSOLE)
        target_compile_definitions(
            ${APD_TARGET} ${APD_HEADER_DEFINITION_VISIBILITY} APD_ENABLE_CONSOLE
        )
    endif()
    if (APD_BUILD_GIT_HASH)
        target_compile_definitions(${APD_TARGET} PRIVATE APD_BUILD_GIT_HASH="${APD_BUILD_GIT_HASH}")
    endif()
endforeach()

target_include_directories(${PROJECT_NAME} PRIVATE "${PROJECT_BINARY_DIR}/Source")

##################################################

if (MSVC)
    if (APD_QT_DEPLOY)
        add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND $<TARGET_FILE:Qt6::windeployqt>
                    --no-compiler-runtime
                    "${APD_BINARY_OUT_DIR}/${PROJECT_NAME}.exe"
            COMMENT "Deploying Qt..."
        )
        foreach(runtime_library IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
            add_custom_command(
                TARGET ${PROJECT_NAME}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${runtime_library}"
                        "${APD_BINARY_OUT_DIR}"
                COMMENT "Deploying MSVC runtime ${runtime_library}"
            )
        endforeach()
    endif()
endif()

add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APD_BINARY_OUT_DIR}/licenses"
    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Resource/Font/Inter/LICENSE.txt"
        "${APD_BINARY_OUT_DIR}/licenses/Inter.txt"
    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/Resource/Font/NotoSansTC/LICENSE.txt"
        "${APD_BINARY_OUT_DIR}/licenses/NotoSansTC.txt"
    COMMENT "Deploying third-party font licenses"
)

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
        set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY ${PROJECT_NAME})
        # A custom migration hook removes an existing Win32 release before registering x64.
        set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL OFF)
        set(CPACK_NSIS_URL_INFO_ABOUT ${CMAKE_PROJECT_HOMEPAGE_URL})
        set(CPACK_NSIS_EXECUTABLES_DIRECTORY "./")
        set(CPACK_NSIS_MUI_FINISHPAGE_RUN "./${PROJECT_NAME}.exe")
        set(CPACK_NSIS_BRANDING_TEXT "${PROJECT_NAME} installer")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/CMake/InstallerMigration.nsh.in"
            "${CMAKE_CURRENT_BINARY_DIR}/InstallerMigration.nsh"
            @ONLY
        )
        file(READ "${CMAKE_CURRENT_BINARY_DIR}/InstallerMigration.nsh" APD_NSIS_MIGRATION_COMMANDS)
        # CPack serializes this value into CPackConfig.cmake before expanding the NSIS template.
        string(REPLACE "\\" "\\\\" APD_NSIS_MIGRATION_COMMANDS "${APD_NSIS_MIGRATION_COMMANDS}")
        string(REPLACE "\"" "\\\"" APD_NSIS_MIGRATION_COMMANDS "${APD_NSIS_MIGRATION_COMMANDS}")
        string(REPLACE "$" "\\$" APD_NSIS_MIGRATION_COMMANDS "${APD_NSIS_MIGRATION_COMMANDS}")
        set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS "${APD_NSIS_MIGRATION_COMMANDS}")
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
            "SetRegView 64\nDelete \\\"$DESKTOP\\\\${SHORTCUT_NAME}.lnk\\\""
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
        # Release CI configures with APD_BUILD_TESTS=ON. Keep every automated-test, simulator,
        # mock, harness, fixture, and validation artifact out of production packages.
        PATTERN "*Test*" EXCLUDE
        PATTERN "*test*" EXCLUDE
        PATTERN "*Simulator*" EXCLUDE
        PATTERN "*simulator*" EXCLUDE
        PATTERN "*Simulation*" EXCLUDE
        PATTERN "*simulation*" EXCLUDE
        PATTERN "*Mock*" EXCLUDE
        PATTERN "*mock*" EXCLUDE
        PATTERN "*Fixture*" EXCLUDE
        PATTERN "*fixture*" EXCLUDE
        PATTERN "*Harness*" EXCLUDE
        PATTERN "*harness*" EXCLUDE
        PATTERN "*Validation*" EXCLUDE
        PATTERN "*validation*" EXCLUDE
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

##################################################

target_link_libraries(
    ${PROJECT_NAME}

    PRIVATE
    apd_gui
    SingleApplication::SingleApplication
)

##################################################
# Tests
#

if (APD_BUILD_TESTS)
    include(CTest)
    add_subdirectory(Tests)
endif()

##################################################

#
# Use PDB file name as alternate PDB path, it is used to make stacktrace work
# See https://github.com/boostorg/stacktrace/issues/55
#
if (MSVC)
    apply_pdbaltpath_pdb_for_all_targets()
endif()
