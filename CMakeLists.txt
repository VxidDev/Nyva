cmake_minimum_required(VERSION 3.15)

project(nyva
    VERSION 0.0.1
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(include)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    src/*.cpp
)

add_executable(nyva ${SOURCES})

if (MSVC)
    target_compile_options(nyva PRIVATE /W4)
else()
    target_compile_options(nyva PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

target_link_libraries(nyva PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)