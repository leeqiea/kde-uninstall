cmake_minimum_required(VERSION 3.16)
project(KDE_uninstall LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(REMOVAL_SOURCES
    src/command.cpp
    src/desktop_file.cpp
    src/package_manager.cpp
    src/paths.cpp
    src/string_utils.cpp
)

add_executable(kde-uninstall
    main.cpp
    src/app.cpp
    src/integration.cpp
    src/ui.cpp
    ${REMOVAL_SOURCES}
)

target_include_directories(kde-uninstall PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kde-uninstall PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS kde-uninstall RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(removal-tests tests/removal_tests.cpp ${REMOVAL_SOURCES})
    target_include_directories(removal-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(removal-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME removal-tests COMMAND removal-tests)

    # An isolated /opt allows testing actual deletion without touching installed apps.
    find_program(BWRAP_EXECUTABLE bwrap)
    if(BWRAP_EXECUTABLE)
        add_test(NAME opt-removal-tests COMMAND ${BWRAP_EXECUTABLE}
            --ro-bind / / --unshare-user --uid 0 --gid 0
            --tmpfs /opt --tmpfs /tmp --tmpfs /usr/local/bin
            --tmpfs /usr/share/applications --dev /dev --proc /proc
            --setenv KDE_UNINSTALL_TEST_SANDBOX 1
            $<TARGET_FILE:removal-tests> --opt-sandbox)
    endif()
endif()
