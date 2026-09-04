#pragma once

#include <windows.h>

#include <memory>

#include "../mqtt/mqtt_session.h"

namespace win32mqtt {

MqttSession::EventHandler MakeMqttWindowEventHandler(HWND window);
std::unique_ptr<MqttEvent> TakeMqttWindowEvent(LPARAM event_parameter);
void DrainMqttWindowEvents(HWND window);

} // namespace win32mqtt
