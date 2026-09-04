#include "mqtt_session.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#if WIN32MQTT_ENABLE_TLS
#include <openssl/ssl.h>
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <utility>

extern "C" {
#include "mqtt_c/mqtt.h"
}

namespace win32mqtt {
namespace {

#if WIN32MQTT_ENABLE_TLS
std::string ExecutableDirectoryUtf8() {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    std::wstring directory(path.data(), length);
    const std::size_t separator = directory.find_last_of(L"\\\\/");
    if (separator == std::wstring::npos) return {};
    directory.resize(separator + 1);
    const int utf8_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, directory.data(), static_cast<int>(directory.size()), nullptr, 0, nullptr, nullptr);
    if (utf8_length == 0) return {};
    std::string result(static_cast<std::size_t>(utf8_length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, directory.data(), static_cast<int>(directory.size()), result.data(), utf8_length, nullptr, nullptr);
    return result;
}
#endif

} // namespace

struct MqttSession::Impl {
    enum class CommandType { Connect, Disconnect, Subscribe, Unsubscribe, Publish, Stop };
    struct Command {
        CommandType type;
        MqttEndpoint endpoint;
        std::string first;
        std::string second;
        MqttPublishQos qos;
        std::optional<MqttLastWill> last_will;
    };

    explicit Impl(EventHandler handler) : handler(std::move(handler)), worker(&Impl::Run, this) {}
    ~Impl() { Stop(); }

    void Enqueue(Command command) {
        { std::lock_guard<std::mutex> lock(mutex); commands.push_back(std::move(command)); }
        wake.notify_one();
    }
    void Stop() {
        if (!stopped.exchange(true)) {
            cancel_connect.store(true);
            Enqueue({CommandType::Stop, {}, {}, {}, MqttPublishQos::Qos0, std::nullopt});
        }
        if (worker.joinable()) worker.join();
    }
    void Emit(MqttEventType type, MqttConnectionState next_state, std::string detail = {},
              std::string topic = {}, std::string payload = {},
              MqttPublishQos publish_qos = MqttPublishQos::Qos0) {
        if (handler) {
            handler(MqttEvent{type, next_state, std::move(detail), std::move(topic),
                              std::move(payload), publish_qos});
        }
    }
    static void Published(void** state, struct mqtt_response_publish* published) {
        auto* self = static_cast<Impl*>(*state);
        self->Emit(MqttEventType::MessageReceived, MqttConnectionState::Connected, {},
                   std::string(static_cast<const char*>(published->topic_name), published->topic_name_size),
                   std::string(static_cast<const char*>(published->application_message), published->application_message_size));
    }
    static std::uint8_t PublishFlags(MqttPublishQos qos) {
        switch (qos) {
        case MqttPublishQos::Qos0: return MQTT_PUBLISH_QOS_0;
        case MqttPublishQos::Qos1: return MQTT_PUBLISH_QOS_1;
        case MqttPublishQos::Qos2: return MQTT_PUBLISH_QOS_2;
        }
        return MQTT_PUBLISH_QOS_0;
    }
    bool OpenSocket(const MqttEndpoint& endpoint, std::string& error) {
        addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
        addrinfo* addresses = nullptr;
        if (getaddrinfo(endpoint.host.c_str(), endpoint.port.c_str(), &hints, &addresses) != 0) { error = "DNS lookup failed"; return false; }
        for (addrinfo* address = addresses; address != nullptr && !cancel_connect.load(); address = address->ai_next) {
            const SOCKET candidate = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (candidate == INVALID_SOCKET) continue;
            u_long nonblocking = 1; ioctlsocket(candidate, FIONBIO, &nonblocking);
            const int result = connect(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen));
            if (result == 0) { socket_handle = candidate; freeaddrinfo(addresses); return true; }
            const int socket_error = WSAGetLastError();
            if (socket_error == WSAEWOULDBLOCK || socket_error == WSAEINPROGRESS) {
                for (int retry = 0; retry < 50 && !cancel_connect.load(); ++retry) {
                    fd_set writable; FD_ZERO(&writable); FD_SET(candidate, &writable);
                    TIMEVAL timeout{0, 200000};
                    if (select(0, nullptr, &writable, nullptr, &timeout) > 0) {
                        int connect_error = 0; int length = sizeof(connect_error);
                        getsockopt(candidate, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&connect_error), &length);
                        if (connect_error == 0) { socket_handle = candidate; freeaddrinfo(addresses); return true; }
                        break;
                    }
                }
            }
            closesocket(candidate);
        }
        freeaddrinfo(addresses); error = cancel_connect.load() ? "connection cancelled" : "TCP connection failed"; return false;
    }
#if WIN32MQTT_ENABLE_TLS
    bool ConfigureTransport(const MqttEndpoint& endpoint, std::string& error) {
        if (endpoint.secure) {
            ssl_context = SSL_CTX_new(TLS_client_method());
            const std::string ca_bundle = ExecutableDirectoryUtf8() + "ca-bundle.pem";
            if (ssl_context == nullptr || ca_bundle.empty() ||
                SSL_CTX_load_verify_locations(ssl_context, ca_bundle.c_str(), nullptr) != 1 ||
                SSL_CTX_set_min_proto_version(ssl_context, TLS1_2_VERSION) != 1) {
                error = "unable to configure trusted TLS certificates"; return false;
            }
            SSL_CTX_set_verify(ssl_context, SSL_VERIFY_PEER, nullptr);
            transport = BIO_new_ssl(ssl_context, 1);
            SSL* ssl = nullptr;
            if (transport != nullptr) BIO_get_ssl(transport, &ssl);
            if (transport == nullptr || ssl == nullptr || SSL_set_tlsext_host_name(ssl, endpoint.host.c_str()) != 1 || SSL_set1_host(ssl, endpoint.host.c_str()) != 1) { error = "unable to configure TLS hostname verification"; return false; }
            BIO* socket_transport = BIO_new_socket(socket_handle, BIO_NOCLOSE);
            if (socket_transport == nullptr) { error = "unable to create TLS socket transport"; return false; }
            transport = BIO_push(transport, socket_transport);
            for (int attempt = 0; attempt < 500 && !cancel_connect.load(); ++attempt) {
                if (BIO_do_handshake(transport) == 1) return true;
                if (!BIO_should_retry(transport)) { error = "TLS handshake failed"; return false; }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            error = "TLS handshake timed out"; return false;
        }
        transport = BIO_new_socket(socket_handle, BIO_NOCLOSE);
        if (transport == nullptr) { error = "unable to create socket transport"; return false; }
        return true;
    }
#endif
    void CloseSocket() {
        client_initialized = false;
#if WIN32MQTT_ENABLE_TLS
        if (transport != nullptr) { BIO_free_all(transport); transport = nullptr; }
        if (ssl_context != nullptr) { SSL_CTX_free(ssl_context); ssl_context = nullptr; }
#endif
        if (socket_handle != INVALID_SOCKET) { closesocket(socket_handle); socket_handle = INVALID_SOCKET; }
    }
    void CompleteCancelledConnect() {
        CloseSocket(); state = MqttConnectionState::Disconnected; awaiting_connack = false;
        Emit(MqttEventType::StateChanged, state);
    }
    void Fail(std::string error) { CloseSocket(); state = MqttConnectionState::Failed; awaiting_connack = false; Emit(MqttEventType::StateChanged, state, std::move(error)); }
    void ConnectNow(const MqttEndpoint& endpoint, const std::string& client_id,
                    const std::optional<MqttLastWill>& last_will) {
        CloseSocket(); cancel_connect.store(false); state = MqttConnectionState::Connecting;
        Emit(MqttEventType::StateChanged, state, FormatMqttEndpointUri(endpoint));
        std::string error;
        if (!OpenSocket(endpoint, error)) {
            if (cancel_connect.load()) CompleteCancelledConnect(); else Fail(std::move(error));
            return;
        }
#if WIN32MQTT_ENABLE_TLS
        if (!ConfigureTransport(endpoint, error)) {
            if (cancel_connect.load()) CompleteCancelledConnect(); else Fail(std::move(error));
            return;
        }
        mqtt_reinit(&client, transport, send_buffer.data(), send_buffer.size(), recv_buffer.data(), recv_buffer.size());
#else
        if (endpoint.secure) { Fail("mqtts:// requires a TLS-enabled build"); return; }
        mqtt_reinit(&client, socket_handle, send_buffer.data(), send_buffer.size(), recv_buffer.data(), recv_buffer.size());
#endif
        client_initialized = true;
        if (cancel_connect.load()) { CompleteCancelledConnect(); return; }
        const char* will_topic = last_will ? last_will->topic.c_str() : nullptr;
        const void* will_payload = last_will ? last_will->payload.data() : nullptr;
        const std::size_t will_payload_size = last_will ? last_will->payload.size() : 0;
        const std::uint8_t connect_flags = static_cast<std::uint8_t>(
            MQTT_CONNECT_CLEAN_SESSION |
            (last_will ? MQTT_CONNECT_WILL_QOS_0 : 0));
        const enum MQTTErrors result = mqtt_connect(
            &client, client_id.empty() ? nullptr : client_id.c_str(), will_topic, will_payload,
            will_payload_size, nullptr, nullptr, connect_flags, 60);
        if (result != MQTT_OK) { Fail(mqtt_error_str(result)); return; }
        awaiting_connack = true;
    }
    void Sync() {
        if (!client_initialized ||
            (state != MqttConnectionState::Connecting && state != MqttConnectionState::Connected)) return;
        const enum MQTTErrors result = mqtt_sync(&client);
        if (result != MQTT_OK || client.error != MQTT_OK) { Fail(mqtt_error_str(result != MQTT_OK ? result : client.error)); return; }
        if (awaiting_connack && mqtt_mq_find(&client.mq, MQTT_CONTROL_CONNECT, nullptr) == nullptr) {
            awaiting_connack = false; state = MqttConnectionState::Connected; Emit(MqttEventType::StateChanged, state);
        }
    }
    void Handle(Command command) {
        switch (command.type) {
        case CommandType::Connect:
            ConnectNow(command.endpoint, command.first, command.last_will);
            break;
        case CommandType::Disconnect:
            cancel_connect.store(true);
            if (state == MqttConnectionState::Disconnected) { CloseSocket(); awaiting_connack = false; break; }
            if (state == MqttConnectionState::Connected && client_initialized) mqtt_disconnect(&client);
            state = MqttConnectionState::Disconnecting; Emit(MqttEventType::StateChanged, state);
            CloseSocket(); state = MqttConnectionState::Disconnected; awaiting_connack = false; Emit(MqttEventType::StateChanged, state); break;
        case CommandType::Subscribe: if (state == MqttConnectionState::Connected) mqtt_subscribe(&client, command.first.c_str(), 0); break;
        case CommandType::Unsubscribe: if (state == MqttConnectionState::Connected) mqtt_unsubscribe(&client, command.first.c_str()); break;
        case CommandType::Publish: {
            if (state != MqttConnectionState::Connected) {
                Emit(MqttEventType::PublishRejected, state, "not connected", std::move(command.first),
                     std::move(command.second), command.qos);
                break;
            }
            const enum MQTTErrors result = mqtt_publish(&client, command.first.c_str(),
                                                        command.second.data(), command.second.size(),
                                                        PublishFlags(command.qos));
            if (result != MQTT_OK) {
                Emit(MqttEventType::PublishRejected, state, mqtt_error_str(result),
                     std::move(command.first), std::move(command.second), command.qos);
                break;
            }
            Emit(MqttEventType::PublishQueued, state, {}, std::move(command.first),
                 std::move(command.second), command.qos);
            break;
        }
        case CommandType::Stop: stopping = true; break;
        }
    }
    void Run() {
        WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { Emit(MqttEventType::StateChanged, MqttConnectionState::Failed, "Winsock initialization failed"); return; }
        mqtt_init_reconnect(&client, nullptr, nullptr, Published); client.publish_response_callback_state = this;
        while (!stopping) {
            std::deque<Command> pending;
            { std::unique_lock<std::mutex> lock(mutex); wake.wait_for(lock, std::chrono::milliseconds(25), [this] { return !commands.empty(); }); pending.swap(commands); }
            for (Command& command : pending) Handle(std::move(command));
            Sync();
        }
        CloseSocket(); WSACleanup();
    }

    EventHandler handler; std::mutex mutex; std::condition_variable wake; std::deque<Command> commands; std::thread worker;
    std::atomic<bool> stopped{false}; std::atomic<bool> cancel_connect{false}; bool stopping = false; bool awaiting_connack = false; bool client_initialized = false;
    SOCKET socket_handle = INVALID_SOCKET;
#if WIN32MQTT_ENABLE_TLS
    BIO* transport = nullptr;
    SSL_CTX* ssl_context = nullptr;
#endif
    MqttConnectionState state = MqttConnectionState::Disconnected; struct mqtt_client client{};
    std::array<std::uint8_t, 8192> send_buffer{}; std::array<std::uint8_t, 8192> recv_buffer{};
};

MqttSession::MqttSession(EventHandler handler) : impl_(std::make_unique<Impl>(std::move(handler))) {}
MqttSession::~MqttSession() = default;
void MqttSession::Connect(MqttEndpoint endpoint, std::string client_id,
                          std::optional<MqttLastWill> last_will) {
    impl_->Enqueue({Impl::CommandType::Connect, std::move(endpoint), std::move(client_id), {},
                    MqttPublishQos::Qos0, std::move(last_will)});
}
void MqttSession::Disconnect() {
    impl_->cancel_connect.store(true);
    impl_->Enqueue({Impl::CommandType::Disconnect, {}, {}, {}, MqttPublishQos::Qos0,
                    std::nullopt});
}
void MqttSession::Subscribe(std::string topic) {
    impl_->Enqueue({Impl::CommandType::Subscribe, {}, std::move(topic), {},
                    MqttPublishQos::Qos0, std::nullopt});
}
void MqttSession::Unsubscribe(std::string topic) {
    impl_->Enqueue({Impl::CommandType::Unsubscribe, {}, std::move(topic), {},
                    MqttPublishQos::Qos0, std::nullopt});
}
void MqttSession::Publish(std::string topic, std::string payload, MqttPublishQos qos) {
    impl_->Enqueue({Impl::CommandType::Publish, {}, std::move(topic), std::move(payload), qos,
                    std::nullopt});
}
void MqttSession::Stop() { impl_->Stop(); }

} // namespace win32mqtt
