#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

/* Static-build stand-in for the header CMake normally generates for miniz.
 * We compile the vendored miniz sources directly (no CMake), so we supply this
 * from a repo-owned shim dir on the include path instead of mutating the
 * pristine vendor/miniz submodule. A static link needs no symbol-visibility
 * decoration, so both macros expand to nothing. */
#define MINIZ_EXPORT
#define MINIZ_NO_EXPORT

#endif /* MINIZ_EXPORT_H */
