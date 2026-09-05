#pragma once

extern "C" {
#include "mqtt_c/mqtt.h"
}

namespace win32mqtt {

// MQTT-C stores a local queue-full condition in client.error. A rejected user
// operation has not queued a packet; preserve the live connection and existing
// queue instead of turning temporary congestion into a connection failure.
// Owned exclusively by the session worker, like the rest of the client.
template<class Operation>
MQTTErrors MqttUserRequest(mqtt_client& client, Operation operation) {
    const auto previous = client.error;
    const auto result = operation();
    if (previous == MQTT_OK && result == MQTT_ERROR_SEND_BUFFER_IS_FULL &&
        client.error == MQTT_ERROR_SEND_BUFFER_IS_FULL) client.error = MQTT_OK;
    return result;
}

} // namespace win32mqtt
