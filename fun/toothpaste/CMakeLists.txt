cmake_minimum_required(VERSION 3.16)
project(toothpaste VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)

# ---- Style plugin (.so loaded by Qt's style subsystem) ----
add_library(toothpaste MODULE
    src/toothpaste.cpp
    src/toothpasteplugin.cpp
)
target_include_directories(toothpaste PRIVATE src)
target_link_libraries(toothpaste PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
set_target_properties(toothpaste PROPERTIES
    PREFIX ""
    OUTPUT_NAME "toothpaste"
)

install(TARGETS toothpaste
    LIBRARY DESTINATION "$ENV{HOME}/.local/lib/qt5/plugins/styles"
)

# ---- Test app (links style source directly — no plugin loader needed) ----
add_executable(test_toothpaste
    test/test_toothpaste.cpp
    src/toothpaste.cpp
    src/toothpastedrips.cpp
)
target_include_directories(test_toothpaste PRIVATE src)
target_link_libraries(test_toothpaste PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
