cmake_minimum_required(VERSION 3.21)
project(osv-qt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Repo root, for reusing src/ and vendor/ untouched.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick ShaderTools Test Widgets)
qt_standard_project_setup()

set(CODEC_PREFIX "${REPO_ROOT}/vendor/codecs-prefix")

# ---------------------------------------------------------------------------
# Locate a static lib in the codec staging prefix. CMake's default static-lib
# naming differs by toolchain: GCC/Clang follow the Unix convention
# lib<name>.a, MSVC has no "lib" prefix and uses <name>.lib. None of the
# vendored codecs override CMAKE_STATIC_LIBRARY_PREFIX/SUFFIX — they only set
# OUTPUT_NAME — so the platform default applies, and a few keep a literal
# "lib" inside OUTPUT_NAME (libde265/libwebp/libsharpyuv), hence the third
# candidate. Mirrors premake5.lua's cmake_static_lib(); see its comment for
# why checking only the Unix name silently no-ops the feature on Windows.
#
# Fails loudly at configure time rather than letting the name fall through to
# the generator: a bare missing path shows up as an inscrutable
# "ninja: error: '<...>.a', needed by '<...>.exe', missing and no known rule
# to make it" only once the build starts.
# ---------------------------------------------------------------------------
function(osv_require_static_lib out name)
    foreach(candidate
            "${CODEC_PREFIX}/lib/lib${name}.a"      # Linux / GCC / Clang
            "${CODEC_PREFIX}/lib/${name}.lib"       # Windows / MSVC
            "${CODEC_PREFIX}/lib/lib${name}.lib")   # Windows, OUTPUT_NAME keeps "lib"
        if(EXISTS "${candidate}")
            set(${out} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR
        "Vendored static lib '${name}' not found in ${CODEC_PREFIX}/lib "
        "(tried lib${name}.a, ${name}.lib, lib${name}.lib). "
        "Run scripts/build_codecs.sh (Linux) or scripts\\build_codecs.bat (Windows) first.")
endfunction()

# googletest — vendored submodule (pinned v1.17.0), not the system package.
# Seven osv_qt_*_test targets use gtest; every other vendored dependency is a
# submodule, and the Windows runner has no system gtest at all, so vendoring is
# what makes the suite buildable on both legs while keeping the build hermetic.
# EXCLUDE_FROM_ALL: only the test targets that link it pull it into the build.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # match MSVC's default /MD
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)            # gmock is unused
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${REPO_ROOT}/vendor/googletest
                 ${CMAKE_BINARY_DIR}/vendor-googletest EXCLUDE_FROM_ALL)

file(GLOB OSV_CRYPTO_SRC "${REPO_ROOT}/src/crypto/*.cpp")
file(GLOB OSV_VAULT_SRC  "${REPO_ROOT}/src/vault/*.cpp")
file(GLOB OSV_MEDIA_SRC  "${REPO_ROOT}/src/media/*.cpp")

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
    ${REPO_ROOT}/src/platform/vault_registry.cpp
    ${REPO_ROOT}/src/platform/theme_pref.cpp
    # media: video decode + demux (Task 9 adds video playback)
    ${OSV_MEDIA_SRC}
    ${REPO_ROOT}/src/ui/advanced_search_model.cpp
    ${REPO_ROOT}/src/ui/search_model.cpp
    ${REPO_ROOT}/src/ui/gallery_sort.cpp
    ${REPO_ROOT}/src/ui/natural_sort.cpp
    ${REPO_ROOT}/src/ui/playback_model.cpp
    ${REPO_ROOT}/src/ui/slideshow_model.cpp
    ${REPO_ROOT}/src/ui/nav_model.cpp
    ${REPO_ROOT}/src/ui/settings_model.cpp
    ${REPO_ROOT}/src/ui/selection_model.cpp
    ${REPO_ROOT}/src/ui/tag_inherit.cpp
    ${REPO_ROOT}/src/ui/tag_suggest.cpp
    ${REPO_ROOT}/src/ui/tag_category.cpp
    ${REPO_ROOT}/src/ui/tag_overview_model.cpp
    ${REPO_ROOT}/src/ui/tag_json_parse.cpp
    ${REPO_ROOT}/src/ui/tag_dict_import.cpp
    ${REPO_ROOT}/src/ui/tag_list_parse.cpp
    ${REPO_ROOT}/src/ui/detail_model.cpp
    ${REPO_ROOT}/src/ui/detail_layout.cpp
    ${REPO_ROOT}/src/ui/delete_summary.cpp
    ${REPO_ROOT}/src/ui/text_metrics.cpp
    ${REPO_ROOT}/src/ui/gallery_cover.cpp
    ${REPO_ROOT}/src/ui/cover_cache.cpp
    ${REPO_ROOT}/src/ui/child_counts.cpp
    # text input models (Task 3 uses SecureTextInput)
    ${REPO_ROOT}/src/ui/text_input_model.cpp
    ${REPO_ROOT}/src/ui/secure_text_input.cpp
    ${REPO_ROOT}/src/ui/passphrase.cpp
    ${REPO_ROOT}/src/ui/unlock_logic.cpp
    # file operations (Task F3: FileOpController)
    ${REPO_ROOT}/src/ui/file_op_job.cpp
    ${REPO_ROOT}/src/ui/export.cpp
    ${REPO_ROOT}/src/ui/meta_format.cpp
    # album rebind (Phase 56: WS3 Finding 5 - vault refresh re-resolution)
    ${REPO_ROOT}/src/ui/album_rebind.cpp
    # import operations (Task WS4: ImportController) — include all UI support modules
    # to avoid linker errors from cross-dependencies
    ${REPO_ROOT}/src/ui/import_queue.cpp
    ${REPO_ROOT}/src/ui/import_model.cpp
    ${REPO_ROOT}/src/ui/import_common.cpp
    ${REPO_ROOT}/src/ui/archive_import.cpp
    ${REPO_ROOT}/src/ui/archive_kind.cpp
    ${REPO_ROOT}/src/ui/archive_reader.cpp
    ${REPO_ROOT}/src/ui/zip_import.cpp
    ${REPO_ROOT}/src/ui/zip_plan.cpp
    ${REPO_ROOT}/src/ui/zip_encoding.cpp
    ${REPO_ROOT}/src/ui/spanned_zip.cpp
    ${REPO_ROOT}/src/ui/recursive_import.cpp
    ${REPO_ROOT}/src/ui/recursive_exec.cpp
    ${REPO_ROOT}/src/ui/recursive_hooks.cpp
    ${REPO_ROOT}/src/ui/volume_import.cpp
    ${REPO_ROOT}/src/ui/volume_set.cpp
    ${REPO_ROOT}/src/ui/tag_dict_import.cpp
    ${REPO_ROOT}/src/ui/meta_json.cpp
    ${REPO_ROOT}/src/ui/import_status_row.cpp
    ${REPO_ROOT}/src/ui/import_status_screen.cpp
    ${REPO_ROOT}/src/ui/folder_scan.cpp
    # theme system (Task 8)
    ${REPO_ROOT}/src/gfx/theme.cpp
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
    ${REPO_ROOT}/vendor/json/include
    ${REPO_ROOT}/vendor/SDL3/include
    ${REPO_ROOT}/vendor/json/single_include
    ${CODEC_PREFIX}/include
)
target_compile_definitions(osv_qt_core PUBLIC
    OSV_VENDORED_AV
    OSV_VENDORED_ARCHIVE
    # LIBARCHIVE_STATIC / LIBHEIF_STATIC_BUILD: both headers default their API
    # declarations to __declspec(dllimport) on MSVC unless told the link is
    # static, which makes the linker demand an import lib for symbols that only
    # exist in the static archive (unresolved __imp_archive_* / __imp_heif_*).
    # No-ops on every other platform — premake5.lua defines both the same way.
    LIBARCHIVE_STATIC
    LIBHEIF_STATIC_BUILD
)
target_compile_definitions(osv_qt_core PRIVATE MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
if(WIN32)
    # Same set premake5.lua defines for the SDL app's Windows build: NOMINMAX
    # so <windows.h>'s min/max macros don't shred std::min/std::max, and
    # WIN32_LEAN_AND_MEAN to keep winsock/ole out of every translation unit.
    target_compile_definitions(osv_qt_core PUBLIC
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_definitions(osv_qt_core PRIVATE _POSIX_C_SOURCE=200809L)
endif()
# Silence warnings in vendored miniz (MSVC spells the "no warnings" flag /w)
if(MSVC)
    set(OSV_NO_WARN_FLAG "/w")
else()
    set(OSV_NO_WARN_FLAG "-w")
endif()
set_source_files_properties(
    ${REPO_ROOT}/vendor/miniz/miniz.c
    ${REPO_ROOT}/vendor/miniz/miniz_tdef.c
    ${REPO_ROOT}/vendor/miniz/miniz_tinfl.c
    ${REPO_ROOT}/vendor/miniz/miniz_zip.c
    PROPERTIES
    COMPILE_FLAGS "${OSV_NO_WARN_FLAG}"
)
# Guard FFmpeg/video support on codec prefix presence. FFmpeg has its own
# (non-CMake) build system that emits lib<name>.a on every platform including
# MSVC — see scripts/build_ffmpeg_windows.sh — so unlike the CMake-built codecs
# below, these names need no per-toolchain lookup.
if(EXISTS "${CODEC_PREFIX}/lib/libavcodec.a")
    target_compile_definitions(osv_qt_core PUBLIC OSV_QT_HAS_AV)
    target_link_libraries(osv_qt_core PUBLIC
        ${CODEC_PREFIX}/lib/libavformat.a
        ${CODEC_PREFIX}/lib/libavcodec.a
        ${CODEC_PREFIX}/lib/libavfilter.a
        ${CODEC_PREFIX}/lib/libswscale.a
        ${CODEC_PREFIX}/lib/libswresample.a
        ${CODEC_PREFIX}/lib/libavutil.a
    )
    # VAAPI shim — Linux only, and only when built (scripts/build_codecs.sh).
    # FFmpeg's hwcontext_vaapi.c references va* symbols that need a real
    # link-time provider; vendor/vaapi-shim supplies them via its own internal
    # dlopen("libva.so.2")/dlsym() forwarding, so nothing here gets a DT_NEEDED
    # on libva. Windows needs no counterpart: hwcontext_d3d11va.c LoadLibrary()s
    # d3d11.dll/dxgi.dll at runtime, so there is no link-time dependency at all.
    # Same existence gating as premake5.lua; linking it unconditionally is what
    # broke the Windows release leg (`libosv_vaapi_shim.a ... missing`).
    # No OSV_HWACCEL_* define here — matching this build's existing behavior,
    # which links the shim to satisfy FFmpeg but leaves our own hwaccel path
    # compiled out.
    if(EXISTS "${CODEC_PREFIX}/lib/libosv_vaapi_shim.a")
        target_link_libraries(osv_qt_core PUBLIC ${CODEC_PREFIX}/lib/libosv_vaapi_shim.a)
    endif()
    # Second aom mention — single-pass ld
    osv_require_static_lib(OSV_LIB_AOM_AV aom)
    target_link_libraries(osv_qt_core PUBLIC ${OSV_LIB_AOM_AV})
endif()

# SDL3's own CMakeLists sets the static target's OUTPUT_NAME to "SDL3-static"
# when the toolchain uses MSVC naming (empty prefix + .lib suffix), to avoid
# colliding with the shared build's import lib, and plain "SDL3" everywhere
# else — so the file is libSDL3.a on Linux and SDL3-static.lib on Windows.
find_library(OSV_SDL3_LIB
    NAMES SDL3-static SDL3
    PATHS "${REPO_ROOT}/vendor/SDL3/build"
    NO_DEFAULT_PATH
    REQUIRED)

foreach(codec archive lzma heif de265 aom webpdemux webp sharpyuv)
    osv_require_static_lib(OSV_LIB_${codec} ${codec})
endforeach()

# zlib: the vendored static target is named "zs" on Windows (its CMakeLists
# appends a static-only suffix there); on Linux the system zlib is linked
# instead, exactly as premake5.lua does.
if(WIN32)
    osv_require_static_lib(OSV_LIB_Z zs)
else()
    set(OSV_LIB_Z z)
endif()

# Same single-pass archive order premake uses: libarchive (depends on liblzma + zlib) → heif → de265 → aom → webpdemux → webp → sharpyuv
target_link_libraries(osv_qt_core PUBLIC
    ${OSV_SDL3_LIB}
    ${OSV_LIB_archive}
    ${OSV_LIB_lzma}
    ${OSV_LIB_heif}
    ${OSV_LIB_de265}
    ${OSV_LIB_aom}
    ${OSV_LIB_webpdemux}
    ${OSV_LIB_webp}
    ${OSV_LIB_sharpyuv}
    ${OSV_LIB_Z}
)

if(WIN32)
    # bcrypt: crypto/random.cpp's BCryptGenRandom. The rest are what a static
    # SDL3 needs. Mirrors premake5.lua's link_platform_extras().
    target_link_libraries(osv_qt_core PUBLIC
        bcrypt winmm imm32 version setupapi ole32 oleaut32
        advapi32 shell32 user32 gdi32 uuid)
else()
    target_link_libraries(osv_qt_core PUBLIC pthread dl m)
endif()

add_executable(osv_qt_core_smoke tests/core_smoke.cpp)
target_link_libraries(osv_qt_core_smoke PRIVATE osv_qt_core)

add_executable(osv_qt_secure_field_test tests/secure_field_test.cpp secure_text_field.cpp)
target_link_libraries(osv_qt_secure_field_test PRIVATE osv_qt_core Qt6::Gui Qt6::Quick)
target_include_directories(osv_qt_secure_field_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_gallery_model_test tests/gallery_model_test.cpp gallery_model.cpp cover_provider.cpp thumb_cache.cpp viewer_controller.cpp secure_image_item.cpp rhi_quad_renderer.cpp theme_palette.cpp status_controller.cpp)
target_link_libraries(osv_qt_gallery_model_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick)
target_include_directories(osv_qt_gallery_model_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_detail_controller_test tests/detail_controller_test.cpp detail_controller.cpp)
target_link_libraries(osv_qt_detail_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick)
target_include_directories(osv_qt_detail_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_file_op_controller_test tests/file_op_controller_test.cpp file_op_controller.cpp)
target_link_libraries(osv_qt_file_op_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_file_op_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_status_controller_test tests/status_controller_test.cpp status_controller.cpp)
target_link_libraries(osv_qt_status_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_status_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_settings_controller_test tests/settings_controller_test.cpp settings_controller.cpp)
target_link_libraries(osv_qt_settings_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_settings_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_help_model_test tests/help_model_test.cpp help_model.cpp)
target_link_libraries(osv_qt_help_model_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_help_model_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_selection_controller_test tests/selection_controller_test.cpp selection_controller.cpp)
target_link_libraries(osv_qt_selection_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_selection_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_session_state_test tests/session_state_test.cpp session_state.cpp)
target_link_libraries(osv_qt_session_state_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_session_state_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_tag_controller_test tests/tag_controller_test.cpp tag_controller.cpp)
target_link_libraries(osv_qt_tag_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test gtest gtest_main)
target_include_directories(osv_qt_tag_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_favorites_controller_test tests/favorites_controller_test.cpp favorites_controller.cpp)
target_link_libraries(osv_qt_favorites_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test gtest gtest_main)
target_include_directories(osv_qt_favorites_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_tag_overview_controller_test tests/tag_overview_controller_test.cpp tag_overview_controller.cpp)
target_link_libraries(osv_qt_tag_overview_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test gtest gtest_main)
target_include_directories(osv_qt_tag_overview_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_tag_list_import_adapter_test tests/tag_list_import_adapter_test.cpp tag_list_import_adapter.cpp)
target_link_libraries(osv_qt_tag_list_import_adapter_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test gtest gtest_main)
target_include_directories(osv_qt_tag_list_import_adapter_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_tag_list_import_controller_test tests/tag_list_import_controller_test.cpp tag_list_import_controller.cpp tag_list_import_adapter.cpp)
target_link_libraries(osv_qt_tag_list_import_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test gtest gtest_main)
target_include_directories(osv_qt_tag_list_import_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_adv_search_controller_test tests/adv_search_controller_test.cpp adv_search_controller.cpp)
target_link_libraries(osv_qt_adv_search_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test gtest gtest_main)
target_include_directories(osv_qt_adv_search_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_search_model_adapter_test tests/search_model_adapter_test.cpp search_model_adapter.cpp)
target_link_libraries(osv_qt_search_model_adapter_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test gtest gtest_main)
target_include_directories(osv_qt_search_model_adapter_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(osv_qt_viewer_controller_test tests/viewer_controller_test.cpp)
target_link_libraries(osv_qt_viewer_controller_test PRIVATE osv_qt_core Qt6::Core)
target_include_directories(osv_qt_viewer_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_viewer_album_test tests/viewer_album_test.cpp)
target_link_libraries(osv_qt_viewer_album_test PRIVATE osv_qt_core Qt6::Core)
target_include_directories(osv_qt_viewer_album_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_slideshow_controller_test tests/slideshow_controller_test.cpp)
target_link_libraries(osv_qt_slideshow_controller_test PRIVATE osv_qt_core Qt6::Core)
target_include_directories(osv_qt_slideshow_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_anim_controller_test tests/anim_controller_test.cpp anim_controller.cpp)
target_link_libraries(osv_qt_anim_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Test)
target_include_directories(osv_qt_anim_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_pixel_buffer_test tests/pixel_buffer_test.cpp)
target_link_libraries(osv_qt_pixel_buffer_test PRIVATE osv_qt_core Qt6::Core)
target_include_directories(osv_qt_pixel_buffer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_anim_hover_probe_test tests/anim_hover_probe_test.cpp)
target_link_libraries(osv_qt_anim_hover_probe_test PRIVATE osv_qt_core Qt6::Core)
target_include_directories(osv_qt_anim_hover_probe_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_album_rebind_test tests/album_rebind_test.cpp)
target_link_libraries(osv_qt_album_rebind_test PRIVATE osv_qt_core Qt6::Core)
target_include_directories(osv_qt_album_rebind_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_viewer_album_integration_test tests/viewer_album_integration_test.cpp)
target_link_libraries(osv_qt_viewer_album_integration_test PRIVATE osv_qt_core Qt6::Core)
target_include_directories(osv_qt_viewer_album_integration_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_executable(osv_qt_playback_engine_test tests/playback_engine_test.cpp playback_engine.cpp audio_pipe.cpp video_frame_item.cpp rhi_quad_renderer.cpp session_state.cpp)
target_link_libraries(osv_qt_playback_engine_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_playback_engine_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

qt6_add_shaders(osv_qt_playback_engine_test "osv_qt_playback_engine_test_shaders"
    PREFIX "/osvqt"
    FILES shaders/texquad.vert shaders/texquad.frag shaders/yuv.frag
)

add_executable(osv_qt_import_controller_test tests/import_controller_test.cpp import_controller.cpp)
target_link_libraries(osv_qt_import_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test Qt6::Widgets)
target_include_directories(osv_qt_import_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_import_autolock_wiring_test tests/import_autolock_wiring_test.cpp)
target_link_libraries(osv_qt_import_autolock_wiring_test PRIVATE Qt6::Core Qt6::Test)
target_include_directories(osv_qt_import_autolock_wiring_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_import_status_screen_test tests/import_status_screen_test.cpp)
target_link_libraries(osv_qt_import_status_screen_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_import_status_screen_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_executable(osv_qt_import_status_screen_qml_test tests/import_status_screen_qml_test.cpp)
target_link_libraries(osv_qt_import_status_screen_qml_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_import_status_screen_qml_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(osv_qt_import_status_screen_qml_test PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

# Shaders for import_status_screen test
qt6_add_shaders(osv_qt_import_status_screen_qml_test "osv_qt_import_status_screen_qml_test_shaders"
    PREFIX "/osvqt"
    FILES shaders/texquad.vert shaders/texquad.frag shaders/yuv.frag
)

add_executable(osv_qt_archive_dialogs_test tests/archive_dialogs_test.cpp)
target_link_libraries(osv_qt_archive_dialogs_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_archive_dialogs_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(osv_qt_archive_dialogs_test PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

add_executable(osv_qt_export_controller_test tests/export_controller_test.cpp export_controller.cpp)
target_link_libraries(osv_qt_export_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_export_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_transfer_controller_test tests/transfer_controller_test.cpp transfer_controller.cpp file_op_controller.cpp)
target_link_libraries(osv_qt_transfer_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test Qt6::Widgets)
target_include_directories(osv_qt_transfer_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_vault_manager_controller_test tests/vault_manager_controller_test.cpp vault_manager_controller.cpp vault_list_model.cpp)
target_link_libraries(osv_qt_vault_manager_controller_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_vault_manager_controller_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv_qt_unlock_controller_keyfile_test tests/unlock_controller_keyfile_test.cpp unlock_controller.cpp secure_text_field.cpp secure_image_item.cpp video_frame_item.cpp viewer_controller.cpp playback_engine.cpp audio_pipe.cpp rhi_quad_renderer.cpp thumb_cache.cpp gallery_model.cpp session_state.cpp cover_provider.cpp status_controller.cpp theme_palette.cpp)
target_link_libraries(osv_qt_unlock_controller_keyfile_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_unlock_controller_keyfile_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_executable(osv_qt_help_popup_qml_test tests/help_popup_qml_test.cpp)
target_link_libraries(osv_qt_help_popup_qml_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick)
target_include_directories(osv_qt_help_popup_qml_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(osv_qt_help_popup_qml_test PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

# Shaders for help_popup test
qt6_add_shaders(osv_qt_help_popup_qml_test "osv_qt_help_popup_qml_test_shaders"
    PREFIX "/osvqt"
    FILES shaders/texquad.vert shaders/texquad.frag shaders/yuv.frag
)

qt_add_executable(osv_qt_settings_overlay_qml_test tests/settings_overlay_qml_test.cpp)
target_link_libraries(osv_qt_settings_overlay_qml_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick)
target_include_directories(osv_qt_settings_overlay_qml_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(osv_qt_settings_overlay_qml_test PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

# Shaders for settings_overlay test
qt6_add_shaders(osv_qt_settings_overlay_qml_test "osv_qt_settings_overlay_qml_test_shaders"
    PREFIX "/osvqt"
    FILES shaders/texquad.vert shaders/texquad.frag shaders/yuv.frag
)

qt_add_executable(osv_qt_favorites_screen_qml_test
    tests/favorites_screen_qml_test.cpp
    theme_palette.cpp
    favorites_controller.cpp
    gallery_model.cpp
    cover_provider.cpp
    status_controller.cpp
    viewer_controller.cpp
    selection_controller.cpp
    thumb_cache.cpp
    secure_image_item.cpp
    rhi_quad_renderer.cpp
)
target_link_libraries(osv_qt_favorites_screen_qml_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick)
target_include_directories(osv_qt_favorites_screen_qml_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(osv_qt_favorites_screen_qml_test PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

# Shaders for favorites_screen test
qt6_add_shaders(osv_qt_favorites_screen_qml_test "osv_qt_favorites_screen_qml_test_shaders"
    PREFIX "/osvqt"
    FILES shaders/texquad.vert shaders/texquad.frag shaders/yuv.frag
)

qt_add_executable(osv_qt_thumb_strip_qml_test tests/thumb_strip_qml_test.cpp)
target_link_libraries(osv_qt_thumb_strip_qml_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick Qt6::Test)
target_include_directories(osv_qt_thumb_strip_qml_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(osv_qt_thumb_strip_qml_test PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

# Shaders for thumb_strip test
qt6_add_shaders(osv_qt_thumb_strip_qml_test "osv_qt_thumb_strip_qml_test_shaders"
    PREFIX "/osvqt"
    FILES shaders/texquad.vert shaders/texquad.frag shaders/yuv.frag
)

add_executable(osv_qt_search_overlay_qml_test tests/search_overlay_qml_test.cpp)
target_link_libraries(osv_qt_search_overlay_qml_test PRIVATE Qt6::Core)
target_include_directories(osv_qt_search_overlay_qml_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_executable(osv_qt_tag_overview_screen_qml_test tests/tag_overview_screen_qml_test.cpp)
target_link_libraries(osv_qt_tag_overview_screen_qml_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick)
target_include_directories(osv_qt_tag_overview_screen_qml_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(osv_qt_tag_overview_screen_qml_test PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

# Shaders for tag_overview_screen test
qt6_add_shaders(osv_qt_tag_overview_screen_qml_test "osv_qt_tag_overview_screen_qml_test_shaders"
    PREFIX "/osvqt"
    FILES shaders/texquad.vert shaders/texquad.frag shaders/yuv.frag
)

qt_add_executable(osv_qt_adv_search_screen_qml_test tests/adv_search_screen_qml_test.cpp)
target_link_libraries(osv_qt_adv_search_screen_qml_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick)
target_include_directories(osv_qt_adv_search_screen_qml_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(osv_qt_adv_search_screen_qml_test PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

# Shaders for adv_search_screen test
qt6_add_shaders(osv_qt_adv_search_screen_qml_test "osv_qt_adv_search_screen_qml_test_shaders"
    PREFIX "/osvqt"
    FILES shaders/texquad.vert shaders/texquad.frag shaders/yuv.frag
)

add_executable(osv_qt_save_search_dialog_qml_test tests/save_search_dialog_qml_test.cpp)
target_link_libraries(osv_qt_save_search_dialog_qml_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
target_include_directories(osv_qt_save_search_dialog_qml_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(osv_qt_save_search_dialog_qml_test PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

add_executable(osv_qt_tag_autocomplete_popup_qml_test tests/tag_autocomplete_popup_qml_test.cpp)
target_link_libraries(osv_qt_tag_autocomplete_popup_qml_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
target_include_directories(osv_qt_tag_autocomplete_popup_qml_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(osv_qt_tag_autocomplete_popup_qml_test PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

# Finding 5 + BREAKAGE FIX: DetailPanel QML load test + tag-section method test
# Test 0: catches attached-object QML load failures (Line 93/140 in DetailPanel.qml)
# Tests 1-3: verify DetailController methods exist and work, preventing undefined-method crashes
add_executable(osv_qt_detail_panel_qml_test tests/detail_panel_qml_test.cpp)
target_link_libraries(osv_qt_detail_panel_qml_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(osv_qt_detail_panel_qml_test PRIVATE
    QTUI_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

add_executable(osv_qt_thumb_stress_test tests/thumb_stress_test.cpp gallery_model.cpp cover_provider.cpp thumb_cache.cpp viewer_controller.cpp secure_image_item.cpp rhi_quad_renderer.cpp status_controller.cpp)
target_link_libraries(osv_qt_thumb_stress_test PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick)
target_include_directories(osv_qt_thumb_stress_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(osv-qt-mkvault tools/mkvault.cpp)
target_link_libraries(osv-qt-mkvault PRIVATE osv_qt_core)

qt_add_executable(osv-qt
    main.cpp
    app_wiring.cpp
    selftest.cpp
    qml_dir.cpp
    secure_text_field.cpp
    secure_image_item.cpp
    video_frame_item.cpp
    rhi_quad_renderer.cpp
    unlock_controller.cpp
    vault_list_model.cpp
    vault_manager_controller.cpp
    gallery_model.cpp
    cover_provider.cpp
    detail_controller.cpp
    thumb_cache.cpp
    viewer_controller.cpp
    slideshow_controller.cpp
    anim_controller.cpp
    animated_image_loader.cpp
    theme_palette.cpp
    playback_engine.cpp
    audio_pipe.cpp
    file_op_controller.cpp
    status_controller.cpp
    settings_controller.cpp
    import_controller.cpp
    export_controller.cpp
    transfer_controller.cpp
    help_model.cpp
    selection_controller.cpp
    session_state.cpp
    tag_controller.cpp
    favorites_controller.cpp
    tag_overview_controller.cpp
    tag_list_import_adapter.cpp
    tag_list_import_controller.cpp
    adv_search_controller.cpp
    search_model_adapter.cpp
    autolock.cpp
)

# Shaders: compile to .qsb and embed as resources
qt6_add_shaders(osv-qt "osv_qt_shaders"
    PREFIX "/osvqt"
    FILES shaders/texquad.vert shaders/texquad.frag shaders/yuv.frag
)

target_include_directories(osv-qt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(osv-qt PRIVATE osv_qt_core Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick Qt6::Widgets)

# osv-qt resolves its QML at runtime relative to the executable (qml_dir.cpp),
# not a compile-time path — a packaged release binary isn't run from the
# source tree it was built in. Keep dev builds and packaged releases on the
# same code path by copying qml/ next to the binary here too.
add_custom_command(TARGET osv-qt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/qml"
        "$<TARGET_FILE_DIR:osv-qt>/qml"
)
