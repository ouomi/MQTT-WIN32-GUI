#ifndef MQTT_SESSION_PAL_H
#define MQTT_SESSION_PAL_H
#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <time.h>
typedef int mqtt_pal_socket_handle;
typedef int64_t mqtt_pal_time_t;
typedef pthread_mutex_t mqtt_pal_mutex_t;
#define MQTT_PAL_MUTEX_INIT(p) pthread_mutex_init(p, NULL)
#define MQTT_PAL_MUTEX_LOCK(p) pthread_mutex_lock(p)
#define MQTT_PAL_MUTEX_UNLOCK(p) pthread_mutex_unlock(p)
#define MQTT_PAL_MUTEX_DESTROY(p) pthread_mutex_destroy(p)
#define MQTT_PAL_HTONS(v) htons(v)
#define MQTT_PAL_NTOHS(v) ntohs(v)
static inline mqtt_pal_time_t mqtt_session_time(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec;
}
#define MQTT_PAL_TIME() mqtt_session_time()
#endif
