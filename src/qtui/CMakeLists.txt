cmake_minimum_required(VERSION 3.21)
project(osv-qt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Repo root, for reusing src/ and vendor/ untouched.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick ShaderTools)
qt_standard_project_setup()

set(CODEC_PREFIX "${REPO_ROOT}/vendor/codecs-prefix")

file(GLOB OSV_CRYPTO_SRC "${REPO_ROOT}/src/crypto/*.cpp")
file(GLOB OSV_VAULT_SRC  "${REPO_ROOT}/src/vault/*.cpp")

add_library(osv_qt_core STATIC
    ${OSV_CRYPTO_SRC}
    ${OSV_VAULT_SRC}
    # image decode WITHOUT the SDL-coupled worker (decode_worker.* stays out;
    # Qt-side loading uses QThreadPool instead — Task 6)
    ${REPO_ROOT}/src/image/decode.cpp
    ${REPO_ROOT}/src/image/decode_webp.cpp
    ${REPO_ROOT}/src/image/decode_heif.cpp
    ${REPO_ROOT}/src/image/decoder.cpp
    ${REPO_ROOT}/src/image/format_registry.cpp
    ${REPO_ROOT}/src/image/thumbnail.cpp
    ${REPO_ROOT}/src/image/anim_info.cpp
    # deps vault.cpp pulls in
    ${REPO_ROOT}/src/platform/error_log.cpp
    ${REPO_ROOT}/src/platform/paths.cpp
    ${REPO_ROOT}/src/media/video_probe.cpp
    ${REPO_ROOT}/src/ui/advanced_search_model.cpp
    ${REPO_ROOT}/src/ui/gallery_sort.cpp
    ${REPO_ROOT}/src/ui/natural_sort.cpp
    # text input models (Task 3 uses SecureTextInput)
    ${REPO_ROOT}/src/ui/text_input_model.cpp
    ${REPO_ROOT}/src/ui/secure_text_input.cpp
    ${REPO_ROOT}/vendor/monocypher/src/monocypher.c
    # miniz: ZIP reader (same sources as premake builds)
    ${REPO_ROOT}/vendor/miniz/miniz.c
    ${REPO_ROOT}/vendor/miniz/miniz_tdef.c
    ${REPO_ROOT}/vendor/miniz/miniz_tinfl.c
    ${REPO_ROOT}/vendor/miniz/miniz_zip.c
)

# Create osv_qt_core_qt variant: removes SDL and codec library dependencies for Qt build
# This library is used only by osv-qt; other targets use osv_qt_core with SDL.
# NOTE: Image decode uses stubs instead of real codecs due to libheif/libaom's std::thread
# use causing deadlock with Qt Quick Scene Graph initialization (see target_link_libraries).
add_library(osv_qt_core_qt STATIC
    ${OSV_CRYPTO_SRC}
    ${OSV_VAULT_SRC}
    # Image decode WITH STUBS (real codecs excluded; see qt_decode_stubs.cpp)
    ${REPO_ROOT}/src/image/decoder.cpp
    ${REPO_ROOT}/src/image/format_registry.cpp
    qt_decode_stubs.cpp
    # deps vault.cpp pulls in; paths.cpp for normalize_user_path (config_dir overridden by qt_paths.cpp)
    ${REPO_ROOT}/src/platform/error_log.cpp
    ${REPO_ROOT}/src/platform/paths.cpp
    qt_paths.cpp
    ${REPO_ROOT}/src/media/video_probe.cpp
    ${REPO_ROOT}/src/ui/advanced_search_model.cpp
    ${REPO_ROOT}/src/ui/gallery_sort.cpp
    ${REPO_ROOT}/src/ui/natural_sort.cpp
    # text input models (Task 3 uses SecureTextInput)
    ${REPO_ROOT}/src/ui/text_input_model.cpp
    ${REPO_ROOT}/src/ui/secure_text_input.cpp
    ${REPO_ROOT}/vendor/monocypher/src/monocypher.c
    # miniz: ZIP reader (same sources as premake builds)
    ${REPO_ROOT}/vendor/miniz/miniz.c
    ${REPO_ROOT}/vendor/miniz/miniz_tdef.c
    ${REPO_ROOT}/vendor/miniz/miniz_tinfl.c
    ${REPO_ROOT}/vendor/miniz/miniz_zip.c
)

target_include_directories(osv_qt_core_qt PUBLIC
    ${REPO_ROOT}/src
    ${REPO_ROOT}/vendor/monocypher/src
    ${REPO_ROOT}/vendor/miniz
    ${REPO_ROOT}/vendor/miniz-shim
    ${REPO_ROOT}/vendor/stb
    ${REPO_ROOT}/vendor/SDL3/include
    ${CODEC_PREFIX}/include
)
target_compile_definitions(osv_qt_core_qt PRIVATE
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES
    _POSIX_C_SOURCE=200809L
    PLATFORM_PATHS_NO_SDL  # Skip SDL-based config_dir() in paths.cpp; use Qt version from qt_paths.cpp
)
set_source_files_properties(
    ${REPO_ROOT}/vendor/miniz/miniz.c
    ${REPO_ROOT}/vendor/miniz/miniz_tdef.c
    ${REPO_ROOT}/vendor/miniz/miniz_tinfl.c
    ${REPO_ROOT}/vendor/miniz/miniz_zip.c
    PROPERTIES
    COMPILE_FLAGS "-w"
)
# ROOT CAUSE ANALYSIS:
# libheif and libaom use std::thread for parallel image decoding. When osv_qt_core_qt is
# linked with static codec libraries, their thread initialization code runs at library load
# time. This conflicts with Qt Quick Scene Graph's thread initialization, causing deadlock.
#
# WORKAROUND: Exclude codec libraries from osv_qt_core_qt for the Qt build. Image decoding
# will be disabled for Qt builds until we implement lazy loading of codec libraries (future).
# This unblocks M2-M7 development phases which don't require image decoding in the real flow
# (only in --selftest-image which uses a workaround).
#
# For non-Qt builds (osv_qt_core, osv-qt-mkvault, osv_qt_core_smoke), codec libraries
# are linked through osv_qt_core which includes the full paths.cpp with SDL.
target_link_libraries(osv_qt_core_qt PUBLIC
    Qt6::Core  # For QStandardPaths in qt_paths.cpp
    pthread
    dl
    m
)
target_include_directories(osv_qt_core PUBLIC
    ${REPO_ROOT}/src
    ${REPO_ROOT}/vendor/monocypher/src
    ${REPO_ROOT}/vendor/miniz
    ${REPO_ROOT}/vendor/miniz-shim
    ${REPO_ROOT}/vendor/stb
    ${REPO_ROOT}/vendor/SDL3/include
    ${CODEC_PREFIX}/include
)
target_compile_definitions(osv_qt_core PRIVATE
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES
    _POSIX_C_SOURCE=200809L
)
# Silence warnings in vendored miniz
set_source_files_properties(
    ${REPO_ROOT}/vendor/miniz/miniz.c
    ${REPO_ROOT}/vendor/miniz/miniz_tdef.c
    ${REPO_ROOT}/vendor/miniz/miniz_tinfl.c
    ${REPO_ROOT}/vendor/miniz/miniz_zip.c
    PROPERTIES
    COMPILE_FLAGS "-w"
)
# Same single-pass archive order premake uses: heif → de265 → aom → webpdemux → webp → sharpyuv
target_link_libraries(osv_qt_core PUBLIC
    ${REPO_ROOT}/vendor/SDL3/build/libSDL3.a
    ${CODEC_PREFIX}/lib/libheif.a
    ${CODEC_PREFIX}/lib/libde265.a
    ${CODEC_PREFIX}/lib/libaom.a
    ${CODEC_PREFIX}/lib/libwebpdemux.a
    ${CODEC_PREFIX}/lib/libwebp.a
    ${CODEC_PREFIX}/lib/libsharpyuv.a
    pthread
    dl
    m
)

add_executable(osv_qt_core_smoke tests/core_smoke.cpp)
target_link_libraries(osv_qt_core_smoke PRIVATE osv_qt_core)

add_executable(osv_qt_secure_field_test tests/secure_field_test.cpp secure_text_field.cpp)
target_link_libraries(osv_qt_secure_field_test PRIVATE osv_qt_core Qt6::Gui Qt6::Quick)
target_include_directories(osv_qt_secure_field_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv-qt-mkvault tools/mkvault.cpp)
target_link_libraries(osv-qt-mkvault PRIVATE osv_qt_core)

qt_add_executable(osv-qt
    main.cpp
    secure_text_field.cpp
    secure_image_item.cpp
    unlock_controller.cpp
)

# Shaders: compile to .qsb and embed as resources
qt6_add_shaders(osv-qt "osv_qt_shaders"
    PREFIX "/osvqt"
    FILES shaders/texquad.vert shaders/texquad.frag
)

target_compile_definitions(osv-qt PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
target_include_directories(osv-qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
# Use osv_qt_core_qt instead of osv_qt_core to avoid SDL3 static linkage conflicts
# with Qt's Wayland platform plugin. qt_paths.cpp provides config_dir() via Qt.
target_link_libraries(osv-qt PRIVATE osv_qt_core_qt Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick)
