#ifndef WIN32MQTT_TEST_PAL_H
#define WIN32MQTT_TEST_PAL_H

/* Protocol tests only: no real sockets, threads, or wall clock. */
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(WIN32MQTT_TEST_MSVC)
typedef ptrdiff_t ssize_t;
#else
#include <sys/types.h>
#endif

typedef int64_t mqtt_pal_time_t;
typedef int mqtt_pal_mutex_t;
#if defined(MQTT_USE_BIO)
#include <openssl/bio.h>
typedef BIO *mqtt_pal_socket_handle;
#else
typedef int mqtt_pal_socket_handle;
#endif

static inline uint16_t mqtt_test_htons(uint16_t value)
{
    const uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    uint16_t result;
    memcpy(&result, bytes, sizeof(result));
    return result;
}

static inline uint16_t mqtt_test_ntohs(uint16_t value)
{
    uint8_t bytes[2];
    memcpy(bytes, &value, sizeof(bytes));
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

#define MQTT_PAL_HTONS(value) mqtt_test_htons(value)
#define MQTT_PAL_NTOHS(value) mqtt_test_ntohs(value)
#if defined(WIN32MQTT_TEST_CLOCK)
extern mqtt_pal_time_t mqtt_test_time;
#define MQTT_PAL_TIME() mqtt_test_time
#else
#define MQTT_PAL_TIME() ((mqtt_pal_time_t)0)
#endif
#if defined(WIN32MQTT_TEST_TRACK_LOCKS)
void mqtt_test_mutex_init(mqtt_pal_mutex_t *mutex);
void mqtt_test_mutex_lock(mqtt_pal_mutex_t *mutex);
void mqtt_test_mutex_unlock(mqtt_pal_mutex_t *mutex);
#define MQTT_PAL_MUTEX_INIT(ptr) mqtt_test_mutex_init(ptr)
#define MQTT_PAL_MUTEX_LOCK(ptr) mqtt_test_mutex_lock(ptr)
#define MQTT_PAL_MUTEX_UNLOCK(ptr) mqtt_test_mutex_unlock(ptr)
#else
#define MQTT_PAL_MUTEX_INIT(ptr) (*(ptr) = 0)
#define MQTT_PAL_MUTEX_LOCK(ptr) ((void)(ptr), abort())
#define MQTT_PAL_MUTEX_UNLOCK(ptr) ((void)(ptr), abort())
#endif

#endif
