#pragma once

#include <windows.h>

#include <string>

#include "localization.h"

namespace win32mqtt {

class MessagePanel {
public:
    void Create(HWND parent, AppLanguage language);
    void Layout(const RECT& bounds) const;
    void UpdateText(AppLanguage language) const;
    bool HandleCommand(WORD id, WORD notification) const;
    void Append(const std::wstring& message) const;

private:
    HWND title_{};
    HWND output_{};
    HWND clear_button_{};
};

} // namespace win32mqtt
