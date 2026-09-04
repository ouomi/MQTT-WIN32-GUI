#pragma once

#include <windows.h>

#include <memory>

#include "app_settings.h"

namespace win32mqtt {

class MainWindow {
public:
    explicit MainWindow(AppSettings settings);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool Register(HINSTANCE instance) const;
    bool Create(HINSTANCE instance);
    HWND Handle() const noexcept;

private:
    struct Impl;
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    std::unique_ptr<Impl> impl_;
};

} // namespace win32mqtt
