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

add_executable(AnimationViewTests AnimationViewTests.cpp)

target_link_libraries(
    AnimationViewTests

    PRIVATE
    apd_gui
    Qt5::Multimedia
    Qt5::Test
    Qt5::Widgets
)

target_include_directories(AnimationViewTests PRIVATE "${CMAKE_SOURCE_DIR}")
target_compile_definitions(
    AnimationViewTests
    PRIVATE
    APD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    APD_BINARY_DIR="${CMAKE_BINARY_DIR}"
)

add_test(NAME AnimationViewTests COMMAND AnimationViewTests)

add_executable(UiRenderingTests UiRenderingTests.cpp ${CMAKE_SOURCE_DIR}/Source/Resource/Resource.qrc)

target_link_libraries(
    UiRenderingTests

    PRIVATE
    apd_gui
    Qt5::Test
    Qt5::Widgets
)

target_include_directories(UiRenderingTests PRIVATE "${CMAKE_SOURCE_DIR}")
target_compile_definitions(UiRenderingTests PRIVATE APD_BINARY_DIR="${CMAKE_BINARY_DIR}")

add_test(NAME UiRenderingTests COMMAND UiRenderingTests)

# Interactive harness: explicitly built and never run by CTest or shipped to users.
add_executable(
    UpdateSimulator WIN32 EXCLUDE_FROM_ALL
    UpdateSimulator.cpp
    ${CMAKE_SOURCE_DIR}/Source/Resource/Resource.qrc
    ${APD_QM_FILES}
)
target_link_libraries(UpdateSimulator PRIVATE apd_gui Qt5::Widgets)
target_include_directories(UpdateSimulator PRIVATE "${CMAKE_SOURCE_DIR}")
if(MSVC)
    target_compile_options(UpdateSimulator PRIVATE /utf-8)
endif()
add_custom_command(
    TARGET UpdateSimulator POST_BUILD
    COMMAND "${Qt5_DIR}/../../../bin/windeployqt" "$<TARGET_FILE:UpdateSimulator>"
    COMMENT "Deploying Qt runtime for the interactive update simulator"
)

foreach(test_target AirPodsDomainTests QuickConnectTests AnimationViewTests UiRenderingTests)
    add_custom_command(
        TARGET ${test_target}
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Test>
            $<TARGET_FILE_DIR:${test_target}>
        COMMENT "Deploying Qt Test runtime for ${test_target}"
    )
endforeach()
