#ifndef WIN32MQTT_TRANSPORT_TEST_PAL_H
#define WIN32MQTT_TRANSPORT_TEST_PAL_H

#include "mqtt_test_pal.h"

#if !defined(MQTT_USE_BIO)
/* Match Winsock's int lengths without requiring Windows in native tests. */
#define SOCKET_ERROR (-1)
#define WSAEWOULDBLOCK 10035
int mqtt_test_send(int socket, const char *buf, int len, int flags);
int mqtt_test_recv(int socket, char *buf, int len, int flags);
int mqtt_test_socket_error(void);
#define send mqtt_test_send
#define recv mqtt_test_recv
#define WSAGetLastError mqtt_test_socket_error
#endif

#endif
