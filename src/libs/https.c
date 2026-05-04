#include "https.h"
#include <stdio.h>
#include <string.h>
#include <xhl/alloc.h>
#include <xhl/debug.h>
#include <xhl/maths.h>
#include <xhl/string.h>
#include <xrequest.h>

const char* http_status_messages_500[] = {
    "HTTP Status 500: Internal Server Error.\nSorry, something unexpected happened!",
    "HTTP Status 501: Not Implemented.\nThis feature is not ready yet. Please try again later.",
    "HTTP Status 502: Bad Gateway.\nSorry, something unexpected happened in an upstream server!",
    "HTTP Status 503: Service Unavailable.\nSorry, something unexpected happened in an upstream server!",
    "HTTP Status 504: Gateway Timeout.\nThe server took too long to respond. Please try again leter.",
};

// 504 Gateway Timeout

void https_free(struct https_response* res)
{
    if (res->buffer != NULL)
    {
        xfree(res->buffer);
        memset(res, 0, sizeof(*res));
    }
}

void https_response_init(struct https_response* res)
{
    if (!res->buffer)
    {
        res->capacity = 1024 * 4;
        res->buffer   = xcalloc(1, res->capacity);
    }
    res->error          = 0;
    res->size           = 0;
    res->content_length = 0;
    res->status_code    = 0;
    res->body_offset    = 0;
}

HTTPSResponseError https_translate_error(XRequestError err)
{
    switch (err)
    {
    case XREQUEST_ERROR_NONE:
        return HTTPS_ERROR_NONE;
    case XREQUEST_ERROR_CONNECTION_FAILED:
        return HTTPS_ERROR_CONNECTION;
    case XREQUEST_ERROR_USER_CANCELLED:
    case XREQUEST_ERROR_TIMEOUT:
    case XREQUEST_ERROR_UNKNOWN:
    case XREQUEST_ERROR_COUNT:
    default:
        return HTTPS_ERROR_UNKNOWN;
    }
}

int https_cb(
    void*       user_ptr,
    const void* data, // May be NULL
    unsigned    size  // May be zero
)
{
    struct https_response* res = user_ptr;

    if (xt_atomic_load_u64(&res->flags) & FLAG_HTTPS_THREAD_CANCEL)
        return 1;

    if (size && data)
    {
        if (res->size + size > res->capacity)
        {
            res->capacity += res->capacity;
            res->buffer    = xrealloc(res->buffer, res->capacity);
        }
        memcpy(res->buffer + res->size, data, size);
        res->size += size;
    }
    return 0;
}

static void https_response_process(const char* hostname, int port, const char* req, int reqlen, https_response* res)
{
    XRequestError xerr = xrequest(hostname, port, req, reqlen, res, https_cb);
    res->error         = https_translate_error(xerr);
    if (res->error != HTTPS_ERROR_NONE)
        return;

    int         version;
    const char* msg;
    size_t      msg_len;

    // If we don't allocate enough headers, phr_parse_response won't return the offset to our response body
    // We already have at least 1kb padded memory allocated in our response buffer
    // Use remaining memory to store headers (at least 1kb / 32bytes = 32 headers)
    size_t used_data   = xm_align_up(res->size, sizeof(struct phr_header));
    size_t num_headers = (res->capacity - used_data) / sizeof(struct phr_header);

    struct phr_header* headers = (struct phr_header*)(res->buffer + used_data);

    res->body_offset = phr_parse_response(
        res->buffer,
        res->size,
        &version,
        &res->status_code,
        &msg,
        &msg_len,
        headers,
        &num_headers, // [in/out]
        0);

    if (res->status_code == 0)
    {
        res->error = HTTPS_ERROR_INCOMPLETE_RESPONSE;
        return;
    }

    ptrdiff_t   expected_size          = res->size - res->body_offset;
    const char* target_header_name     = "content-length";
    int         target_header_name_len = STRLEN("content-length");
    for (int i = 0; i < num_headers; i++)
    {
        struct phr_header* h = headers + i;

        // Find key
        if (xtr_imatch2(h->name, h->name_len, target_header_name, target_header_name_len))
        {
            // Get value
            char* end;
            res->content_length = (ptrdiff_t)strtol(h->value, &end, 10);
            break;
        }
    }
    xassert(expected_size == res->content_length);
    if (expected_size != res->content_length)
        res->error = HTTPS_ERROR_INCOMPLETE_RESPONSE;
}

void https_get(https_response* res, const char* hostname, int port, const char* pathname)
{
    https_response_init(res);

    char req[128];
    int  reqlen = xfmt(
        req,
        0,
        "GET %s HTTP/1.1\r\n"
         "Host: %s\r\n"
         "Connection: close\r\n\r\n",
        pathname,
        hostname);

    https_response_process(hostname, port, req, reqlen, res);
}

void https_post(https_response* res, const char* hostname, int port, const char* path, const char* data, int datalen)
{
    https_response_init(res);

    char req[2048];
    int  reqlen = xfmt(
        req,
        0,
        "POST %s HTTP/1.1\r\n"
         "Host: %s\r\n"
         "Connection: close\r\n"
         "Accept: */*\r\n"
         "Content-Type: application/json\r\n"
         "Content-Length: %d\r\n\r\n%.*s",
        path,
        hostname,
        datalen,
        datalen,
        data);
    xassert(reqlen < sizeof(req));

    https_response_process(hostname, port, req, reqlen, res);
}

void https_cancel(https_response* res, xt_thread_ptr_t* thread)
{
    xassert(xthread_current() != *thread);
    if (xt_atomic_load_u64(&res->flags) & FLAG_HTTPS_THREAD_RUNNING)
    {
        xt_atomic_fetch_or_u64(&res->flags, FLAG_HTTPS_THREAD_CANCEL);

        xthread_join(*thread);
        xthread_destroy(*thread);
        *thread = NULL;
    }
}