#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace win32mqtt {

void UseClassicControlTheme(HWND control);
void UseClassicWindowFrame(HWND window);
// Collect sibling geometry changes and commit them together.
class ControlLayoutBatch {
public:
    ControlLayoutBatch();
    ~ControlLayoutBatch();
    ControlLayoutBatch(const ControlLayoutBatch&) = delete;
    ControlLayoutBatch& operator=(const ControlLayoutBatch&) = delete;
private:
    friend void PositionControl(HWND, int, int, int, int);
    struct Position { HWND control; int x, y, width, height; };
    std::vector<Position> positions_;
    ControlLayoutBatch* previous_;
};
void PositionControl(HWND control, int x, int y, int width, int height);
int ShowClassicMessageBox(HWND owner, const wchar_t* text, const wchar_t* caption, UINT type);

HWND AddControl(const wchar_t* class_name, DWORD style, int id, HWND parent,
                DWORD ex_style = 0);
HWND AddText(HWND parent, int id, const wchar_t* value);
HWND AddEdit(HWND parent, int id, const wchar_t* value, DWORD extra_style = 0);
HWND AddButton(HWND parent, int id, const wchar_t* value, DWORD extra_style = 0);
std::wstring ControlText(HWND control);

} // namespace win32mqtt
