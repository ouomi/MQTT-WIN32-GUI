#include "mqtt_window_bridge.h"

#include <utility>

#include "ui_ids.h"

namespace win32mqtt {

MqttSession::EventHandler MakeMqttWindowEventHandler(HWND window) {
    return [window](MqttEvent event) {
        auto* queued_event = new MqttEvent(std::move(event));
        if (!PostMessageW(window, WM_MQTT_EVENT, 0,
                          reinterpret_cast<LPARAM>(queued_event))) {
            delete queued_event;
        }
    };
}

std::unique_ptr<MqttEvent> TakeMqttWindowEvent(LPARAM event_parameter) {
    return std::unique_ptr<MqttEvent>(reinterpret_cast<MqttEvent*>(event_parameter));
}

void DrainMqttWindowEvents(HWND window) {
    MSG message{};
    while (PeekMessageW(&message, window, WM_MQTT_EVENT, WM_MQTT_EVENT, PM_REMOVE)) {
        delete reinterpret_cast<MqttEvent*>(message.lParam);
    }
}

} // namespace win32mqtt
