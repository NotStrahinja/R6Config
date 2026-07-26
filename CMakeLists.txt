cmake_minimum_required(VERSION 3.16)
project(R6Config LANGUAGES CXX C)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

include(FetchContent)

FetchContent_Declare(
    webview
    GIT_REPOSITORY https://github.com/webview/webview
    GIT_TAG 0.12.0)
FetchContent_MakeAvailable(webview)

find_package(ZLIB REQUIRED)

add_executable(R6Config WIN32)
target_sources(R6Config PRIVATE main.c minIni.c app.rc)
target_link_libraries(R6Config PRIVATE webview::core_static ZLIB::ZLIB)
