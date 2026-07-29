cmake_minimum_required(VERSION 3.21)
project(osv-qt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Repo root, for reusing src/ and vendor/ untouched.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Qml Quick ShaderTools)
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
    unlock_controller.cpp
)
target_compile_definitions(osv-qt PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
target_include_directories(osv-qt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(osv-qt PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
