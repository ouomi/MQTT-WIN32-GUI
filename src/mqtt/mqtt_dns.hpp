#pragma once

#if defined(WIN32MQTT_DNS_TEST_API)
#include "mqtt_dns_test_api.hpp"
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <system_error>

namespace win32mqtt {

// Windows 8+ exports this API; older MinGW headers omit its declaration.
using CancelDns = INT (WSAAPI*)(LPHANDLE);

// The callback or fallback worker owns storage independently of the session,
// including a Winsock startup reference. Session destruction never waits for DNS.
class MqttDnsQuery {
    // Process-wide bound, including queries abandoned by destroyed sessions.
    inline static std::atomic<unsigned> fallback_queries_{0};
    static constexpr unsigned MaxFallbackQueries = 4;
    struct State;
    struct Completion {
        OVERLAPPED overlapped{};
        State* owner = nullptr;
    };
    struct State {
        Completion completion{};
        TIMEVAL timeout{5, 0};
        std::atomic<unsigned> references{2};
        std::atomic<bool> done{false};
        DWORD error = 0;
        ADDRINFOEXW* addresses = nullptr;
        ADDRINFOW* fallback_addresses = nullptr;
        bool fallback_slot = false;
        ADDRINFOEXW hints{};
        HANDLE cancellation = nullptr;
        std::wstring host;
        std::wstring service;
        bool winsock_started = false;
        State(std::wstring name, std::wstring port) : host(std::move(name)), service(std::move(port)) {
            completion.owner = this;
        }
        ~State() {
            if (addresses) FreeAddrInfoExW(addresses);
            if (fallback_addresses) FreeAddrInfoW(fallback_addresses);
            if (winsock_started) WSACleanup();
            if (fallback_slot) fallback_queries_.fetch_sub(1);
        }
        void Release() { if (references.fetch_sub(1) == 1) delete this; }
    };
    static void CALLBACK Complete(DWORD error, DWORD, OVERLAPPED* overlapped) {
        // OVERLAPPED is the first member of this standard-layout context.
        auto* state = reinterpret_cast<Completion*>(overlapped)->owner;
        state->error = error;
        state->done.store(true);
        state->Release();
    }
public:
    MqttDnsQuery() = default;
    MqttDnsQuery(const MqttDnsQuery&) = delete;
    MqttDnsQuery& operator=(const MqttDnsQuery&) = delete;
    ~MqttDnsQuery() {
        if (!state_) return;
        if (cancel_ && !state_->done.load()) cancel_(&state_->cancellation);
        state_->Release();
    }
    bool Start(const std::string& host, const std::string& port, std::string& error) {
        if (state_) { error = "DNS query already started"; return false; }
        // Windows 7 uses a bounded, independently owned synchronous worker.
        const HMODULE module = GetModuleHandleW(L"ws2_32.dll");
        const auto symbol = module ? GetProcAddress(module, "GetAddrInfoExCancel") : nullptr;
        static_assert(sizeof(cancel_) == sizeof(symbol));
        std::memcpy(&cancel_, &symbol, sizeof(cancel_));
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host.c_str(), -1, nullptr, 0);
        if (!size) { error = "invalid DNS hostname"; return false; }
        std::wstring name(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host.c_str(), -1, name.data(), size);
        state_ = new State(std::move(name), std::wstring(port.begin(), port.end()));
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            state_->done.store(true);
            state_->Release();
            error = "DNS Winsock initialization failed";
            return false;
        }
        state_->winsock_started = true;
        if (!cancel_) {
            unsigned count = fallback_queries_.load();
            while (count < MaxFallbackQueries &&
                   !fallback_queries_.compare_exchange_weak(count, count + 1)) {}
            if (count >= MaxFallbackQueries) {
                state_->done.store(true);
                state_->Release();
                error = "DNS resolver busy; please retry later";
                return false;
            }
            state_->fallback_slot = true;
            std::thread resolver;
            try {
                resolver = std::thread([state = state_] {
                    ADDRINFOW hints{};
                    hints.ai_family = AF_UNSPEC;
                    hints.ai_socktype = SOCK_STREAM;
                    state->error = static_cast<DWORD>(GetAddrInfoW(state->host.c_str(),
                        state->service.c_str(), &hints, &state->fallback_addresses));
                    state->done.store(true);
                    state->Release();
                });
            } catch (const std::system_error&) {
                state_->done.store(true);
                state_->Release();
                error = "unable to start DNS resolver thread";
                return false;
            }
            // Never join this worker: caller cancellation/timeout abandons its result.
            resolver.detach();
            return true;
        }
        state_->hints.ai_family = AF_UNSPEC;
        state_->hints.ai_socktype = SOCK_STREAM;
        // A provider timeout complements the caller's monotonic deadline.
        const int result = GetAddrInfoExW(state_->host.c_str(), state_->service.c_str(), NS_DNS,
            nullptr, &state_->hints, &state_->addresses, &state_->timeout, &state_->completion.overlapped,
            Complete, &state_->cancellation);
        if (result != WSA_IO_PENDING) {
            // Synchronous completion does not schedule the completion routine.
            state_->error = static_cast<DWORD>(result);
            state_->done.store(true);
            state_->Release();
        }
        return true;
    }
    bool Done() const { return state_->done.load(); }
    DWORD Error() const { return state_->error; } // Read only after Done().
    // Both Winsock result types expose the same socket fields. Only call after Done().
    template<class Visitor>
    bool WithAddresses(Visitor&& visitor) const {
        if (cancel_) return visitor(static_cast<const ADDRINFOEXW*>(state_->addresses));
        return visitor(static_cast<const ADDRINFOW*>(state_->fallback_addresses));
    }
private:
    State* state_ = nullptr;
    CancelDns cancel_ = nullptr;
};

} // namespace win32mqtt
