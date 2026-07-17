// osv_vaapi_shim.c -- dlopen-based forwarding shim for the ~36 libva/libva-drm
// symbols FFmpeg's libavutil/hwcontext_vaapi.c and the vaapi_{decode,h264,
// hevc,vp8,vp9,mjpeg}.c hwaccel glue reference. See
// docs/superpowers/specs/2026-07-17-hardware-video-decode-design.md ("VAAPI
// linking: why dlopen, not a system link") for the full rationale: FFmpeg
// compiles hwcontext_vaapi.c unconditionally once --enable-vaapi is passed to
// its configure, so these symbols must resolve at THIS PROJECT's own final
// static link regardless of whether hw decode is ever exercised -- but a
// direct `-lva` system link would give the produced binary a DT_NEEDED entry
// on libva.so.2, breaking "opportunistic, never a hard requirement" for
// every Linux user without libva installed, not just ones without a
// supported GPU.
//
// Each function below dlopen()s the real shared library on first call
// (cached thereafter, never retried) and forwards via dlsym(); if either
// step fails, it returns the same "unimplemented" value a real libva driver
// would return for an unsupported call -- media::HwAccelContext
// (src/media/hw_accel.cpp, Part 1) already treats every hwaccel failure this
// way, so callers need no VAAPI-specific handling.
#include <va/va.h>
#include <va/va_drm.h>

#include <dlfcn.h>
#include <stddef.h>

static void *core_handle(void)
{
    static void *handle;
    static int   tried;
    if (!tried) {
        handle = dlopen("libva.so.2", RTLD_NOW | RTLD_GLOBAL);
        tried  = 1;
    }
    return handle;
}

static void *drm_handle(void)
{
    static void *handle;
    static int   tried;
    if (!tried) {
        handle = dlopen("libva-drm.so.2", RTLD_NOW | RTLD_GLOBAL);
        tried  = 1;
    }
    return handle;
}

#define VA_SHIM_FORWARD(ret, name, params, args, fail)                \
    ret name params                                                   \
    {                                                                 \
        static ret (*real) params;                                    \
        static int  tried;                                            \
        if (!tried) {                                                 \
            void *h = core_handle();                                  \
            real = h ? (ret (*) params)dlsym(h, #name) : NULL;         \
            tried = 1;                                                \
        }                                                             \
        return real ? real args : (fail);                             \
    }

VA_SHIM_FORWARD(VAStatus, vaAcquireBufferHandle,
    (VADisplay dpy, VABufferID buf_id, VABufferInfo *buf_info),
    (dpy, buf_id, buf_info),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaBeginPicture,
    (VADisplay dpy, VAContextID context, VASurfaceID render_target),
    (dpy, context, render_target),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaCreateBuffer,
    (VADisplay dpy, VAContextID context, VABufferType type, unsigned int size,
     unsigned int num_elements, void *data, VABufferID *buf_id),
    (dpy, context, type, size, num_elements, data, buf_id),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaCreateConfig,
    (VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint,
     VAConfigAttrib *attrib_list, int num_attribs, VAConfigID *config_id),
    (dpy, profile, entrypoint, attrib_list, num_attribs, config_id),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaCreateContext,
    (VADisplay dpy, VAConfigID config_id, int picture_width, int picture_height,
     int flag, VASurfaceID *render_targets, int num_render_targets,
     VAContextID *context),
    (dpy, config_id, picture_width, picture_height, flag, render_targets,
     num_render_targets, context),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaCreateImage,
    (VADisplay dpy, VAImageFormat *format, int width, int height, VAImage *image),
    (dpy, format, width, height, image),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaCreateSurfaces,
    (VADisplay dpy, unsigned int format, unsigned int width, unsigned int height,
     VASurfaceID *surfaces, unsigned int num_surfaces,
     VASurfaceAttrib *attrib_list, unsigned int num_attribs),
    (dpy, format, width, height, surfaces, num_surfaces, attrib_list, num_attribs),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaDeriveImage,
    (VADisplay dpy, VASurfaceID surface, VAImage *image),
    (dpy, surface, image),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaDestroyBuffer,
    (VADisplay dpy, VABufferID buffer_id),
    (dpy, buffer_id),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaDestroyConfig,
    (VADisplay dpy, VAConfigID config_id),
    (dpy, config_id),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaDestroyContext,
    (VADisplay dpy, VAContextID context),
    (dpy, context),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaDestroyImage,
    (VADisplay dpy, VAImageID image),
    (dpy, image),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaDestroySurfaces,
    (VADisplay dpy, VASurfaceID *surfaces, int num_surfaces),
    (dpy, surfaces, num_surfaces),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaEndPicture,
    (VADisplay dpy, VAContextID context),
    (dpy, context),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(const char *, vaErrorStr,
    (VAStatus error_status),
    (error_status),
    NULL)

VA_SHIM_FORWARD(VAStatus, vaExportSurfaceHandle,
    (VADisplay dpy, VASurfaceID surface_id, uint32_t mem_type, uint32_t flags,
     void *descriptor),
    (dpy, surface_id, mem_type, flags, descriptor),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaGetImage,
    (VADisplay dpy, VASurfaceID surface, int x, int y, unsigned int width,
     unsigned int height, VAImageID image),
    (dpy, surface, x, y, width, height, image),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaInitialize,
    (VADisplay dpy, int *major_version, int *minor_version),
    (dpy, major_version, minor_version),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaMapBuffer,
    (VADisplay dpy, VABufferID buf_id, void **pbuf),
    (dpy, buf_id, pbuf),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaMapBuffer2,
    (VADisplay dpy, VABufferID buf_id, void **pbuf, uint32_t flags),
    (dpy, buf_id, pbuf, flags),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(int, vaMaxNumImageFormats,
    (VADisplay dpy),
    (dpy),
    0)

VA_SHIM_FORWARD(int, vaMaxNumProfiles,
    (VADisplay dpy),
    (dpy),
    0)

VA_SHIM_FORWARD(VAStatus, vaPutImage,
    (VADisplay dpy, VASurfaceID surface, VAImageID image, int src_x, int src_y,
     unsigned int src_width, unsigned int src_height, int dest_x, int dest_y,
     unsigned int dest_width, unsigned int dest_height),
    (dpy, surface, image, src_x, src_y, src_width, src_height, dest_x, dest_y,
     dest_width, dest_height),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaQueryConfigProfiles,
    (VADisplay dpy, VAProfile *profile_list, int *num_profiles),
    (dpy, profile_list, num_profiles),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaQueryImageFormats,
    (VADisplay dpy, VAImageFormat *format_list, int *num_formats),
    (dpy, format_list, num_formats),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaQuerySurfaceAttributes,
    (VADisplay dpy, VAConfigID config, VASurfaceAttrib *attrib_list,
     unsigned int *num_attribs),
    (dpy, config, attrib_list, num_attribs),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(const char *, vaQueryVendorString,
    (VADisplay dpy),
    (dpy),
    NULL)

VA_SHIM_FORWARD(VAStatus, vaReleaseBufferHandle,
    (VADisplay dpy, VABufferID buf_id),
    (dpy, buf_id),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaRenderPicture,
    (VADisplay dpy, VAContextID context, VABufferID *buffers, int num_buffers),
    (dpy, context, buffers, num_buffers),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaSetDriverName,
    (VADisplay dpy, char *driver_name),
    (dpy, driver_name),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAMessageCallback, vaSetErrorCallback,
    (VADisplay dpy, VAMessageCallback callback, void *user_context),
    (dpy, callback, user_context),
    NULL)

VA_SHIM_FORWARD(VAMessageCallback, vaSetInfoCallback,
    (VADisplay dpy, VAMessageCallback callback, void *user_context),
    (dpy, callback, user_context),
    NULL)

VA_SHIM_FORWARD(VAStatus, vaSyncSurface,
    (VADisplay dpy, VASurfaceID render_target),
    (dpy, render_target),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaTerminate,
    (VADisplay dpy),
    (dpy),
    VA_STATUS_ERROR_UNIMPLEMENTED)

VA_SHIM_FORWARD(VAStatus, vaUnmapBuffer,
    (VADisplay dpy, VABufferID buf_id),
    (dpy, buf_id),
    VA_STATUS_ERROR_UNIMPLEMENTED)

#undef VA_SHIM_FORWARD

// vaGetDisplayDRM lives in libva-drm.so.2, a separate shared object from
// libva.so.2 in real libva packaging -- hand-written rather than routed
// through VA_SHIM_FORWARD since it needs drm_handle(), not core_handle().
VADisplay vaGetDisplayDRM(int fd)
{
    static VADisplay (*real)(int);
    static int        tried;
    if (!tried) {
        void *h = drm_handle();
        real = h ? (VADisplay (*)(int))dlsym(h, "vaGetDisplayDRM") : NULL;
        tried = 1;
    }
    return real ? real(fd) : NULL;
}
