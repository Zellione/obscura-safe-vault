cmake_minimum_required(VERSION 3.21)
project(osv-qt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Repo root, for reusing src/ and vendor/ untouched.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Qml Quick ShaderTools)
qt_standard_project_setup()

qt_add_executable(osv-qt
    main.cpp
)
target_compile_definitions(osv-qt PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
target_link_libraries(osv-qt PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
