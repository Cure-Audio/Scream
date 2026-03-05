#define XHL_STRING_IMPL
#define XHL_FILES_IMPL
#define XHL_ALLOC_IMPL

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION

#include <stdio.h>

#include <xhl/alloc.h>
#include <xhl/files.h>
#include <xhl/string.h>

#include "nanosvg3.h"

// NOTE: this appears to have some release mode bugs...?
void parse_svg(const char* path)
{
    const char* name = xfiles_get_name(path);
    const char* ext  = xfiles_get_extension(name);

    void*  filedata = 0;
    size_t filesize = 0;

    if (xfiles_read(path, &filedata, &filesize))
    {
        NSVGimage2* svg = nsvgParse2(filedata, "px", 96);

        // Print buffer to console
        int       i           = 0;
        int       n           = 0;
        char      buffer[256] = {0};
        uint64_t* data        = (uint64_t*)svg;
        xassert((svg->buffer_size & 7) == 0);
        const int N = svg->buffer_size / 8;

        printf(
            "// clang-format off\n"
            "const unsigned long long SVG_DATA_%.*s[] = {\n",
            (int)(ext - name),
            name);
        while (i < N)
        {
            // Try to print the shortest amount we can
            uint64_t v = data[i];
            if (v == 0)
            {
                n += xtr_fmt(buffer, sizeof(buffer), n, "0,");
            }
            else if (v < 100000000000000000llu) // 18 places
            {
                n += xtr_fmt(buffer, sizeof(buffer), n, "%llu,", v);
            }
            else
            {
                n += xtr_fmt(buffer, sizeof(buffer), n, "0x%llX,", v);
            }

            if (n > 120)
            {
                printf("%s\n", buffer);
                n = 0;
            }

            i++;
        }
        if (n)
            printf("%s\n", buffer);
        printf("};\n"
               "// clang-format on\n");

        xfree(filedata);
    }
}

int main(int argc, char* argv[])
{
    xalloc_init();
    int should_print_usage = 0;

    // Verify all paths are well formed correct
    for (int i = 1; i < argc; i++)
    {
        const char* arg  = argv[i];
        const char* name = NULL;
        const char* ext  = NULL;

        int bad_arg = 0 == xfiles_exists(arg);

        if (bad_arg == 0)
        {
            name = xfiles_get_name(arg);
            if (name)
                ext = xfiles_get_extension(name);

            bad_arg |= name == NULL;
            bad_arg |= ext == NULL;
            bad_arg |= ext == name;
            bad_arg |= ext == name;
            bad_arg |= 0 == xtr_match(ext, ".svg");
        }

        if (bad_arg != 0)
        {
            printf("Bad argument: (idx: %d) \"%s\"", i - 1, arg);
        }

        should_print_usage |= bad_arg;
    }

    // Arguments are well formed
    if (should_print_usage == 0 && argc > 1)
    {
        for (int i = 1; i < argc; i++)
        {
            const char* path = argv[i];

            // xfiles_
            parse_svg(path);
        }
    }

    if (should_print_usage)
    {
        printf("Usage: {program_name} arg1 arg2 arg3 ...\n"
               "All args should be paths to .svg files");
    }

    xalloc_shutdown();
    return 0;
}
