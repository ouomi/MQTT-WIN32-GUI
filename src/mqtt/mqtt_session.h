#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "mqtt_endpoint.hpp"

namespace win32mqtt {

enum class MqttConnectionState { Disconnected, Connecting, Connected, Disconnecting, Failed };
enum class MqttEventType { StateChanged, MessageReceived, PublishQueued, PublishRejected, Log };
enum class MqttPublishQos { Qos0, Qos1, Qos2 };

struct MqttLastWill {
    std::string topic;
    std::string payload;
};

struct MqttEvent {
    MqttEventType type;
    MqttConnectionState connection_state;
    std::string detail;
    std::string topic;
    std::string payload;
    MqttPublishQos publish_qos;
};

class MqttSession {
public:
    using EventHandler = std::function<void(MqttEvent)>;

    explicit MqttSession(EventHandler event_handler);
    ~MqttSession();

    MqttSession(const MqttSession&) = delete;
    MqttSession& operator=(const MqttSession&) = delete;

    void Connect(MqttEndpoint endpoint, std::string client_id,
                 std::optional<MqttLastWill> last_will = std::nullopt);
    void Disconnect();
    void Subscribe(std::string topic);
    void Unsubscribe(std::string topic);
    void Publish(std::string topic, std::string payload, MqttPublishQos qos);
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace win32mqtt
