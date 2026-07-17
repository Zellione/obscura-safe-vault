/*
 * Copyright (C) 2009 Splitted-Desktop Systems. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* NOT part of the vendor/libva submodule: libva's real va_version.h is
 * generated at build time from va_version.h.in (see va/meson.build) and is
 * therefore not a tracked file in the submodule tree. This is a hand-written
 * equivalent, pinned to the exact values libva 2.22.0's own meson.build
 * would substitute (va_api_major_version=1, va_api_minor_version=22,
 * va_api_micro_version=0) -- update these three values together with the
 * vendor/libva submodule pin (docs/VENDORED_DEPS.md bump procedure). */

#ifndef VA_VERSION_H
#define VA_VERSION_H

#define VA_MAJOR_VERSION    1
#define VA_MINOR_VERSION    22
#define VA_MICRO_VERSION    0
#define VA_VERSION          1.22.0
#define VA_VERSION_S        "1.22.0"

#define VA_VERSION_HEX     ((VA_MAJOR_VERSION << 24) | \
                            (VA_MINOR_VERSION << 16) | \
                            (VA_MICRO_VERSION << 8))

#define VA_CHECK_VERSION(major,minor,micro) \
        (VA_MAJOR_VERSION > (major) || \
         (VA_MAJOR_VERSION == (major) && VA_MINOR_VERSION > (minor)) || \
         (VA_MAJOR_VERSION == (major) && VA_MINOR_VERSION == (minor) && VA_MICRO_VERSION >= (micro)))

#endif /* VA_VERSION_H */
