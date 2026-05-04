#pragma once

typedef enum UpdatesStatus
{
    UPDATE_STATUS_UNKNOWN,
    UPDATE_STATUS_AVAILABLE,   // A new update of the plugin exists
    UPDATE_STATUS_UNAVAILABLE, // The user has the latest version of the plugin
    UPDATE_STATUS_ERROR,       // Something went wrong...
} UpdatesStatus;

extern UpdatesStatus UPDATE_STATUS;
extern char          UPDATE_URL[];

void updates_init();
void updates_deinit();