#include "mqtt_session.h"
#include "mqtt_capacity.hpp"
#include "mqtt_request.hpp"
#include "mqtt_topic.hpp"
#include "mqtt_disconnect.hpp"
#include "mqtt_connect_attempt.hpp"
#if defined(_WIN32)
#include "mqtt_dns.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#endif

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
    enum class CommandType { Connect, Disconnect, Publish };
    struct Command {
        CommandType type;
        MqttEndpoint endpoint;
        std::string first;
        std::string second;
        MqttPublishQos qos;
        std::optional<MqttLastWill> last_will;
        MqttConnectAttempt::Cancellation cancellation{};
        std::uint64_t operation = 0;
        std::uint64_t generation = 0;
    };

    explicit Impl(EventHandler handler, std::shared_ptr<MqttSessionBackend> backend) : backend(std::move(backend)), handler(std::move(handler)) {
        // Run may access every member, so start only after member initialization.
        worker = std::thread(&Impl::Run, this);
    }
    ~Impl() { Stop(); }

    MqttAdmission Enqueue(Command command) {
        const bool disconnecting = command.type == CommandType::Disconnect;
        const bool connecting = command.type == CommandType::Connect;
        if (!disconnecting) {
            bool fits;
            if (connecting) {
                fits = command.endpoint.host.size() <= 1024 && command.endpoint.port.size() <= 5 &&
                    MqttPacketFits({10, 2, command.first.size(), command.last_will ? 4u : 0u,
                        command.last_will ? command.last_will->topic.size() : 0u,
                        command.last_will ? command.last_will->payload.size() : 0u});
            } else if (command.type == CommandType::Publish) {
                fits = MqttPacketFits({2, command.first.size(), command.second.size(),
                    command.qos == MqttPublishQos::Qos0 ? 0u : 2u});
            } else {
                fits = false;
            }
            if (!fits) return MqttAdmission::TooLarge;
        }
        // All string lengths were bounded above, so this addition cannot overflow.
        const auto bytes = command.first.size() + command.second.size() + command.endpoint.host.size() +
            command.endpoint.port.size() + (command.last_will ? command.last_will->topic.size() + command.last_will->payload.size() : 0);
        { std::lock_guard<std::mutex> lock(mutex);
          if (stopped.load()) return MqttAdmission::Stopped;
          if (disconnecting) {
              if (latest_connect) latest_connect->cancelled.store(true);
              if (commands.ControlPending()) return MqttAdmission::Accepted;
          }
          const bool publishing = command.type == CommandType::Publish;
          if (publishing && outstanding_publishes >= 256) return MqttAdmission::QueueFull;
          if (publishing) { command.operation = ++request_sequence; command.generation = generation.load(); }
          auto cancellation = connecting ? std::make_shared<MqttConnectCancellation>() : nullptr;
          command.cancellation = cancellation;
          if (!commands.Push(std::move(command), bytes, disconnecting)) return MqttAdmission::QueueFull;
          if (publishing) ++outstanding_publishes;
          // Rejected replacement connections must not cancel the accepted request.
          if (connecting) {
              if (latest_connect) latest_connect->cancelled.store(true);
              latest_connect = std::move(cancellation);
          } }
        wake.notify_one();
        return MqttAdmission::Accepted;
    }
    void Stop() {
        if (!stopped.exchange(true)) {
            { std::lock_guard<std::mutex> lock(mutex);
              if (latest_connect) latest_connect->cancelled.store(true); }
            wake.notify_one();
        }
        if (worker.joinable()) worker.join();
    }
    void Emit(MqttEventType type, MqttConnectionState next_state, std::string detail = {},
              std::string topic = {}, std::string payload = {},
              MqttPublishQos publish_qos = MqttPublishQos::Qos0) {
        if (type == MqttEventType::PublishQueued || type == MqttEventType::PublishRejected) {
            std::lock_guard<std::mutex> lock(mutex);
            publish_results.push_back(MqttEvent{type, next_state, std::move(detail), std::move(topic),
                std::move(payload), publish_qos, false, false, 0, active_generation, active_operation});
            return;
        }
        if (handler) {
            handler(MqttEvent{type, next_state, std::move(detail), std::move(topic),
                              std::move(payload), publish_qos, false, false, 0, generation, ++event_id});
        }
    }
    static void Published(void** state, struct mqtt_response_publish* published) {
        auto* self = static_cast<Impl*>(*state);
        MqttEvent event{MqttEventType::MessageReceived, MqttConnectionState::Connected, {},
            std::string(static_cast<const char*>(published->topic_name), published->topic_name_size),
            std::string(static_cast<const char*>(published->application_message), published->application_message_size),
            static_cast<MqttPublishQos>(published->qos_level)};
        event.retain = published->retain_flag; event.dup = published->dup_flag;
        event.packet_id = published->packet_id; event.generation = self->generation;
        event.received_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            self->Now().time_since_epoch()).count();
        if (self->handler) self->handler(std::move(event));
    }
    static void Subscribed(void* state, const mqtt_queued_message* request, std::uint8_t code) {
        SubscriptionResult(state, request, code);
    }
    static void Unsubscribed(void* state, const mqtt_queued_message* request) {
        SubscriptionResult(state, request, std::nullopt);
    }
    static void SubscriptionResult(void* state, const mqtt_queued_message* request,
                                   std::optional<std::uint8_t> code) {
        auto* self = static_cast<Impl*>(state);
        mqtt_response header{};
        const auto header_size = mqtt_unpack_fixed_header(&header, request->start, request->size);
        // Both requests contain packet ID, string length and topic bytes;
        // only SUBSCRIBE has a trailing requested QoS byte.
        const std::size_t overhead = code.has_value() ? 5 : 4;
        if (header_size <= 0 || static_cast<std::size_t>(header_size) + overhead > request->size) return;
        const auto* body = request->start + header_size;
        const std::size_t length = (static_cast<std::size_t>(body[2]) << 8) | body[3];
        if (length != request->size - static_cast<std::size_t>(header_size) - overhead) return;
        const std::string topic(reinterpret_cast<const char*>(body + 4), length);
        const std::string detail = !code.has_value() ? ": unsubscription acknowledged by broker"
            : *code == MQTT_SUBACK_FAILURE ? ": subscription rejected by broker"
            : ": subscription accepted by broker (QoS " + std::to_string(*code) + ")";
        { std::lock_guard<std::mutex> lock(self->mutex);
          self->subscriptions.Complete(topic, request->packet_id, !code || *code != MQTT_SUBACK_FAILURE); }
        self->Emit(MqttEventType::Log, self->state, topic + detail);
    }
    static std::uint8_t PublishFlags(MqttPublishQos qos) {
        switch (qos) {
        case MqttPublishQos::Qos0: return MQTT_PUBLISH_QOS_0;
        case MqttPublishQos::Qos1: return MQTT_PUBLISH_QOS_1;
        case MqttPublishQos::Qos2: return MQTT_PUBLISH_QOS_2;
        }
        return MQTT_PUBLISH_QOS_0;
    }
    bool ConnectionPending(std::string& error) {
        const auto status = connect_attempt.Check(Now());
        if (stopped.load() || status == MqttConnectAttempt::Status::Cancelled) {
            error = "connection cancelled"; return false;
        }
        if (status == MqttConnectAttempt::Status::TimedOut) {
            error = connect_attempt.TimeoutMessage(); return false;
        }
        return true;
    }
    void WaitForConnectProgress() {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait_for(lock, std::chrono::milliseconds(20), [this] {
            return stopped.load() || connect_attempt.Cancelled();
        });
    }
    std::chrono::steady_clock::time_point Now() const { return backend ? backend->Now() : std::chrono::steady_clock::now(); }
    bool OpenSocket(const MqttEndpoint& endpoint, std::string& error) {
        if (backend) return backend->Open(endpoint, [this, &error] { return !ConnectionPending(error); }, error);
#if defined(_WIN32)
        MqttDnsQuery query;
        if (!ConnectionPending(error) || !query.Start(endpoint.host, endpoint.port, error)) return false;
        while (!query.Done()) {
            if (!ConnectionPending(error)) return false;
            WaitForConnectProgress();
        }
        if (!ConnectionPending(error)) return false;
        if (query.Error() != 0) { error = "DNS lookup failed"; return false; }
        connect_attempt.Enter(MqttConnectAttempt::Phase::Tcp, Now());
        // One deadline covers all addresses, not ten seconds per address.
        for (const auto* address = query.Addresses(); address; address = address->ai_next) {
            if (!ConnectionPending(error)) return false;
            const SOCKET candidate = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (candidate == INVALID_SOCKET) continue;
            u_long nonblocking = 1;
            if (ioctlsocket(candidate, FIONBIO, &nonblocking) != 0) { closesocket(candidate); continue; }
            const int result = connect(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen));
            if (result == 0) { socket_handle = candidate; return ConnectionPending(error); }
            const int socket_error = WSAGetLastError();
            if (socket_error == WSAEWOULDBLOCK || socket_error == WSAEINPROGRESS) {
                while (ConnectionPending(error)) {
                    fd_set writable; FD_ZERO(&writable); FD_SET(candidate, &writable);
                    fd_set failed; FD_ZERO(&failed); FD_SET(candidate, &failed);
                    TIMEVAL timeout{0, 20000};
                    const int ready = select(0, nullptr, &writable, &failed, &timeout);
                    if (ready == SOCKET_ERROR) break;
                    if (ready > 0) {
                        int connect_error = 0; int length = sizeof(connect_error);
                        if (getsockopt(candidate, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&connect_error), &length) == 0 &&
                            connect_error == 0 && FD_ISSET(candidate, &writable)) {
                            socket_handle = candidate; return ConnectionPending(error);
                        }
                        break;
                    }
                }
            }
            closesocket(candidate);
        }
        if (ConnectionPending(error)) error = "TCP connection failed";
        return false;
#else
        (void)endpoint; error = "a transport backend is required"; return false;
#endif
    }
#if WIN32MQTT_ENABLE_TLS
    bool ConfigureTransport(const MqttEndpoint& endpoint, std::string& error) {
        if (!ConnectionPending(error)) return false;
        if (endpoint.secure) {
            connect_attempt.Enter(MqttConnectAttempt::Phase::Tls, Now());
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
            // MQTT-C may compact its queue between SSL_write retries. The
            // retried bytes and length stay identical, but their address may move.
            SSL_set_mode(ssl, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
            BIO* socket_transport = BIO_new_socket(socket_handle, BIO_NOCLOSE);
            if (socket_transport == nullptr) { error = "unable to create TLS socket transport"; return false; }
            transport = BIO_push(transport, socket_transport);
            while (ConnectionPending(error)) {
                if (BIO_do_handshake(transport) == 1) return ConnectionPending(error);
                if (!BIO_should_retry(transport)) { error = "TLS handshake failed"; return false; }
                WaitForConnectProgress();
            }
            return false;
        }
        transport = BIO_new_socket(socket_handle, BIO_NOCLOSE);
        if (transport == nullptr) { error = "unable to create socket transport"; return false; }
        return true;
    }
#endif
    void CloseSocket() {
        client_initialized = false;
        { std::lock_guard<std::mutex> lock(mutex); subscriptions.Reset(generation); }
#if WIN32MQTT_ENABLE_TLS
        if (transport != nullptr) { BIO_free_all(transport); transport = nullptr; }
        if (ssl_context != nullptr) { SSL_CTX_free(ssl_context); ssl_context = nullptr; }
#endif
        if (backend) backend->Close();
#if defined(_WIN32)
        if (socket_handle != INVALID_SOCKET) { closesocket(socket_handle); socket_handle = INVALID_SOCKET; }
#endif
    }
    void CompleteCancelledConnect() {
        CloseSocket(); state = MqttConnectionState::Disconnected; awaiting_connack = false;
        Emit(MqttEventType::StateChanged, state);
    }
    void Fail(std::string error) { CloseSocket(); state = MqttConnectionState::Failed; awaiting_connack = false; Emit(MqttEventType::StateChanged, state, std::move(error)); }
    void ConnectNow(const MqttEndpoint& endpoint, const std::string& client_id,
                    const std::optional<MqttLastWill>& last_will,
                    MqttConnectAttempt::Cancellation cancellation) {
        if (stopped.load() || !cancellation || cancellation->cancelled.load()) return;
        if (last_will && !IsValidPublishTopic(last_will->topic)) {
            Fail("invalid MQTT Last Will topic");
            return;
        }
        ++generation;
        connect_attempt.Begin(std::move(cancellation), Now());
        CloseSocket(); state = MqttConnectionState::Connecting;
        Emit(MqttEventType::StateChanged, state, FormatMqttEndpointUri(endpoint));
        std::string error;
        if (!OpenSocket(endpoint, error)) {
            if (stopped.load() || connect_attempt.Cancelled()) CompleteCancelledConnect(); else Fail(std::move(error));
            return;
        }
        if (backend) {
            mqtt_reinit(&client, {}, send_buffer.data(), send_buffer.size(), recv_buffer.data(), recv_buffer.size());
            client.io_state = backend.get();
            client.send_callback = [](void* p, const void* bytes, size_t n) -> ssize_t {
                return static_cast<MqttSessionBackend*>(p)->Send(bytes, n);
            };
            client.recv_callback = [](void* p, void* bytes, size_t n) -> ssize_t {
                return static_cast<MqttSessionBackend*>(p)->Receive(bytes, n);
            };
            client.clock_callback = [](void* p) -> mqtt_pal_time_t {
                return std::chrono::duration_cast<std::chrono::seconds>(
                    static_cast<MqttSessionBackend*>(p)->Now().time_since_epoch()).count();
            };
        } else {
#if WIN32MQTT_ENABLE_TLS
        if (!ConfigureTransport(endpoint, error)) {
            if (stopped.load() || connect_attempt.Cancelled()) CompleteCancelledConnect(); else Fail(std::move(error));
            return;
        }
        mqtt_reinit(&client, transport, send_buffer.data(), send_buffer.size(), recv_buffer.data(), recv_buffer.size());
#else
        if (endpoint.secure) { Fail("mqtts:// requires a TLS-enabled build"); return; }
        mqtt_reinit(&client, socket_handle, send_buffer.data(), send_buffer.size(), recv_buffer.data(), recv_buffer.size());
#endif
        }
        client_initialized = true;
        if (stopped.load() || connect_attempt.Cancelled()) { CompleteCancelledConnect(); return; }
        const char* will_topic = last_will ? last_will->topic.c_str() : nullptr;
        const void* will_payload = last_will ? last_will->payload.data() : nullptr;
        const std::size_t will_payload_size = last_will ? last_will->payload.size() : 0;
        const std::uint8_t connect_flags = static_cast<std::uint8_t>(
            MQTT_CONNECT_CLEAN_SESSION |
            (last_will ? MQTT_CONNECT_WILL_QOS_0 : 0));
        // mqtt_init_reconnect leaves the mutex unlocked. Unlike ordinary MQTT-C
        // operations, mqtt_connect requires ownership and releases it on every return.
        // Acquire here, after all cancellation exits; do not wrap this in a lock guard.
        MQTT_PAL_MUTEX_LOCK(&client.mutex);
        const enum MQTTErrors result = mqtt_connect(
            &client, client_id.empty() ? nullptr : client_id.c_str(), will_topic, will_payload,
            will_payload_size, nullptr, nullptr, connect_flags, 60);
        if (result != MQTT_OK) { Fail(mqtt_error_str(result)); return; }
        connect_attempt.Enter(MqttConnectAttempt::Phase::Connack, Now());
        awaiting_connack = true;
    }
    void FinishDisconnect(std::string detail = {}) {
        CloseSocket();
        awaiting_connack = false;
        state = MqttConnectionState::Disconnected;
        Emit(MqttEventType::StateChanged, state, std::move(detail));
    }
    void BeginDisconnect() {
        if (state == MqttConnectionState::Disconnecting) return;
        if (!client_initialized || state != MqttConnectionState::Connected) {
            FinishDisconnect();
            return;
        }
        disconnect.Begin(Now());
        awaiting_connack = false;
        state = MqttConnectionState::Disconnecting;
        Emit(MqttEventType::StateChanged, state);
    }
    bool CheckConnecting() {
        if (!awaiting_connack) return true;
        std::string error;
        if (ConnectionPending(error)) return true;
        if (stopped.load() || connect_attempt.Cancelled()) CompleteCancelledConnect();
        else Fail(std::move(error));
        return false;
    }
    void Sync() {
        if (state == MqttConnectionState::Disconnecting) {
            switch (disconnect.Poll(client, Now())) {
            case MqttDisconnect::Result::Pending: return;
            case MqttDisconnect::Result::Sent: FinishDisconnect(); return;
            case MqttDisconnect::Result::TimedOut:
                FinishDisconnect("DISCONNECT send timed out; connection closed"); return;
            case MqttDisconnect::Result::Failed:
                FinishDisconnect("DISCONNECT send failed; connection closed"); return;
            }
        }
        if (!client_initialized ||
            (state != MqttConnectionState::Connecting && state != MqttConnectionState::Connected)) return;
        if (!CheckConnecting()) return;
        const enum MQTTErrors result = mqtt_sync(&client);
        if (!CheckConnecting()) return;
        if (result != MQTT_OK || client.error != MQTT_OK) {
            const auto error = result != MQTT_OK ? result : client.error;
            Fail(error == MQTT_ERROR_RECV_BUFFER_TOO_SMALL ? "incoming MQTT packet exceeds 8192-byte receive limit" : mqtt_error_str(error));
            return;
        }
        if (awaiting_connack && mqtt_mq_find(&client.mq, MQTT_CONTROL_CONNECT, nullptr) == nullptr) {
            awaiting_connack = false; state = MqttConnectionState::Connected; Emit(MqttEventType::StateChanged, state);
        }
    }
    void Handle(Command command) {
        active_operation = command.operation;
        active_generation = command.generation;
        switch (command.type) {
        case CommandType::Connect:
            if (!command.cancellation || command.cancellation->cancelled.load()) break;
            if (state == MqttConnectionState::Connected || state == MqttConnectionState::Disconnecting) {
                next_connect = std::move(command);
                BeginDisconnect();
            } else {
                ConnectNow(command.endpoint, command.first, command.last_will, std::move(command.cancellation));
            }
            break;
        case CommandType::Disconnect:
            next_connect.reset();
            BeginDisconnect();
            break;
        case CommandType::Publish: {
            if (command.generation != generation.load()) {
                Emit(MqttEventType::PublishRejected, state, "connection replaced before publish executed", command.first, {}, command.qos);
                break;
            }
            if (!IsValidPublishTopic(command.first)) {
                Emit(MqttEventType::PublishRejected, state, "invalid MQTT publish topic",
                     std::move(command.first), std::move(command.second), command.qos);
                break;
            }
            if (state != MqttConnectionState::Connected) {
                Emit(MqttEventType::PublishRejected, state, "not connected", std::move(command.first),
                     std::move(command.second), command.qos);
                break;
            }
            const auto result = MqttUserRequest(client, [&] {
                return mqtt_publish(&client, command.first.c_str(), command.second.data(),
                                    command.second.size(), PublishFlags(command.qos));
            });
            if (result != MQTT_OK) {
                Emit(MqttEventType::PublishRejected, state,
                     result == MQTT_ERROR_SEND_BUFFER_IS_FULL ? "send queue full; retry later" : mqtt_error_str(result),
                     std::move(command.first), std::move(command.second), command.qos);
                break;
            }
            Emit(MqttEventType::PublishQueued, state, {}, std::move(command.first),
                 std::move(command.second), command.qos);
            break;
        }
        }
    }
    void Run() {
#if defined(_WIN32)
        WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { Emit(MqttEventType::StateChanged, MqttConnectionState::Failed, "Winsock initialization failed"); return; }
#endif
        mqtt_init_reconnect(&client, nullptr, nullptr, Published); client.publish_response_callback_state = this;
        client.subscribe_response_callback = Subscribed;
        client.subscribe_response_callback_state = this;
        client.unsubscribe_response_callback = Unsubscribed;
        client.unsubscribe_response_callback_state = this;
        while (!stopping || state == MqttConnectionState::Disconnecting) {
            std::deque<Command> pending;
            { std::unique_lock<std::mutex> lock(mutex);
              wake.wait_for(lock, std::chrono::milliseconds(25), [this] {
                  return !commands.Empty() || (stopped.load() && !stopping);
              });
              for (unsigned count = 0; count < 32 && !commands.Empty(); ++count) {
                  pending.push_back(commands.Pop());
              } }
            for (Command& command : pending) {
                if (stopped.load()) {
                    if (command.type == CommandType::Publish) {
                        active_operation = command.operation;
                        active_generation = command.generation;
                        Emit(MqttEventType::PublishRejected, state, "session stopped", command.first, {}, command.qos);
                    }
                    continue;
                }
                Handle(std::move(command));
            }
            // Stop takes priority over queued work and reuses an existing
            // disconnect deadline instead of restarting its timeout.
            if (stopped.load() && !stopping) {
                stopping = true;
                next_connect.reset();
                BeginDisconnect();
            }
            Sync();
            if (state == MqttConnectionState::Connected && !stopped.load()) {
                std::lock_guard<std::mutex> lock(mutex);
                subscriptions.Advance(generation, [&](const std::string& topic, bool desired) {
                    const auto result = MqttUserRequest(client, [&] {
                        return desired ? mqtt_subscribe(&client, topic.c_str(), 0)
                                       : mqtt_unsubscribe(&client, topic.c_str());
                    });
                    return result == MQTT_OK ? static_cast<int>(mqtt_mq_get(&client.mq, mqtt_mq_length(&client.mq) - 1)->packet_id)
                        : result == MQTT_ERROR_SEND_BUFFER_IS_FULL ? 0 : -1;
                });
            }
            if (!stopped.load() && state == MqttConnectionState::Disconnected && next_connect) {
                Command command = std::move(*next_connect);
                next_connect.reset();
                ConnectNow(command.endpoint, command.first, command.last_will, std::move(command.cancellation));
            }
        }
        // Finalize accepted commands that Stop prevented from executing.
        for (;;) {
            std::optional<Command> command;
            { std::lock_guard<std::mutex> lock(mutex);
              if (commands.Empty()) break;
              command = commands.Pop(); }
            if (command->type == CommandType::Publish) {
                active_operation = command->operation;
                active_generation = command->generation;
                Emit(MqttEventType::PublishRejected, state, "session stopped", command->first, {}, command->qos);
            }
        }
        CloseSocket();
#if defined(_WIN32)
        DeleteCriticalSection(&client.mutex);
        WSACleanup();
#else
        MQTT_PAL_MUTEX_DESTROY(&client.mutex);
#endif
    }

    std::shared_ptr<MqttSessionBackend> backend;
    std::vector<MqttEvent> publish_results;
    std::size_t outstanding_publishes = 0;
    std::uint64_t request_sequence = 0, active_operation = 0, active_generation = 0;
    MqttSubscriptions subscriptions;
    std::atomic<std::uint64_t> generation{0};
    std::uint64_t event_id = 0;
    EventHandler handler; std::mutex mutex; std::condition_variable wake; MqttCommandQueue<Command> commands; std::thread worker;
    MqttConnectAttempt connect_attempt;
    MqttConnectAttempt::Cancellation latest_connect; // Protected by mutex.
    std::atomic<bool> stopped{false}; bool stopping = false; bool awaiting_connack = false; bool client_initialized = false;
    MqttDisconnect disconnect;
    std::optional<Command> next_connect;
#if defined(_WIN32)
    SOCKET socket_handle = INVALID_SOCKET;
#else
    mqtt_pal_socket_handle socket_handle = 0;
#endif
#if WIN32MQTT_ENABLE_TLS
    BIO* transport = nullptr;
    SSL_CTX* ssl_context = nullptr;
#endif
    MqttConnectionState state = MqttConnectionState::Disconnected; struct mqtt_client client{};
    alignas(mqtt_queued_message) std::array<std::uint8_t, 8192> send_buffer{};
    std::array<std::uint8_t, MqttReceiveCapacity> recv_buffer{};
};

MqttSession::MqttSession(EventHandler handler, std::shared_ptr<MqttSessionBackend> backend)
    : impl_(std::make_unique<Impl>(std::move(handler), std::move(backend))) {}
MqttSession::~MqttSession() = default;
MqttAdmission MqttSession::Connect(MqttEndpoint endpoint, std::string client_id,
                          std::optional<MqttLastWill> last_will) {
    return impl_->Enqueue({Impl::CommandType::Connect, std::move(endpoint), std::move(client_id), {},
                    MqttPublishQos::Qos0, std::move(last_will)});
}
void MqttSession::Disconnect() {
    impl_->Enqueue({Impl::CommandType::Disconnect, {}, {}, {}, MqttPublishQos::Qos0,
                    std::nullopt});
}
MqttAdmission MqttSession::Subscribe(std::string topic) {
    if (!IsValidSubscriptionFilter(topic) || !MqttPacketFits({5, topic.size()})) return MqttAdmission::TooLarge;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stopped.load()) return MqttAdmission::Stopped;
    std::vector<std::string> topics;
    for (const auto& r : impl_->subscriptions.Snapshot()) if (r.desired && r.topic != topic) topics.push_back(r.topic);
    topics.push_back(std::move(topic));
    return impl_->subscriptions.SetDesired(topics) ? MqttAdmission::Accepted : MqttAdmission::QueueFull;
}
MqttAdmission MqttSession::Unsubscribe(std::string topic) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stopped.load()) return MqttAdmission::Stopped;
    std::vector<std::string> topics;
    for (const auto& r : impl_->subscriptions.Snapshot()) if (r.desired && r.topic != topic) topics.push_back(r.topic);
    return impl_->subscriptions.SetDesired(topics) ? MqttAdmission::Accepted : MqttAdmission::QueueFull;
}
MqttAdmission MqttSession::Publish(std::string topic, std::string payload, MqttPublishQos qos) {
    return impl_->Enqueue({Impl::CommandType::Publish, {}, std::move(topic), std::move(payload), qos,
                    std::nullopt});
}
void MqttSession::Stop() { impl_->Stop(); }

} // namespace win32mqtt

namespace win32mqtt {
MqttAdmission MqttSession::SetSubscriptions(std::vector<std::string> topics) {
    for (const auto& topic : topics)
        if (!IsValidSubscriptionFilter(topic) || !MqttPacketFits({5, topic.size()})) return MqttAdmission::TooLarge;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stopped.load()) return MqttAdmission::Stopped;
    return impl_->subscriptions.SetDesired(topics) ? MqttAdmission::Accepted : MqttAdmission::QueueFull;
}
std::vector<MqttSubscriptionStatus> MqttSession::Subscriptions() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->subscriptions.Snapshot();
}
}

namespace win32mqtt {
std::vector<MqttEvent> MqttSession::TakePublishResults() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<MqttEvent> results;
    results.swap(impl_->publish_results);
    impl_->outstanding_publishes -= results.size();
    return results;
}
}
