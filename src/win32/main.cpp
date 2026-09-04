#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

#include "app_settings.h"
#include "control_helpers.h"
#include "main_window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    // Keep every common control on the pre-visual-style rendering path.
    SetThemeAppProperties(0);

    const win32mqtt::AppSettings settings =
        win32mqtt::LoadAppSettings(win32mqtt::DefaultAppLanguage());
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
