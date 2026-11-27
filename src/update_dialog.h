#pragma once
// The updater's dialog: a standard Windows dialog, built in memory (no resource
// file), with the offer, the changes in a read-only text box that wraps whole
// words and reads line by line, and Update now (default) and Skip buttons.
#include "platform.h"
#include <string>
#include <vector>
namespace a11y {
namespace update_dialog_detail {
enum : int { heading = 100, changes = 101, changes_label = 102 };
struct Text {
    std::wstring heading, changes;
};
// A dialog template: DLGTEMPLATE, then each DLGITEMTEMPLATE on a DWORD boundary.
class Template {
  public:
    Template(const wchar_t *title, short width, short height) {
        DWORD style = DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
        dword(style);
        dword(0); // extended style
        count_ = words_.size();
        word(0); // item count, filled in as items are added
        word(0), word(0), word(width), word(height);
        word(0), word(0); // no menu, default class
        string(title);
        word(9);
        string(L"Segoe UI");
    }
    void item(DWORD style, short x, short y, short cx, short cy, int id, WORD atom,
              const wchar_t *text) {
        while (words_.size() % 2)
            word(0);
        dword(style | WS_CHILD | WS_VISIBLE);
        dword(0);
        word(x), word(y), word(cx), word(cy);
        word(WORD(id));
        word(0xffff), word(atom);
        string(text);
        word(0); // no creation data
        ++words_[count_];
    }
    const DLGTEMPLATE *get() const {
        return reinterpret_cast<const DLGTEMPLATE *>(words_.data());
    }

  private:
    void word(WORD w) {
        words_.push_back(w);
    }
    void dword(DWORD d) {
        word(WORD(d & 0xffff));
        word(WORD(d >> 16));
    }
    void string(const wchar_t *s) {
        for (; *s; ++s)
            word(WORD(*s));
        word(0);
    }
    std::vector<WORD> words_;
    size_t count_{};
};
inline INT_PTR CALLBACK procedure(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_INITDIALOG: {
            auto *text = reinterpret_cast<Text *>(lparam);
            SetDlgItemTextW(dialog, heading, text->heading.c_str());
            SetDlgItemTextW(dialog, changes, text->changes.c_str());
            SetForegroundWindow(dialog);
            SetFocus(GetDlgItem(dialog, IDOK));
            return FALSE; // focus set here
        }
        case WM_COMMAND:
            // Tabbing into the text box would select all of it; put the caret at the start.
            if (LOWORD(wparam) == changes && HIWORD(wparam) == EN_SETFOCUS) {
                SendMessageW(reinterpret_cast<HWND>(lparam), EM_SETSEL, 0, 0);
                return TRUE;
            }
            if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) {
                EndDialog(dialog, LOWORD(wparam));
                return TRUE;
            }
            break;
        case WM_CLOSE:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}
// Edit controls need CRLF line breaks.
inline std::wstring crlf(const std::string &text) {
    std::wstring out;
    for (auto c : wide(text)) {
        if (c == L'\n')
            out += L'\r';
        out += c;
    }
    return out;
}
} // namespace update_dialog_detail
// Shows the offer; true when the player chooses Update now.
inline bool update_dialog(const std::string &heading, const std::string &changes) {
    using namespace update_dialog_detail;
    Template dialog(L"Blind Rage update", 300, 222);
    dialog.item(SS_LEFT, 7, 7, 286, 20, update_dialog_detail::heading, 0x0082, L"");
    dialog.item(SS_LEFT, 7, 30, 286, 10, changes_label, 0x0082, L"&Changes:");
    dialog.item(ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
                7, 42, 286, 150, update_dialog_detail::changes, 0x0081, L"");
    dialog.item(BS_DEFPUSHBUTTON | WS_TABSTOP, 175, 199, 58, 15, IDOK, 0x0080, L"&Update now");
    dialog.item(BS_PUSHBUTTON | WS_TABSTOP, 237, 199, 56, 15, IDCANCEL, 0x0080, L"&Skip");
    Text text{wide(heading), crlf(changes.empty() ? "No list of changes was found." : changes)};
    auto result =
        DialogBoxIndirectParamW(GetModuleHandleW(nullptr), dialog.get(), GetConsoleWindow(),
                                procedure, reinterpret_cast<LPARAM>(&text));
    return result == IDOK;
}
inline void update_message(const std::string &text, bool error) {
    MessageBoxW(GetConsoleWindow(), wide(text).c_str(), L"Blind Rage update",
                MB_OK | MB_SETFOREGROUND | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}
} // namespace a11y
