cmake_minimum_required(VERSION 3.16)
project(osv_vaapi_shim C)

set(LIBVA_SRC "" CACHE PATH "Path to the vendored vendor/libva submodule (headers only, never built)")
if(NOT LIBVA_SRC)
    message(FATAL_ERROR "LIBVA_SRC must point at vendor/libva")
endif()

add_library(osv_vaapi_shim STATIC osv_vaapi_shim.c)

# Include order: our own va/va_version.h + va/va_drm.h (flattened, since we
# never run libva's own build to generate/flatten them -- see Task 2's step
# 1-2 comments) resolve from this directory; everything else (va/va.h and the
# headers it pulls in at its own end-of-file) resolves from the submodule.
# No path collision: the submodule tree has no va/va_version.h (only the
# unprocessed va/va_version.h.in) and no va/va_drm.h (only va/drm/va_drm.h,
# a different relative path) -- see Task 2 file header comments.
target_include_directories(osv_vaapi_shim PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${LIBVA_SRC}"
)

install(TARGETS osv_vaapi_shim ARCHIVE DESTINATION lib)

# Header subset FFmpeg's hwcontext_vaapi.c needs: va.h unconditionally
# #includes all of these at its own end-of-file (dec/enc/fei/vpp/prot
# headers), plus va_drm.h for the HAVE_VAAPI_DRM include. va_str.h and
# va_drmcommon.h are NOT included by hwcontext_vaapi.c/vaapi_*.c given this
# project's configure flags (no CONFIG_LIBDRM -- a separate, unrelated
# configure check we never satisfy) -- confirmed by direct grep of the
# pinned vendor/ffmpeg source, not assumed.
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/va/va_version.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/va/va_drm.h"
    "${LIBVA_SRC}/va/va.h"
    "${LIBVA_SRC}/va/va_dec_hevc.h"
    "${LIBVA_SRC}/va/va_dec_jpeg.h"
    "${LIBVA_SRC}/va/va_dec_vp8.h"
    "${LIBVA_SRC}/va/va_dec_vp9.h"
    "${LIBVA_SRC}/va/va_dec_av1.h"
    "${LIBVA_SRC}/va/va_dec_vvc.h"
    "${LIBVA_SRC}/va/va_enc_hevc.h"
    "${LIBVA_SRC}/va/va_fei_hevc.h"
    "${LIBVA_SRC}/va/va_enc_h264.h"
    "${LIBVA_SRC}/va/va_enc_jpeg.h"
    "${LIBVA_SRC}/va/va_enc_mpeg2.h"
    "${LIBVA_SRC}/va/va_enc_vp8.h"
    "${LIBVA_SRC}/va/va_enc_vp9.h"
    "${LIBVA_SRC}/va/va_enc_av1.h"
    "${LIBVA_SRC}/va/va_fei.h"
    "${LIBVA_SRC}/va/va_fei_h264.h"
    "${LIBVA_SRC}/va/va_vpp.h"
    "${LIBVA_SRC}/va/va_prot.h"
    DESTINATION include/va)

# Synthetic pkg-config files: FFmpeg's ./configure runs
# `check_pkg_config vaapi "libva >= 0.35.0" "va/va.h" vaInitialize` and
# `check_pkg_config vaapi_drm "libva-drm" "va/va_drm.h" vaGetDisplayDRM` --
# these must resolve against THIS shim, not a real system libva, so a build
# on a machine without libva-dev installed behaves identically to one with
# it (see docs/superpowers/specs/2026-07-17-hardware-video-decode-design.md).
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/libva.pc.in"
               "${CMAKE_CURRENT_BINARY_DIR}/libva.pc" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/libva-drm.pc.in"
               "${CMAKE_CURRENT_BINARY_DIR}/libva-drm.pc" @ONLY)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libva.pc"
    "${CMAKE_CURRENT_BINARY_DIR}/libva-drm.pc"
    DESTINATION lib/pkgconfig)
