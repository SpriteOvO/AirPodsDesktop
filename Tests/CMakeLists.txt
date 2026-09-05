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
foreach(scale_factor 1 1.25 1.5)
    string(REPLACE "." "_" scale_suffix "${scale_factor}")
    add_test(
        NAME MainWindowRenderingScale${scale_suffix}
        COMMAND UiRenderingTests MainWindowElidesLongDeviceNames
    )
    set_tests_properties(
        MainWindowRenderingScale${scale_suffix}
        PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=${scale_factor}"
    )
endforeach()

foreach(test_target AirPodsDomainTests QuickConnectTests AnimationViewTests UiRenderingTests)
    add_custom_command(
        TARGET ${test_target}
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Test>
            $<TARGET_FILE_DIR:${test_target}>
        COMMENT "Deploying Qt Test runtime for ${test_target}"
    )
endforeach()
