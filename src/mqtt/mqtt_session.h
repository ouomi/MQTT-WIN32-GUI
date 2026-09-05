#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "mqtt_endpoint.hpp"
#include "mqtt_subscriptions.hpp"
#include "mqtt_session_backend.hpp"

namespace win32mqtt {

enum class MqttConnectionState { Disconnected, Connecting, Connected, Disconnecting, Failed };
enum class MqttEventType { StateChanged, MessageReceived, PublishQueued, PublishRejected, Log };
enum class MqttPublishQos { Qos0, Qos1, Qos2 };
// Admission only; Accepted does not mean sent or acknowledged by the broker.
enum class MqttAdmission { Accepted, QueueFull, TooLarge, Stopped };

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
    bool retain = false;
    bool dup = false;
    std::uint16_t packet_id = 0;
    std::uint64_t generation = 0;
    std::uint64_t operation = 0;
    std::uint64_t received_ms = 0;
    bool simulated_disconnect = false;
};

class MqttSession {
public:
    using EventHandler = std::function<void(MqttEvent)>;

    explicit MqttSession(EventHandler event_handler, std::shared_ptr<MqttSessionBackend> backend = {});
    ~MqttSession();

    MqttSession(const MqttSession&) = delete;
    MqttSession& operator=(const MqttSession&) = delete;

    MqttAdmission Connect(MqttEndpoint endpoint, std::string client_id,
                 std::optional<MqttLastWill> last_will = std::nullopt);
    void Disconnect();
    // Close the transport without sending MQTT DISCONNECT.
    MqttAdmission SimulateAbnormalDisconnect();
    MqttAdmission Subscribe(std::string topic);
    MqttAdmission Unsubscribe(std::string topic);
    MqttAdmission Publish(std::string topic, std::string payload, MqttPublishQos qos);
    void Stop();
    // Desired state survives disconnects and is reconciled after each clean connection.
    MqttAdmission SetSubscriptions(std::vector<std::string> topics);
    std::vector<MqttSubscriptionStatus> Subscriptions() const;
    // Reliable admission outcomes, retained until taken (maximum 256 outstanding).
    // These are protocol queue results, not broker acknowledgements.
    std::vector<MqttEvent> TakePublishResults();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace win32mqtt
