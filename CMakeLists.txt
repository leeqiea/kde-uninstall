cmake_minimum_required(VERSION 3.16)
project(KDE_uninstall LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(kde-uninstall
    main.cpp
    src/app.cpp
    src/command.cpp
    src/desktop_file.cpp
    src/integration.cpp
    src/package_manager.cpp
    src/paths.cpp
    src/string_utils.cpp
    src/ui.cpp
)

target_include_directories(kde-uninstall PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kde-uninstall PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS kde-uninstall RUNTIME DESTINATION bin)
