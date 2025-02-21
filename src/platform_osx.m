#define SOKOL_METAL
#define XHL_FILES_IMPL
#define XHL_TIME_IMPL
#define XHL_ALLOC_IMPL

#ifndef NDEBUG
#define SOKOL_ASSERT(cond) (cond) ? (void)0 : __builtin_debugtrap()
#endif

#include "libs/sokol_gfx.h"
#include <cplug_extensions/window_osx.m>

#include <xhl/alloc.h>
#include <xhl/files.h>
#include <xhl/time.h>

#ifndef NDEBUG
void println(const char* const fmt, ...)
{
    char    buf[256] = {0};
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n)
    {
        if (n < sizeof(buf) && buf[n - 1] != '\n')
        {
            buf[n] = '\n';
            n++;
        }

#ifdef CPLUG_BUILD_STANDALONE
        fwrite(buf, 1, n, stdout);
#else
        char path[1024];
        xfiles_get_user_directory(path, sizeof(path), XFILES_USER_DIRECTORY_DESKTOP);
        strcat(path, "/log.txt");

        FILE* f = fopen(path, "a");
        if (f)
        {
            fwrite(buf, 1, n, f);
            fclose(f);
        }
#endif
    }
}
#endif // NDEBUG