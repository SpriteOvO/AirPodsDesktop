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

add_executable(UiRenderingTests UiRenderingTests.cpp)

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

foreach(test_target AirPodsDomainTests QuickConnectTests UiRenderingTests)
    add_custom_command(
        TARGET ${test_target}
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Test>
            $<TARGET_FILE_DIR:${test_target}>
        COMMENT "Deploying Qt Test runtime for ${test_target}"
    )
endforeach()
