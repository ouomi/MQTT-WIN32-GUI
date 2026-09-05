#pragma once

#include <memory>
#include "../mqtt/mqtt_event_queue.hpp"

namespace win32mqtt {

// Owned by the window. Handlers hold only the mailbox, never the HWND or window.
class MqttWindowBridge {
public:
    MqttWindowBridge() = default;
    MqttWindowBridge(const MqttWindowBridge&) = delete;
    MqttWindowBridge& operator=(const MqttWindowBridge&) = delete;
    ~MqttWindowBridge() { Close(); }
    MqttSession::EventHandler Handler() const;
    MqttEventQueue::Batch Take() { return queue_->Take(); }
    void Close() { queue_->Close(); }
private:
    std::shared_ptr<MqttEventQueue> queue_ = std::make_shared<MqttEventQueue>();
};

} // namespace win32mqtt
