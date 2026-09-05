#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

#include "app_settings.h"
#include "control_helpers.h"
#include "main_window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    // Keep every common control on the pre-visual-style rendering path.
    SetThemeAppProperties(0);

    const auto loaded = win32mqtt::LoadAppSettings(win32mqtt::DefaultAppLanguage());
    if (!loaded.Succeeded()) {
        const std::wstring message =
            L"配置文件无效或无法读取，程序未启动，原文件未修改。\r\n"
            L"Configuration is invalid or unreadable. Startup stopped; the file was not changed.\r\n\r\n" +
            loaded.path + L"\r\n\r\n" + loaded.error +
            L"\r\n\r\n请修正配置，或重命名配置文件后重新启动以使用默认设置。\r\n"
            L"Fix the file, or rename it and restart to use defaults.";
        win32mqtt::ShowClassicMessageBox(nullptr, message.c_str(), L"WIN32 MQTT", MB_OK | MB_ICONERROR);
        return 1;
    }
    const auto& settings = loaded.settings;
    const win32mqtt::AppLanguage language = settings.language;
    win32mqtt::MainWindow main_window(settings);

    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&common_controls);

    if (!main_window.Register(instance)) {
        win32mqtt::ShowClassicMessageBox(
            nullptr, win32mqtt::Text(language, win32mqtt::UiText::WindowClassRegistrationFailed).data(),
            win32mqtt::Text(language, win32mqtt::UiText::ApplicationTitle).data(),
            MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!main_window.Create(instance)) {
        win32mqtt::ShowClassicMessageBox(
            nullptr, win32mqtt::Text(language, win32mqtt::UiText::WindowCreationFailed).data(),
            win32mqtt::Text(language, win32mqtt::UiText::ApplicationTitle).data(),
            MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(main_window.Handle(), show_command);
    UpdateWindow(main_window.Handle());

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
