#pragma once
#include <sys/time.h>
typedef void (*sntp_sync_time_cb_t)(struct timeval*);
void sntp_set_time_sync_notification_cb(sntp_sync_time_cb_t callback);
// Test hook: simulate the SNTP library reporting a completed sync.
void hostTimeSync();
