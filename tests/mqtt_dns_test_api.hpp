#pragma once

#include <cstddef>
#include <cstdint>

#define CALLBACK
#define WSAAPI
using DWORD = std::uint32_t;
using INT = int;
using HANDLE = void*;
using LPHANDLE = HANDLE*;
using HMODULE = void*;
using FARPROC = void (*)();
struct OVERLAPPED { void* Pointer = nullptr; HANDLE hEvent = nullptr; };
struct TIMEVAL { long tv_sec; long tv_usec; };
struct WSADATA {};
struct ADDRINFOEXW {
    int ai_family = 0;
    int ai_socktype = 0;
};
using CompletionRoutine = void (*)(DWORD, DWORD, OVERLAPPED*);
constexpr int AF_UNSPEC = 0;
constexpr int SOCK_STREAM = 1;
constexpr DWORD NS_DNS = 12;
constexpr int WSA_IO_PENDING = 997;
constexpr unsigned CP_UTF8 = 65001;
constexpr DWORD MB_ERR_INVALID_CHARS = 8;
constexpr unsigned MAKEWORD(unsigned a, unsigned b) { return a | (b << 8); }
HMODULE GetModuleHandleW(const wchar_t*);
FARPROC GetProcAddress(HMODULE, const char*);
int MultiByteToWideChar(unsigned, DWORD, const char*, int, wchar_t*, int);
int WSAStartup(unsigned, WSADATA*);
int WSACleanup();
void FreeAddrInfoExW(ADDRINFOEXW*);
int GetAddrInfoExW(const wchar_t*, const wchar_t*, DWORD, void*, const ADDRINFOEXW*,
                  ADDRINFOEXW**, TIMEVAL*, OVERLAPPED*, CompletionRoutine, HANDLE*);
