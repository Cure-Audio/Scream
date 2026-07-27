// clang-format off

#ifdef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#ifdef __APPLE__
#define HAVE_FCNTL_H
#define HAVE_UNISTD_H
#endif

#define FT2BUILD_H_

#define FT_CONFIG_OPTION_DISABLE_STREAM_SUPPORT

#define FT_BEGIN_HEADER
#define FT_END_HEADER
#define FT2_BUILD_LIBRARY
#define FREETYPE_CONFIG_PUBLIC_MACROS_H_

#define FT_PUBLIC_FUNCTION_ATTRIBUTE  /* empty */
#define FT_EXPORT( x )  FT_PUBLIC_FUNCTION_ATTRIBUTE extern x
#define FT_UNUSED( arg )  ( (arg) = (arg) )


#ifdef __cplusplus
#define FT_STATIC_CAST( type, var )       static_cast<type>(var)
#define FT_REINTERPRET_CAST( type, var )  reinterpret_cast<type>(var)

#define FT_STATIC_BYTE_CAST( type, var )                         \
          static_cast<type>( static_cast<unsigned char>( var ) )
#else
#define FT_STATIC_CAST( type, var )       (type)(var)
#define FT_REINTERPRET_CAST( type, var )  (type)(var)

#define FT_STATIC_BYTE_CAST( type, var )  (type)(unsigned char)(var)
#endif

#define FT_CONFIG_OPTIONS_H <freetype_scu_options.h>
#define FT_CONFIG_MODULES_H <freetype_scu_modules.h>

#define FT_CONFIG_CONFIG_H <freetype/config/ftconfig.h>
// #define FT_CONFIG_OPTIONS_H <freetype/config/ftoption.h>

#define FT_CONFIG_STANDARD_LIBRARY_H <freetype/config/ftstdlib.h>

// Note: currently this include order works. DO NOT CHANGE!

// These are all niche font formats we probably don't need
// TODO: disable
// #include "../../modules/freetype/src/pfr/pfr.c"
// // TODO: disable
// #include "../../modules/freetype/src/base/ftbdf.c"
// #include "../../modules/freetype/src/bdf/bdf.c"
// #include "../../modules/freetype/src/base/ftcid.c"
// #include "../../modules/freetype/src/cid/type1cid.c"
// #include "../../modules/freetype/src/winfonts/winfnt.c"
// #include "../../modules/freetype/src/base/ftwinfnt.c"
// #include "../../modules/freetype/src/pcf/pcf.c"
// #include "../../modules/freetype/src/base/fttype1.c"
// #include "../../modules/freetype/src/type1/type1.c"
// #include "../../modules/freetype/src/type42/type42.c"

// Rasterizers we dont need
// monochrome rasterizer
// #include "../../modules/freetype/src/raster/raster.c"
// // SDF rasterizer
// #include "../../modules/freetype/src/sdf/sdf.c"
// #undef ONE_PIXEL

// #include "../../modules/freetype/src/base/ftbbox.c"
// #include "../../modules/freetype/src/base/ftpatent.c"
// #include "../../modules/freetype/src/base/ftsynth.c"
#include "../../modules/freetype/src/base/ftmm.c"
// #include "../../modules/freetype/src/base/ftstroke.c"
// #include "../../modules/freetype/src/base/ftpfr.c"
// #include "../../modules/freetype/src/base/ftfstype.c"
// #include "../../modules/freetype/src/base/ftotval.c"
#include "../../modules/freetype/src/base/ftinit.c"
// #include "../../modules/freetype/src/base/ftgxval.c"
// #include "../../modules/freetype/src/base/ftgasp.c"
// #include "../../modules/freetype/src/base/ftglyph.c"
// #include "../../modules/freetype/src/base/ftbitmap.c"

// May not want this
// #include "../../modules/freetype/src/cache/ftcache.c"

// For compressed fonts. Do not want
// #include "../../modules/freetype/src/lzw/ftlzw.c"

#include "../../modules/freetype/src/base/ftbase.c"

// #include "../../modules/freetype/src/svg/svg.c"
#include "../../modules/freetype/src/smooth/smooth.c"
// #include "../../modules/freetype/src/cff/cff.c"

// We probably want this?
// #include "../../modules/freetype/src/autofit/autofit.c"

// postscript. Probably don't need
// #include "../../modules/freetype/src/pshinter/pshinter.c"
// #include "../../modules/freetype/src/psaux/psaux.c"

// Truetype. Need
#include "../../modules/freetype/src/truetype/truetype.c"
// Truetype dependancies
// #include "../../modules/freetype/src/psnames/psnames.c"
#include "../../modules/freetype/src/sfnt/sfnt.c"

#if defined (_WIN32)
#include "../../modules/freetype/builds/windows/ftdebug.c"
#include "../../modules/freetype/builds/windows/ftsystem.c"
#endif

#ifdef __APPLE__
#include "../../modules/freetype/builds/mac/ftmac.c"
#include "../../modules/freetype/src/base/ftdebug.c"
#include "../../modules/freetype/builds/unix/ftsystem.c"
#endif

// TODO: remove
// Support for zipped font files we don't need
// This include some source code (crc32.c) that uses #define N {some_constant} and #define W {some_constant}
// #include "../../modules/freetype/src/gzip/ftgzip.c"
// #ifdef N
// #undef N
// #endif
// #ifdef W
// #undef W
// #endif

// clang-format on