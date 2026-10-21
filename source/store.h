/* store.h - the background worker: brings the network up, loads the
 * catalogs, installs, updates and removes apps. The UI thread asks for work
 * and reads progress; results are applied on the UI thread in store_poll(). */
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    STORE_STARTING,     /* bringing the network up */
    STORE_OFFLINE,      /* no network; cached catalogs only */
    STORE_IDLE,
    STORE_REFRESHING,
    STORE_INSTALLING,
    STORE_REMOVING,
} StoreState;

void store_init(void);
void store_poll(void);               /* once per frame, UI thread */
StoreState store_state(void);
bool store_busy(void);
const char *store_step(void);        /* what the worker is doing right now */
float store_progress(void);          /* 0..1, or -1 when unknown */
const char *store_job_name(void);    /* app being installed/removed */

void store_request_refresh(void);
void store_request_install(int app_index);
void store_request_remove(int installed_index);
void store_cancel(void);

/* Messages for the toast area; error = something went wrong. */
bool store_take_message(char *out, size_t cap, bool *error);
bool store_net_up(void);
const char *store_net_error(void);   /* "" when fine */
bool store_tls_verified(void);       /* a CA bundle is loaded */
