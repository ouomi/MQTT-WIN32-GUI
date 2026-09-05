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

namespace win32mqtt {

// Windows 8+ exports this API; older MinGW headers omit its declaration.
using CancelDns = INT (WSAAPI*)(LPHANDLE);

// Owns all storage touched by asynchronous Winsock. The callback owns a reference
// independent of the session, including a Winsock startup reference, so cancellation
// never requires waiting for DNS completion during session destruction.
class MqttDnsQuery {
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
            if (winsock_started) WSACleanup();
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
        if (!state_->done.load()) cancel_(&state_->cancellation);
        state_->Release();
    }
    bool Start(const std::string& host, const std::string& port, std::string& error) {
        if (state_) { error = "DNS query already started"; return false; }
        // Resolve cancellation dynamically so unsupported systems fail explicitly
        // instead of falling back to an unbounded synchronous lookup.
        const HMODULE module = GetModuleHandleW(L"ws2_32.dll");
        const auto symbol = module ? GetProcAddress(module, "GetAddrInfoExCancel") : nullptr;
        static_assert(sizeof(cancel_) == sizeof(symbol));
        std::memcpy(&cancel_, &symbol, sizeof(cancel_));
        if (!cancel_) { error = "asynchronous DNS requires Windows 8 or later"; return false; }
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
    const ADDRINFOEXW* Addresses() const { return state_->addresses; }
private:
    State* state_ = nullptr;
    CancelDns cancel_ = nullptr;
};

} // namespace win32mqtt
