# Copies the Qt runtime libraries that the executable and the selected plugins actually need into
# OUTPUT_DIR, from where install(DIRECTORY) adds them to the package.
#
# Copying the whole Qt lib directory instead would ship unrelated modules whose own dependencies may
# not even be part of the Qt installation (Quick3D is a common example), which both bloats the
# package and breaks CPack's dependency scanning. The script runs after linking so it can resolve
# the dependencies of the real executable and of the plugins it will load.
#
# Invoked as: cmake -DAPD_QT_LIB_DIR=... -DAPD_QT_PLUGINS_DIR=... -DAPD_BINARY_OUT_DIR=...
#                  -DOUTPUT_DIR=... -P BundleLinuxRuntime.cmake

cmake_minimum_required(VERSION 3.21)

set(_apd_plugin_files)
foreach(
    _apd_plugin_dir
    IN ITEMS
        platforms
        xcbglintegrations
        iconengines
        imageformats
        multimedia
        platforminputcontexts
        platformthemes
        tls
        networkinformation
        wayland-decoration-client
        wayland-graphics-integration-client
        wayland-shell-integration
)
    file(GLOB _apd_dir_plugins "${APD_QT_PLUGINS_DIR}/${_apd_plugin_dir}/*.so")
    list(APPEND _apd_plugin_files ${_apd_dir_plugins})
endforeach()

file(
    GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${APD_BINARY_OUT_DIR}/AirPodsDesktop"
    LIBRARIES ${_apd_plugin_files}
    DIRECTORIES "${APD_QT_LIB_DIR}" "${APD_BINARY_OUT_DIR}"
    RESOLVED_DEPENDENCIES_VAR _apd_resolved_dependencies
    UNRESOLVED_DEPENDENCIES_VAR _apd_unresolved_dependencies
)

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

set(_apd_copied_real_paths)
set(_apd_bundled_names)
foreach(_apd_dependency IN LISTS _apd_resolved_dependencies)
    file(REAL_PATH "${_apd_dependency}" _apd_real_path)

    # Only libraries from the Qt installation belong to the bundle. System libraries stay package
    # dependencies, and the fetched third-party libraries are installed by their own rules.
    string(FIND "${_apd_real_path}" "${APD_QT_LIB_DIR}/" _apd_in_qt)
    if (NOT _apd_in_qt EQUAL 0)
        continue()
    endif()

    list(FIND _apd_copied_real_paths "${_apd_real_path}" _apd_already_copied)
    if (NOT _apd_already_copied EQUAL -1)
        continue()
    endif()
    list(APPEND _apd_copied_real_paths "${_apd_real_path}")

    # The loader asks for the SONAME (for example libQt6Core.so.6). Copy the library under the name
    # it was resolved with, dereferencing the versioned symlink.
    get_filename_component(_apd_dependency_name "${_apd_dependency}" NAME)
    list(APPEND _apd_bundled_names "${_apd_dependency_name}")
    file(INSTALL "${_apd_real_path}" DESTINATION "${OUTPUT_DIR}" RENAME "${_apd_dependency_name}")
endforeach()

# Drop libraries bundled by an earlier run that the current build no longer needs.
file(GLOB _apd_existing_files "${OUTPUT_DIR}/*")
foreach(_apd_existing_file IN LISTS _apd_existing_files)
    get_filename_component(_apd_existing_name "${_apd_existing_file}" NAME)
    if (NOT _apd_existing_name IN_LIST _apd_bundled_names)
        file(REMOVE "${_apd_existing_file}")
    endif()
endforeach()

if (_apd_unresolved_dependencies)
    message(
        STATUS "Bundled Qt runtime left these dependencies to the system: "
               "${_apd_unresolved_dependencies}"
    )
endif()
