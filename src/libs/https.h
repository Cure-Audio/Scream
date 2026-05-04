#pragma once
#include "../libs/picohttpparser.h"
#include <stdlib.h>
#include <xhl/thread.h>

#define FLAG_HTTPS_THREAD_CANCEL   1ULL
#define FLAG_HTTPS_THREAD_RUNNING  2ULL
#define FLAG_HTTPS_THREAD_FINISHED 4ULL

#ifdef __cplusplus
extern "C" {
#endif

// Messages for status codes 500-504
extern const char* http_status_messages_500[5];

typedef enum HTTPSResponseError
{
    HTTPS_ERROR_NONE,
    HTTPS_ERROR_CONNECTION,
    HTTPS_ERROR_INCOMPLETE_RESPONSE,
    HTTPS_ERROR_UNKNOWN,
} HTTPSResponseError;

typedef struct https_response
{
    HTTPSResponseError error;

    char*  buffer;
    size_t size;
    size_t capacity;

    ptrdiff_t content_length;
    ptrdiff_t body_offset; // body = buffer + body_offset

    int status_code;

    xt_atomic_uint64_t flags;
} https_response;

void https_free(https_response* res);
void https_get(https_response* res, const char* hostname, int port, const char* pathname);
// json only
void https_post(https_response* res, const char* hostname, int port, const char* path, const char* data, int datalen);

// Call from main thread
// 1. [Main thread] Set cancel flag on request thread
//     1.1 [Request thread] Cancel request
//     1.2 [Request thread] Post cleanup/resolve callback to the global queue
//     1.3 [Request thread] exit thread
// 2. [Main thread] join thread
// 3. [Main thread] destroy thread
// 4. [Main thread] process global queue
void https_cancel(https_response* res, xt_thread_ptr_t* thread);

#ifdef __cplusplus
}
#endif