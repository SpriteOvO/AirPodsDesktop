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

add_test(NAME AirPodsDomainTests COMMAND AirPodsDomainTests)
