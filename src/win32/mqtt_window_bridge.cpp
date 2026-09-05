#include "mqtt_window_bridge.h"

#include <utility>

namespace win32mqtt {

MqttSession::EventHandler MqttWindowBridge::Handler() const {
    return [queue = queue_](MqttEvent event) {
        queue->Push(std::move(event));
    };
}

} // namespace win32mqtt
