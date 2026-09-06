#include "mqtt/mqtt_dns.hpp"
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <vector>

static int startup_result;
static int query_result;
static bool supported;
static bool callback_during_start;
static std::atomic<unsigned> startup_refs;
static std::atomic<unsigned> frees;
static unsigned cancels;
static std::atomic<bool> fallback_release{false};
static std::atomic<unsigned> fallback_entered{0};
static int fallback_result = 0;
static OVERLAPPED* pending;
static CompletionRoutine callback;
static ADDRINFOEXW** result_slot;
static const wchar_t* stored_host;
static const wchar_t* stored_port;
static TIMEVAL* stored_timeout;
static const ADDRINFOEXW* stored_hints;

static void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}
static void Complete(DWORD error) {
    Check(pending && callback, "callback context exists");
    Check(std::wcscmp(stored_host, L"broker.example") == 0, "host survives owner destruction");
    Check(std::wcscmp(stored_port, L"1883") == 0, "service survives owner destruction");
    Check(stored_timeout->tv_sec == 5 && stored_hints->ai_socktype == SOCK_STREAM,
          "timeout and hints survive owner destruction");
    *result_slot = new ADDRINFOEXW;
    auto* overlapped = pending;
    pending = nullptr;
    callback(error, 0, overlapped);
}
static int Cancel(HANDLE* handle) {
    Check(*handle != nullptr, "valid cancellation handle");
    ++cancels;
    *handle = nullptr;
    return 0; // Deliberately defer callback until after owner destruction.
}
HMODULE GetModuleHandleW(const wchar_t*) { return reinterpret_cast<HMODULE>(1); }
FARPROC GetProcAddress(HMODULE, const char*) {
    FARPROC result = nullptr;
    auto cancel = &Cancel;
    if (supported) std::memcpy(&result, &cancel, sizeof(result));
    return result;
}
int MultiByteToWideChar(unsigned, DWORD, const char* input, int, wchar_t* output, int size) {
    const int count = static_cast<int>(std::strlen(input)) + 1;
    if (output) {
        Check(size >= count, "UTF buffer size");
        for (int i = 0; i < count; ++i) output[i] = static_cast<unsigned char>(input[i]);
    }
    return count;
}
int WSAStartup(unsigned, WSADATA*) { if (!startup_result) ++startup_refs; return startup_result; }
int WSACleanup() { Check(startup_refs > 0, "Winsock ownership"); --startup_refs; return 0; }
void FreeAddrInfoExW(ADDRINFOEXW* result) { ++frees; delete result; }
int GetAddrInfoExW(const wchar_t* host, const wchar_t* port, DWORD, void*, const ADDRINFOEXW* hints,
                  ADDRINFOEXW** result, TIMEVAL* timeout, OVERLAPPED* overlapped,
                  CompletionRoutine completion, HANDLE* handle) {
    if (query_result != WSA_IO_PENDING) {
        if (!query_result) *result = new ADDRINFOEXW;
        return query_result;
    }
    stored_host = host; stored_port = port; stored_hints = hints; stored_timeout = timeout;
    result_slot = result; pending = overlapped; callback = completion;
    *handle = reinterpret_cast<HANDLE>(1);
    if (callback_during_start) Complete(0);
    return query_result;
}
int GetAddrInfoW(const wchar_t* host, const wchar_t* port, const ADDRINFOW* hints,
                 ADDRINFOW** result) {
    ++fallback_entered;
    while (!fallback_release.load()) std::this_thread::yield();
    Check(std::wcscmp(host, L"broker.example") == 0 && std::wcscmp(port, L"1883") == 0,
          "fallback input survives owner destruction");
    Check(hints->ai_family == AF_UNSPEC && hints->ai_socktype == SOCK_STREAM, "fallback hints");
    if (!fallback_result) *result = new ADDRINFOW;
    return fallback_result;
}
void FreeAddrInfoW(ADDRINFOW* result) { ++frees; delete result; }
template<class Predicate>
static void Await(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!predicate()) {
        Check(std::chrono::steady_clock::now() < deadline, "worker progress deadline");
        std::this_thread::yield();
    }
}
static void Reset() {
    Check(startup_refs == 0 && pending == nullptr, "previous query released all resources");
    startup_result = 0; query_result = 0; supported = true; callback_during_start = false;
    frees = 0; cancels = 0;
}
static void TestImmediate(int code) {
    Reset(); query_result = code;
    {
        win32mqtt::MqttDnsQuery query;
        std::string error;
        Check(query.Start("broker.example", "1883", error), "immediate query starts");
        Check(query.Done() && query.Error() == static_cast<DWORD>(code), "immediate result published");
        Check(!query.Start("other", "1883", error), "double start rejected");
    }
    Check(startup_refs == 0 && cancels == 0 && frees == (code ? 0u : 1u), "immediate cleanup");
}
static void TestPending(bool abandon, bool inline_completion) {
    Reset(); query_result = WSA_IO_PENDING; callback_during_start = inline_completion;
    {
        win32mqtt::MqttDnsQuery query;
        std::string error;
        Check(query.Start("broker.example", "1883", error), "asynchronous query starts");
        if (!inline_completion) Check(!query.Done(), "pending query observable without waiting");
        if (!abandon && !inline_completion) {
            std::thread resolver([] { Complete(0); });
            resolver.join();
        }
        if (!abandon) Check(query.Done() && query.Error() == 0 && query.WithAddresses([](const auto* addresses) { return addresses != nullptr; }), "async result delivered");
    }
    if (abandon) {
        Check(cancels == 1 && startup_refs == 1 && frees == 0,
              "destruction cancels without waiting and keeps callback resources alive");
        std::thread resolver([] { Complete(995); });
        resolver.join();
    }
    Check(startup_refs == 0 && frees == 1, "callback and owner release exactly once");
}
static void TestFallback() {
    Reset(); supported = false; fallback_release = false; fallback_entered = 0;
    std::vector<std::unique_ptr<win32mqtt::MqttDnsQuery>> queries;
    std::string error;
    for (unsigned i = 0; i < 4; ++i) {
        auto query = std::make_unique<win32mqtt::MqttDnsQuery>();
        Check(query->Start("broker.example", "1883", error), "fallback starts");
        Check(!query->Done(), "fallback does not block caller");
        queries.push_back(std::move(query));
    }
    Await([] { return fallback_entered.load() == 4; });
    queries.clear(); // Simulates cancellation/timeout/session destruction while DNS is blocked.
    Check(startup_refs == 4 && frees == 0, "abandoned fallback workers own their resources");
    {
        win32mqtt::MqttDnsQuery excess;
        Check(!excess.Start("broker.example", "1883", error) && error.find("busy") != std::string::npos,
              "abandoned workers still count against global limit");
    }
    fallback_release = true;
    Await([] { return startup_refs.load() == 0; });
    Check(frees == 4, "late fallback results freed");
    // Admission may follow WSACleanup by a few instructions; wait for slot recovery.
    for (int code : {0, 11001}) {
        fallback_result = code;
        std::unique_ptr<win32mqtt::MqttDnsQuery> query;
        Await([&] {
            query = std::make_unique<win32mqtt::MqttDnsQuery>();
            return query->Start("broker.example", "1883", error);
        });
        Await([&] { return query->Done(); });
        Check(query->Error() == static_cast<DWORD>(code), "fallback completion status");
        Check(query->WithAddresses([&](const auto* addresses) { return (addresses != nullptr) == (code == 0); }),
              "fallback result available to transport");
        query.reset();
        Await([] { return startup_refs.load() == 0; });
    }
    fallback_result = 0;
}
static void TestCancelRace() {
    for (unsigned i = 0; i < 100; ++i) {
        Reset(); query_result = WSA_IO_PENDING;
        auto query = std::make_unique<win32mqtt::MqttDnsQuery>();
        std::string error;
        Check(query->Start("broker.example", "1883", error), "start racing query");
        std::atomic<bool> run{false};
        std::thread resolver([&run] {
            while (!run.load()) std::this_thread::yield();
            Complete(0);
        });
        run.store(true);
        query.reset();
        resolver.join();
        Check(startup_refs == 0 && frees == 1, "concurrent destruction and completion release once");
    }
}

int main() {
    TestImmediate(0);
    TestImmediate(11001);
    TestPending(false, false);
    TestPending(true, false);
    TestPending(false, true);
    TestCancelRace();
    TestFallback();
    Reset(); startup_result = 1;
    {
        win32mqtt::MqttDnsQuery query;
        std::string error;
        Check(!query.Start("broker.example", "1883", error), "Winsock initialization failure");
    }
    Check(startup_refs == 0 && cancels == 0, "startup failure cleanup");
    std::cout << "Asynchronous DNS ownership tests passed\n";
}
