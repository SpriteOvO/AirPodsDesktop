add_executable(
    AirPodsDomainTests

    AirPodsDomainTests.cpp
)

target_link_libraries(
    AirPodsDomainTests

    PRIVATE
    apd_core
    apd_presentation
    Qt6::Test
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
    Qt6::Test
    Qt6::Widgets
)

target_include_directories(QuickConnectTests PRIVATE "${CMAKE_SOURCE_DIR}")

add_test(NAME QuickConnectTests COMMAND QuickConnectTests)

add_executable(AnimationViewTests AnimationViewTests.cpp)

target_link_libraries(
    AnimationViewTests

    PRIVATE
    apd_gui
    Qt6::Multimedia
    Qt6::Test
    Qt6::Widgets
)

target_include_directories(AnimationViewTests PRIVATE "${CMAKE_SOURCE_DIR}")
target_compile_definitions(
    AnimationViewTests
    PRIVATE
    APD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    APD_BINARY_DIR="${CMAKE_BINARY_DIR}"
)

add_test(NAME AnimationViewTests COMMAND AnimationViewTests)

if (APD_OS_LINUX)
    add_executable(NoiseControlTests NoiseControlTests.cpp)
    target_link_libraries(NoiseControlTests PRIVATE apd_core Qt6::Test)
    target_include_directories(NoiseControlTests PRIVATE "${CMAKE_SOURCE_DIR}")
    add_test(NAME NoiseControlTests COMMAND NoiseControlTests)

    add_executable(GlobalMediaTests GlobalMediaTests.cpp)

    target_link_libraries(
        GlobalMediaTests

        PRIVATE
        apd_core
        Qt6::Test
    )

    target_include_directories(GlobalMediaTests PRIVATE "${CMAKE_SOURCE_DIR}")

    add_test(NAME GlobalMediaTests COMMAND GlobalMediaTests)
endif()

add_executable(UiRenderingTests UiRenderingTests.cpp ${CMAKE_SOURCE_DIR}/Source/Resource/Resource.qrc)

target_link_libraries(
    UiRenderingTests

    PRIVATE
    apd_gui
    Qt6::Test
    Qt6::Widgets
)

target_include_directories(UiRenderingTests PRIVATE "${CMAKE_SOURCE_DIR}")
target_compile_definitions(UiRenderingTests PRIVATE APD_BINARY_DIR="${CMAKE_BINARY_DIR}")
add_dependencies(UiRenderingTests apd_translations)

add_test(NAME UiRenderingTests COMMAND UiRenderingTests)
foreach(scale_factor 1 1.25 1.5 2)
    string(REPLACE "." "_" scale_suffix "${scale_factor}")
    add_test(
        NAME MainWindowRenderingScale${scale_suffix}
        COMMAND UiRenderingTests MainWindowElidesLongDeviceNames
    )
    set_tests_properties(
        MainWindowRenderingScale${scale_suffix}
        PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=${scale_factor}"
    )
    add_test(
        NAME AirPods5RenderingScale${scale_suffix}
        COMMAND UiRenderingTests MainWindowRendersAirPods5
    )
    set_tests_properties(
        AirPods5RenderingScale${scale_suffix}
        PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=${scale_factor}"
    )
endforeach()

set(APD_TEST_TARGETS AirPodsDomainTests QuickConnectTests AnimationViewTests UiRenderingTests)
if (APD_OS_LINUX)
    list(APPEND APD_TEST_TARGETS GlobalMediaTests NoiseControlTests)
endif()

foreach(test_target ${APD_TEST_TARGETS})
    add_custom_command(
        TARGET ${test_target}
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Test>
            $<TARGET_FILE_DIR:${test_target}>
        COMMENT "Deploying Qt Test runtime for ${test_target}"
    )
endforeach()
