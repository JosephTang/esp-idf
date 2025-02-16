/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_sntp.h"

// Remove compat macro and include lwip API
#undef SNTP_OPMODE_POLL
#include "lwip/apps/sntp.h"
#include "lwip/tcpip.h"

static const char *TAG = "sntp";

ESP_STATIC_ASSERT(SNTP_OPMODE_POLL == ESP_SNTP_OPMODE_POLL, "SNTP mode in lwip doesn't match the IDF enum. Please make sure lwIP version is correct");
ESP_STATIC_ASSERT(SNTP_OPMODE_LISTENONLY == ESP_SNTP_OPMODE_LISTENONLY, "SNTP mode in lwip doesn't match the IDF enum. Please make sure lwIP version is correct");

static volatile sntp_sync_mode_t sntp_sync_mode = SNTP_SYNC_MODE_IMMED;
static volatile sntp_sync_status_t sntp_sync_status = SNTP_SYNC_STATUS_RESET;
static sntp_sync_time_cb_t time_sync_notification_cb = NULL;
static uint32_t s_sync_interval = CONFIG_LWIP_SNTP_UPDATE_DELAY;

inline void sntp_set_sync_status(sntp_sync_status_t sync_status)
{
    sntp_sync_status = sync_status;
}

void __attribute__((weak)) sntp_sync_time(struct timeval *tv)
{
    if (sntp_sync_mode == SNTP_SYNC_MODE_IMMED) {
        settimeofday(tv, NULL);
        sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
    } else if (sntp_sync_mode == SNTP_SYNC_MODE_SMOOTH) {
        struct timeval tv_now;
        gettimeofday(&tv_now, NULL);
        int64_t cpu_time = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
        int64_t sntp_time = (int64_t)tv->tv_sec * 1000000L + (int64_t)tv->tv_usec;
        int64_t delta = sntp_time - cpu_time;
        struct timeval tv_delta = { .tv_sec = delta / 1000000L, .tv_usec = delta % 1000000L };
        if (adjtime(&tv_delta, NULL) == -1) {
            ESP_LOGD(TAG, "Function adjtime don't update time because the error is very big");
            settimeofday(tv, NULL);
            ESP_LOGD(TAG, "Time was synchronized through settimeofday");
            sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
        } else {
            sntp_set_sync_status(SNTP_SYNC_STATUS_IN_PROGRESS);
        }
    }
    if (time_sync_notification_cb) {
        time_sync_notification_cb(tv);
    }
}

void sntp_set_sync_mode(sntp_sync_mode_t sync_mode)
{
    sntp_sync_mode = sync_mode;
}

sntp_sync_mode_t sntp_get_sync_mode(void)
{
    return sntp_sync_mode;
}

// set a callback function for time synchronization notification
void sntp_set_time_sync_notification_cb(sntp_sync_time_cb_t callback)
{
    time_sync_notification_cb = callback;
}

sntp_sync_status_t sntp_get_sync_status(void)
{
    sntp_sync_status_t ret_sync_status = SNTP_SYNC_STATUS_RESET;
    sntp_sync_status_t sync_status = sntp_sync_status;
    if (sync_status == SNTP_SYNC_STATUS_COMPLETED) {
        sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
        ret_sync_status = SNTP_SYNC_STATUS_COMPLETED;
    } else if (sync_status == SNTP_SYNC_STATUS_IN_PROGRESS) {
        struct timeval outdelta;
        adjtime(NULL, &outdelta);
        if (outdelta.tv_sec == 0 && outdelta.tv_usec == 0) {
            sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
            ret_sync_status = SNTP_SYNC_STATUS_COMPLETED;
        } else {
            ret_sync_status = SNTP_SYNC_STATUS_IN_PROGRESS;
        }
    }
    return ret_sync_status;
}

void sntp_set_sync_interval(uint32_t interval_ms)
{
    if (interval_ms < 15000) {
        // SNTPv4 RFC 4330 enforces a minimum update time of 15 seconds
        interval_ms = 15000;
    }
    s_sync_interval = interval_ms;
}

uint32_t sntp_get_sync_interval(void)
{
    return s_sync_interval;
}

static void sntp_do_restart(void *ctx)
{
    (void)ctx;
    sntp_stop();
    sntp_init();
}

bool sntp_restart(void)
{
    if (sntp_enabled()) {
        tcpip_callback(sntp_do_restart, NULL);
        return true;
    }
    return false;
}

void sntp_set_system_time(uint32_t sec, uint32_t us)
{
    // Note: SNTP/NTP timestamp is defined as 64-bit fixed point int
    // in seconds from 1900 (integer part is the first 32 bits)
    // which overflows in 2036.
    // The lifetime of the NTP timestamps has been extended by convention
    // of the MSB bit 0 to span between 1968 and 2104. This is implemented
    // in lwip sntp module, so this API returns number of seconds/milliseconds
    // representing dates range from 1968 to 2104.
    // (see: RFC-4330#section-3 and https://github.com/lwip-tcpip/lwip/blob/239918cc/src/apps/sntp/sntp.c#L129-L134)
    // Warning: Here, we convert the 32 bit NTP timestamp to 64 bit representation
    // of system time (time_t). This won't work for timestamps in future
    // after some time in 2104
    struct timeval tv = { .tv_sec = sec, .tv_usec = us };
    sntp_sync_time(&tv);
}

void sntp_get_system_time(uint32_t *sec, uint32_t *us)
{
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
    gettimeofday(&tv, NULL);
    // Warning: Here, we convert 64 bit representation of system time (time_t) to
    // 32 bit NTP timestamp. This won't work for future timestamps after some time in 2104
    // (see: RFC-4330#section-3)
    *(sec) = tv.tv_sec;
    *(us) = tv.tv_usec;
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
}

static void do_setoperatingmode(void *ctx)
{
    esp_sntp_operatingmode_t operating_mode = (esp_sntp_operatingmode_t)ctx;
    sntp_setoperatingmode(operating_mode);
}

void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t operating_mode)
{
    tcpip_callback(do_setoperatingmode, (void*)operating_mode);
}

static void do_init(void *ctx)
{
    sntp_init();
}

void esp_sntp_init(void)
{
    tcpip_callback(do_init, NULL);
}

static void do_stop(void *ctx)
{
    sntp_stop();
}

void esp_sntp_stop(void)
{
    tcpip_callback(do_stop, NULL);
}

struct do_setserver {
    u8_t idx;
    const ip_addr_t *addr;
};

static void do_setserver(void *ctx)
{
    struct do_setserver *params = ctx;
    sntp_setserver(params->idx, params->addr);
}

void esp_sntp_setserver(u8_t idx, const ip_addr_t *addr)
{
    struct do_setserver params = {
            .idx = idx,
            .addr = addr
    };
    tcpip_callback(do_setserver, &params);
}

struct do_setservername {
    u8_t idx;
    const char *server;
};

static void do_setservername(void *ctx)
{
    struct do_setservername *params = ctx;
    sntp_setservername(params->idx, params->server);
}

void esp_sntp_setservername(u8_t idx, const char *server)
{
    struct do_setservername params = {
            .idx = idx,
            .server = server
    };
    tcpip_callback(do_setservername, &params);
}

const char *esp_sntp_getservername(u8_t idx)
{
    return sntp_getservername(idx);
}

const ip_addr_t* esp_sntp_getserver(u8_t idx)
{
    return sntp_getserver(idx);
}

#if LWIP_DHCP_GET_NTP_SRV
static void do_servermode_dhcp(void* ctx)
{
    u8_t servermode = (bool)ctx ? 1 : 0;
    sntp_servermode_dhcp(servermode);
}

void esp_sntp_servermode_dhcp(bool enable)
{
    tcpip_callback(do_servermode_dhcp, (void*)enable);
}

#endif /* LWIP_DHCP_GET_NTP_SRV */
