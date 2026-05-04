#include "updates.h"
#include "plugin.h"
#include <https.h>
#include <xhl/debug.h>
#include <xhl/string.h>
#include <xhl/thread.h>
#include <yyjson.h>

UpdatesStatus UPDATE_STATUS   = 0;
char          UPDATE_URL[512] = {0};

int             g_updates_refcount = 0;
xt_thread_ptr_t g_updates_thread   = NULL;
https_response  g_updates_response = {0};

// [unknown thread]
int check_updates_thread(void*)
{
    const char* pathname = "/api/v1/products/version?name=" CPLUG_PLUGIN_NAME "&version=" CPLUG_PLUGIN_VERSION;
    https_get(&g_updates_response, "exacoustics.com", 443, pathname);

    if (g_updates_response.error == HTTPS_ERROR_NONE && g_updates_response.content_length &&
        g_updates_response.status_code == 200)
    {
        const char* body = g_updates_response.buffer + g_updates_response.body_offset;

        yyjson_doc* doc  = yyjson_read(body, g_updates_response.content_length, 0);
        yyjson_val* root = yyjson_doc_get_root(doc);

#ifdef _WIN32
#define DOWNLOAD_URL_KEY "windowsDownloadUrl"
#elif defined(__APPLE__)
#define DOWNLOAD_URL_KEY "macOSDownloadUrl"
#elif __linux__
#define DOWNLOAD_URL_KEY "linuxDownloadUrl"
#error "TODO"
#endif

        const char* version_string = yyjson_get_str(yyjson_obj_get(root, "version"));
        const char* download_url   = yyjson_get_str(yyjson_obj_get(root, DOWNLOAD_URL_KEY));
        // const char* download_url = "https://exacoustics.com/test_url";

        if (version_string && download_url)
        {
            plugin_version current_version = parse_plugin_version(CPLUG_PLUGIN_VERSION);
            plugin_version latest_version  = parse_plugin_version(version_string);

            // Validate data
            if (latest_version.u32 > 0 && xtr_startswith(download_url, "https://"))
            {
                xtr_fmt(UPDATE_URL, sizeof(UPDATE_URL), 0, "%s", download_url);

                UPDATE_STATUS =
                    latest_version.u32 > current_version.u32 ? UPDATE_STATUS_AVAILABLE : UPDATE_STATUS_UNAVAILABLE;
            }
        }

        yyjson_doc_free(doc);
    }

    if (UPDATE_STATUS == UPDATE_STATUS_UNKNOWN)
        UPDATE_STATUS = UPDATE_STATUS_ERROR;

    // println("UPDATE_STATUS: %d", UPDATE_STATUS);
    // xassert(UPDATE_STATUS != UPDATE_STATUS_ERROR);
    https_free(&g_updates_response);

    return 0;
}

// [main thread]
void updates_init()
{
    g_updates_refcount++;
    if (UPDATE_STATUS != UPDATE_STATUS_UNKNOWN)
        return;
    if (g_updates_thread != NULL)
        return;
    xassert(g_updates_response.buffer == NULL);

    g_updates_thread = xthread_create(check_updates_thread, NULL, 0);
}

// [main thread]
void updates_deinit()
{
    g_updates_refcount--;

    if (g_updates_refcount == 0)
    {
        https_cancel(&g_updates_response, &g_updates_thread);
    }
}