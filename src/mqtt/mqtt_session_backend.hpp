#pragma once
#include "mqtt_endpoint.hpp"
#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
namespace win32mqtt {
// Optional transport boundary for deterministic execution of the production
// worker. Open must poll cancelled while waiting. Zero I/O means retry later.
class MqttSessionBackend {
public:
    virtual ~MqttSessionBackend() = default;
    virtual bool Open(const MqttEndpoint&, const std::function<bool()>& cancelled, std::string& error) = 0;
    virtual std::ptrdiff_t Send(const void*, std::size_t) = 0;
    virtual std::ptrdiff_t Receive(void*, std::size_t) = 0;
    virtual void Close() = 0;
    virtual std::chrono::steady_clock::time_point Now() const { return std::chrono::steady_clock::now(); }
};
}
