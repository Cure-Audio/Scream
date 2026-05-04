#include <stdio.h>
#include <string.h>
#include <xhl/files.h>
#include <xhl/string.h>
#include <xhl/system.h>
#include <xhl/time.h>

#ifdef _WIN32
__declspec(dllimport) void __stdcall OutputDebugStringA(const char* lpOutputString);

int __stdcall MultiByteToWideChar(
    unsigned int    CodePage,
    unsigned long   dwFlags,
    const char*     lpMultiByteStr,
    int             cbMultiByte,
    unsigned short* lpWideCharStr,
    int             cchWideChar);
#endif

FILE* my_freopen(const char* path, const char* mode, FILE* stream)
{
    fflush(stream);
#ifdef _WIN32
    static wchar_t TempPath[260] = {0};
    static wchar_t TempMode[8]   = {0};
    int            ret1          = MultiByteToWideChar(65001, 0x00000008, path, -1, TempPath, 260);
    int            ret2          = MultiByteToWideChar(65001, 0x00000008, mode, -1, TempMode, 8);
    FILE*          f             = NULL;
    if (ret1 > 0 && ret2 > 0)
        f = _wfreopen(TempPath, TempMode, stdout);
    return f;
#else
    return freopen(path, mode, stream);
#endif
}

#ifndef NDEBUG
void println(const char* const fmt, ...)
{
    char    buf[256] = {0};
    va_list args;
    va_start(args, fmt);
    int n = xtr_fmt_va(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    if (!n)
        return;

    xassert(n <= ARRLEN(buf) - 2);
    buf[n++] = '\n';
    buf[n]   = '\0';

#ifdef _WIN32
#define DBGPRINT(str, len) OutputDebugStringA(str)
#else
#define DBGPRINT(str, len) fwrite(str, 1, len, stderr);
#endif
    DBGPRINT(buf, n);

    // Log to file
#if 0
    static char path[1024] = {0};
    if (strlen(path) == 0)
    {
        bool ok = xfiles_get_user_directory(path, sizeof(path), XFILES_USER_DIRECTORY_DESKTOP);
        xassert(ok);
        strcat(path, XFILES_DIR_STR "scream_log.txt");
    }
    bool ok = xfiles_append(path, buf, n);
    // xassert(ok);
#endif
}
#endif // NDEBUG

static char LOG_FILE_PATH[1024] = {0};

void log_error(const char* fmt, ...)
{
    bool is_first_log = LOG_FILE_PATH[0] == 0;
    int  n            = 0;

    FILE* f = NULL;

#ifdef NDEBUG
    if (is_first_log)
    {
        n = xfiles_get_user_directory(LOG_FILE_PATH, sizeof(LOG_FILE_PATH), XFILES_USER_DIRECTORY_APPDATA);
        const char*                                          rel_path =
            XFILES_DIR_STR CPLUG_COMPANY_NAME XFILES_DIR_STR CPLUG_PLUGIN_NAME XFILES_DIR_STR "logs" XFILES_DIR_STR;

        n += xtr_fmt(LOG_FILE_PATH, sizeof(LOG_FILE_PATH), n, "%s", rel_path);
        xfiles_create_directory_recursive(LOG_FILE_PATH);

        uint64_t now_ms = xtime_unix_ms();
        XDate    date   = xtime_get_date(now_ms);

        n += xtr_fmt(
            LOG_FILE_PATH,
            sizeof(LOG_FILE_PATH),
            n,
            "%s_log_%d-%.2d-%.2d_%.2d-%.2d-%.2d_GMT00.log",
            CPLUG_PLUGIN_NAME,
            date.year,
            date.month,
            date.day,
            date.hour,
            date.minute,
            date.second);

        f = my_freopen(LOG_FILE_PATH, "a+", stdout);

        printf(
            "%d-%.2d-%.2d %.2d:%.2d:%.2d:%.3d GMT+00\n",
            date.year,
            date.month,
            date.day,
            date.hour,
            date.minute,
            date.second,
            date.millisecond);

        xsys_print(&g_xsysinfo);
    }
#endif // NDEBUG

    char buf[256] = {0};

    va_list args;
    va_start(args, fmt);
    n = xtr_fmt_va(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    buf[n]     = '\n';
    buf[n + 1] = '\0';

#if defined(NDEBUG)
    if (f == NULL)
        f = my_freopen(LOG_FILE_PATH, "a+", stdout);
    xassert(f);
    fwrite(buf, 1, n, f);
#endif
    if (f)
    {
        fflush(f);
        fclose(f);
    }

#ifdef _WIN32
    OutputDebugStringA(buf);
#else
    fwrite(buf, 1, n, stdout);
#endif // NDEBUG
}
