#include "stdafx.h"
#include "SettingsTheme.h"
#include <uxtheme.h>
#include <dwmapi.h>

namespace {
constexpr COLORREF Background = RGB(32, 36, 40);
constexpr COLORREF Field = RGB(23, 26, 29);
constexpr COLORREF Text = RGB(226, 230, 234);
constexpr COLORREF Disabled = RGB(139, 146, 153);
constexpr COLORREF Border = RGB(77, 85, 93);
constexpr COLORREF Accent = RGB(112, 168, 215);
constexpr UINT_PTR SubclassId = 0x46535448;

HBRUSH BackgroundBrush()
{
    static CBrush brush(Background);
    return brush;
}
HBRUSH FieldBrush()
{
    static CBrush brush(Field);
    return brush;
}
struct WindowTheme {
    bool dark = false;
    COLORREF background = CLR_DEFAULT, text = CLR_DEFAULT, textBackground = CLR_DEFAULT;
};
bool IsClass(HWND window, LPCWSTR name)
{
    WCHAR className[128] = {};
    GetClassNameW(window, className, _countof(className));
    return _wcsicmp(className, name) == 0;
}
int Scale(HWND window, int value)
{
    // GetDpiForWindow is optional on older supported Windows releases.
    using GetDpi = UINT(WINAPI*)(HWND);
    static auto getDpi = reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    return MulDiv(value, getDpi ? getDpi(window) : 96, 96);
}
void Fill(HDC dc, const RECT& rect, COLORREF color)
{
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
void Outline(HDC dc, RECT rect, COLORREF color)
{
    SetDCBrushColor(dc, color);
    FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
RECT ClientRectInWindow(HWND window)
{
    RECT client = {};
    GetClientRect(window, &client);
    POINT origin = {0, 0};
    ClientToScreen(window, &origin);
    RECT outer = {};
    GetWindowRect(window, &outer);
    OffsetRect(&client, origin.x - outer.left, origin.y - outer.top);
    return client;
}
void DrawScrollBar(HDC dc, HWND window, int bar, RECT rect)
{
    Fill(dc, rect, Background);
    Outline(dc, rect, Border);

    SCROLLINFO info = {sizeof(info), SIF_ALL};
    if (!GetScrollInfo(window, bar, &info) || info.nMax <= info.nMin || info.nPage == 0) {
        return;
    }
    const bool vertical = bar == SB_VERT;
    const int track = vertical ? rect.bottom - rect.top : rect.right - rect.left;
    const int range = std::max(1, info.nMax - info.nMin + 1);
    const int page = std::min(static_cast<int>(info.nPage), range);
    const int thumb = std::max(12, MulDiv(track, page, range));
    const int travel = std::max(0, track - thumb);
    const int maxPos = std::max(1, info.nMax - info.nMin - page + 1);
    const int offset = std::min(travel, std::max(0, MulDiv(info.nPos - info.nMin, travel, maxPos)));
    if (vertical) {
        RECT thumbRect = {rect.left + 2, rect.top + offset + 2, rect.right - 2, rect.top + offset + thumb - 2};
        Fill(dc, thumbRect, Border);
        Outline(dc, thumbRect, Accent);
    } else {
        RECT thumbRect = {rect.left + offset + 2, rect.top + 2, rect.left + offset + thumb - 2, rect.bottom - 2};
        Fill(dc, thumbRect, Border);
        Outline(dc, thumbRect, Accent);
    }
}
void PaintScrollBars(HWND window)
{
    const LONG style = GetWindowLongW(window, GWL_STYLE);
    if (!(style & (WS_VSCROLL | WS_HSCROLL))) return;
    RECT client = ClientRectInWindow(window);
    RECT outer = {};
    GetWindowRect(window, &outer);
    OffsetRect(&outer, -outer.left, -outer.top);
    HDC dc = GetWindowDC(window);
    if (!dc) return;
    const int cx = Scale(window, GetSystemMetrics(SM_CXVSCROLL));
    const int cy = Scale(window, GetSystemMetrics(SM_CYHSCROLL));
    if (style & WS_VSCROLL) {
        RECT bar = {client.right, client.top, outer.right, client.bottom - ((style & WS_HSCROLL) ? cy : 0)};
        DrawScrollBar(dc, window, SB_VERT, bar);
    }
    if (style & WS_HSCROLL) {
        RECT bar = {client.left, client.bottom, client.right - ((style & WS_VSCROLL) ? cx : 0), outer.bottom};
        DrawScrollBar(dc, window, SB_HORZ, bar);
    }
    if ((style & (WS_VSCROLL | WS_HSCROLL)) == (WS_VSCROLL | WS_HSCROLL)) {
        RECT corner = {client.right, client.bottom, outer.right, outer.bottom};
        Fill(dc, corner, Background);
    }
    ReleaseDC(window, dc);
}
void Arrow(HDC dc, RECT rect, bool up, COLORREF color)
{
    const int x = (rect.left + rect.right) / 2;
    const int y = (rect.top + rect.bottom) / 2;
    const int size = std::max(2L, (rect.right - rect.left) / 5);
    POINT points[] = {{x - size, y + (up ? size / 2 : -size / 2)},
                      {x + size, y + (up ? size / 2 : -size / 2)},
                      {x, y + (up ? -size / 2 : size / 2)}};
    SetDCPenColor(dc, color); SetDCBrushColor(dc, color);
    SelectObject(dc, GetStockObject(DC_PEN)); SelectObject(dc, GetStockObject(DC_BRUSH));
    Polygon(dc, points, 3);
}
bool KeepNativeButton(HWND window)
{
    const int id = GetDlgCtrlID(window);
    return id == IDC_BUTTON_CLRFACE || id == IDC_BUTTON_CLROUTLINE ||
        id == IDC_BUTTON_CLRFONT || id == IDC_BUTTON_CLRGRAD1 || id == IDC_BUTTON_CLRGRAD2 ||
        id == IDC_COLORPRI || id == IDC_COLORSEC || id == IDC_COLOROUTL || id == IDC_COLORSHAD;
}
bool PaintButton(HWND window, HDC dc)
{
    const LONG style = GetWindowLongW(window, GWL_STYLE);
    const UINT type = style & BS_TYPEMASK;
    if (type == BS_OWNERDRAW || (style & (BS_ICON | BS_BITMAP))) {
        return false; // Keep existing colour previews and owner-drawn controls.
    }
    RECT rect; GetClientRect(window, &rect);
    WCHAR caption[1024] = {}; GetWindowTextW(window, caption, _countof(caption));
    const bool enabled = IsWindowEnabled(window) != FALSE;
    const UINT state = static_cast<UINT>(SendMessageW(window, BM_GETSTATE, 0, 0));
    const int saved = SaveDC(dc);
    SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, enabled ? Text : Disabled);
    RECT label = rect;
    UINT flags = DT_VCENTER | ((style & BS_MULTILINE) ? DT_WORDBREAK : DT_SINGLELINE);
    if (SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL) flags |= DT_HIDEPREFIX;
    if (type == BS_GROUPBOX) {
        RECT measured = rect; DrawTextW(dc, caption, -1, &measured, DT_SINGLELINE | DT_CALCRECT);
        rect.top += (measured.bottom - measured.top) / 2;
        Outline(dc, rect, Border);
        label.left += Scale(window, 8); label.top = 0;
        label.right = std::min(label.right, label.left + measured.right - measured.left + Scale(window, 4));
        label.bottom = measured.bottom;
        Fill(dc, label, Background);
        DrawTextW(dc, caption, -1, &label, DT_SINGLELINE | (flags & DT_HIDEPREFIX));
    } else if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON) {
        POINT cursor; GetCursorPos(&cursor); ScreenToClient(window, &cursor);
        const bool hot = enabled && PtInRect(&rect, cursor);
        Fill(dc, rect, state & BST_PUSHED ? RGB(49, 61, 72) : hot ? RGB(61, 69, 77) : RGB(47, 53, 59));
        Outline(dc, rect, enabled && ((state & BST_FOCUS) || type == BS_DEFPUSHBUTTON) ? Accent : Border);
        InflateRect(&label, -Scale(window, 4), -Scale(window, 2));
        DrawTextW(dc, caption, -1, &label, flags | DT_CENTER);
        if ((state & BST_FOCUS) && !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
            InflateRect(&rect, -Scale(window, 3), -Scale(window, 3)); DrawFocusRect(dc, &rect);
        }
    } else {
        Fill(dc, rect, Background);
        const int size = Scale(window, 13), gap = Scale(window, 5);
        RECT mark = {rect.left, rect.top + (rect.bottom - rect.top - size) / 2, rect.left + size, 0};
        mark.bottom = mark.top + size;
        if (style & BS_LEFTTEXT) {OffsetRect(&mark, rect.right - size, 0); label.right = mark.left - gap;}
        else {label.left = mark.right + gap;}
        SetDCPenColor(dc, enabled ? Accent : Border); SetDCBrushColor(dc, Field);
        SelectObject(dc, GetStockObject(DC_PEN)); SelectObject(dc, GetStockObject(DC_BRUSH));
        const bool radio = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
        if (radio) Ellipse(dc, mark.left, mark.top, mark.right, mark.bottom);
        else Rectangle(dc, mark.left, mark.top, mark.right, mark.bottom);
        const auto check = SendMessageW(window, BM_GETCHECK, 0, 0);
        if (check != BST_UNCHECKED) {
            RECT inside = mark; InflateRect(&inside, -Scale(window, 3), -Scale(window, 3));
            if (radio) {SetDCBrushColor(dc, enabled ? Accent : Disabled); Ellipse(dc, inside.left, inside.top, inside.right, inside.bottom);}
            else if (check == BST_INDETERMINATE) Fill(dc, inside, enabled ? Text : Disabled);
            else {
                HPEN pen = CreatePen(PS_SOLID, std::max(1, Scale(window, 2)), enabled ? Text : Disabled);
                const auto old = SelectObject(dc, pen);
                MoveToEx(dc, mark.left + Scale(window, 3), mark.top + size / 2, nullptr);
                LineTo(dc, mark.left + size / 2, mark.bottom - Scale(window, 3));
                LineTo(dc, mark.right - Scale(window, 2), mark.top + Scale(window, 3));
                SelectObject(dc, old); DeleteObject(pen);
            }
        }
        DrawTextW(dc, caption, -1, &label, flags | ((style & BS_RIGHT) == BS_RIGHT ? DT_RIGHT : DT_LEFT));
        if ((state & BST_FOCUS) && !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) DrawFocusRect(dc, &label);
    }
    RestoreDC(dc, saved);
    return true;
}
bool PaintTabs(HWND window, HDC dc)
{
    if (!IsClass(window, WC_TABCONTROLW)) return false;
    RECT client = {};
    GetClientRect(window, &client);
    Fill(dc, client, Background);
    const int selected = TabCtrl_GetCurSel(window);
    const int count = TabCtrl_GetItemCount(window);
    SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, Text);
    for (int index = 0; index < count; index++) {
        RECT item = {};
        TabCtrl_GetItemRect(window, index, &item);
        const bool active = index == selected;
        Fill(dc, item, active ? Field : Background);
        Outline(dc, item, active ? Accent : Border);
        WCHAR caption[256] = {};
        TCITEMW info = {TCIF_TEXT, 0, 0, caption, static_cast<int>(_countof(caption)), 0, 0};
        TabCtrl_GetItem(window, index, &info);
        InflateRect(&item, -Scale(window, 4), 0);
        DrawTextW(dc, caption, -1, &item, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
    }
    return true;
}
LRESULT CALLBACK SettingsSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
{
    auto* theme = reinterpret_cast<WindowTheme*>(data);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, SettingsSubclass, SubclassId);
        delete theme;
        return DefSubclassProc(window, message, wParam, lParam);
    }
    if (message == WM_CLASSIC_SETTINGS_THEME) {
        ApplyClassicSettingsTheme(window);
        return 0;
    }
    if (!theme->dark) return DefSubclassProc(window, message, wParam, lParam);
    if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC) {
        DefSubclassProc(window, message, wParam, lParam); // Preserve control-specific status/preview handling.
        const HWND child = reinterpret_cast<HWND>(lParam);
        const HDC dc = reinterpret_cast<HDC>(wParam);
        const bool field = IsClass(child, L"Edit") || IsClass(child, L"ListBox") || IsClass(child, L"ComboBox");
        const COLORREF old = GetTextColor(dc);
        if (!IsWindowEnabled(child)) SetTextColor(dc, Disabled);
        else if (old == RGB(0, 0, 255)) SetTextColor(dc, RGB(111, 179, 239));
        else if (old == GetSysColor(COLOR_WINDOWTEXT) || old == RGB(0, 0, 0) || old == GetSysColor(COLOR_GRAYTEXT)) SetTextColor(dc, Text);
        SetBkColor(dc, field ? Field : Background);
        return reinterpret_cast<LRESULT>(field ? FieldBrush() : BackgroundBrush());
    }
    if (message == WM_ERASEBKGND && (IsClass(window, L"#32770") || IsClass(window, L"Button"))) {
        if (!IsClass(window, L"Button") || (GetWindowLongW(window, GWL_STYLE) & BS_TYPEMASK) != BS_GROUPBOX) {
            const HDC dc = reinterpret_cast<HDC>(wParam);
            const int saved = SaveDC(dc);
            // A property page can erase after its siblings have painted (also
            // during WM_PRINT). Never paint through its visible child controls.
            for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
                if (!IsWindowVisible(child) || (IsClass(child, L"Button") &&
                    (GetWindowLongW(child, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX)) continue;
                RECT childRect; GetWindowRect(child, &childRect);
                MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&childRect), 2);
                ExcludeClipRect(dc, childRect.left, childRect.top, childRect.right, childRect.bottom);
            }
            RECT rect; GetClientRect(window, &rect); FillRect(dc, &rect, BackgroundBrush());
            RestoreDC(dc, saved);
        }
        return 1;
    }
    if (message == WM_NCPAINT) {
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        PaintScrollBars(window);
        const LONG style = GetWindowLongW(window, GWL_STYLE);
        if ((style & WS_BORDER) && !IsClass(window, L"Button")) {
            HDC dc = GetWindowDC(window);
            if (dc) {
                RECT outer = {}; GetWindowRect(window, &outer);
                OffsetRect(&outer, -outer.left, -outer.top);
                Outline(dc, outer, Border);
                ReleaseDC(window, dc);
            }
        }
        return result;
    }
    if ((message == WM_PAINT || message == WM_PRINTCLIENT) && IsClass(window, WC_TABCONTROLW)) {
        PAINTSTRUCT paint = {};
        HDC dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(wParam);
        PaintTabs(window, dc);
        if (message == WM_PAINT) EndPaint(window, &paint);
        return 0;
    }
    if ((message == WM_PAINT || message == WM_PRINTCLIENT) && IsClass(window, WC_HEADERW)) {
        PAINTSTRUCT paint = {};
        HDC dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(wParam);
        const int saved = SaveDC(dc);
        RECT rect; GetClientRect(window, &rect); Fill(dc, rect, Background);
        SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
        SetTextColor(dc, IsWindowEnabled(window) ? Text : Disabled); SetBkMode(dc, TRANSPARENT);
        for (int index = 0; index < Header_GetItemCount(window); index++) {
            WCHAR text[1024] = {}; HDITEMW item = {}; item.mask = HDI_TEXT | HDI_FORMAT;
            item.pszText = text; item.cchTextMax = _countof(text);
            Header_GetItem(window, index, &item); Header_GetItemRect(window, index, &rect);
            Outline(dc, rect, Border); InflateRect(&rect, -Scale(window, 4), 0);
            if (item.fmt & (HDF_SORTUP | HDF_SORTDOWN)) {
                RECT arrow = rect; arrow.left = arrow.right - Scale(window, 12);
                Arrow(dc, arrow, (item.fmt & HDF_SORTUP) != 0, Text); rect.right = arrow.left;
            }
            DrawTextW(dc, text, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
                ((item.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT ? DT_RIGHT : (item.fmt & HDF_JUSTIFYMASK) == HDF_CENTER ? DT_CENTER : DT_LEFT));
        }
        RestoreDC(dc, saved); if (message == WM_PAINT) EndPaint(window, &paint);
        return 0;
    }
    if ((message == WM_PAINT || message == WM_PRINTCLIENT) && IsClass(window, L"Button")) {
        const LONG style = GetWindowLongW(window, GWL_STYLE);
        if ((style & BS_TYPEMASK) != BS_OWNERDRAW && !(style & (BS_ICON | BS_BITMAP)) && !KeepNativeButton(window)) {
            PAINTSTRUCT paint = {};
            HDC dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(wParam);
            PaintButton(window, dc);
            if (message == WM_PAINT) EndPaint(window, &paint);
            return 0;
        }
    }
    const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    if (message == WM_NOTIFY && lParam) {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->code == NM_CUSTOMDRAW && IsClass(header->hwndFrom, WC_LISTVIEWW)) {
            auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return result | CDRF_NOTIFYITEMDRAW;
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT || draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                if (draw->clrTextBk == RGB(255, 130, 120)) draw->clrTextBk = RGB(95, 42, 38); // Keep shortcut conflict warnings.
                else if (draw->clrTextBk == GetSysColor(COLOR_WINDOW) || draw->clrTextBk == CLR_DEFAULT) draw->clrTextBk = Field;
                draw->clrText = Text;
                return draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT ? result | CDRF_NOTIFYSUBITEMDRAW : result;
            }
        } else if (header->code == NM_CUSTOMDRAW && IsClass(header->hwndFrom, TRACKBAR_CLASSW)) {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
            if (draw->dwDrawStage == CDDS_PREPAINT) return result | CDRF_NOTIFYITEMDRAW;
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT && (draw->dwItemSpec == TBCD_CHANNEL || draw->dwItemSpec == TBCD_THUMB)) {
                Fill(draw->hdc, draw->rc, draw->dwItemSpec == TBCD_CHANNEL ? Field : Border);
                Outline(draw->hdc, draw->rc, Accent); return CDRF_SKIPDEFAULT;
            }
        }
    }
    if ((message == WM_PAINT || message == WM_PRINTCLIENT) && (IsClass(window, L"ComboBox") || IsClass(window, UPDOWN_CLASSW))) {
        HDC dc = message == WM_PRINTCLIENT ? reinterpret_cast<HDC>(wParam) : GetDC(window);
        const int saved = SaveDC(dc);
        RECT rect; GetClientRect(window, &rect);
        if (IsClass(window, L"ComboBox")) {
            COMBOBOXINFO info = {sizeof(info)};
            if (GetComboBoxInfo(window, &info) && info.rcButton.right > info.rcButton.left) {
                Fill(dc, info.rcButton, Background); Outline(dc, info.rcButton, Border);
                Arrow(dc, info.rcButton, false, IsWindowEnabled(window) ? Text : Disabled);
            }
        } else {
            RECT first = rect, second = rect;
            const bool horizontal = (GetWindowLongW(window, GWL_STYLE) & UDS_HORZ) != 0;
            if (!horizontal) {first.bottom = rect.bottom / 2; second.top = first.bottom;}
            else {first.right = rect.right / 2; second.left = first.right;}
            // Leave the unusual horizontal spin style to the native control.
            if (!horizontal) {
                Fill(dc, rect, Background); Outline(dc, first, Border); Outline(dc, second, Border);
                Arrow(dc, first, true, IsWindowEnabled(window) ? Text : Disabled);
                Arrow(dc, second, false, IsWindowEnabled(window) ? Text : Disabled);
            }
        }
        RestoreDC(dc, saved); if (message != WM_PRINTCLIENT) ReleaseDC(window, dc);
    }
    if (IsClass(window, L"Button") && (message == BM_SETCHECK || message == BM_SETSTATE || message == WM_ENABLE ||
        message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_MOUSEMOVE || message == WM_MOUSELEAVE || message == WM_UPDATEUISTATE)) {
        if (message == WM_MOUSEMOVE) {TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);}
        InvalidateRect(window, nullptr, FALSE);
    }
    return result;
}
void ApplyWindow(HWND window, bool dark)
{
    DWORD_PTR data = 0;
    const bool existing = GetWindowSubclass(window, SettingsSubclass, SubclassId, &data) != FALSE;
    auto* theme = reinterpret_cast<WindowTheme*>(data);
    if (existing && theme->dark == dark) return;
    if (!existing) {
        theme = new WindowTheme;
        if (!SetWindowSubclass(window, SettingsSubclass, SubclassId, reinterpret_cast<DWORD_PTR>(theme))) {delete theme; return;}
        if (IsClass(window, WC_TREEVIEWW)) {theme->background = TreeView_GetBkColor(window); theme->text = TreeView_GetTextColor(window);}
        if (IsClass(window, WC_LISTVIEWW)) {theme->background = ListView_GetBkColor(window); theme->text = ListView_GetTextColor(window); theme->textBackground = ListView_GetTextBkColor(window);}
        if (!dark) return;
    }
    theme->dark = dark;
    // Do not disable visual styles for dialogs or the page frame: that also
    // disables DWM captions and changes the native frame's caption geometry.
    if (IsClass(window, L"Button") || IsClass(window, L"Edit") || IsClass(window, L"ComboBox") ||
        IsClass(window, WC_TREEVIEWW) || IsClass(window, WC_LISTVIEWW) || IsClass(window, WC_HEADERW) ||
        IsClass(window, UPDOWN_CLASSW) || IsClass(window, TRACKBAR_CLASSW) || IsClass(window, L"ListBox")) {
        SetWindowTheme(window, dark ? L"" : nullptr, dark ? L"" : nullptr);
    }
    if (IsClass(window, WC_TREEVIEWW)) {
        TreeView_SetBkColor(window, dark ? Field : theme->background);
        TreeView_SetTextColor(window, dark ? Text : theme->text);
        TreeView_SetLineColor(window, dark ? Border : CLR_DEFAULT);
    } else if (IsClass(window, WC_LISTVIEWW)) {
        ListView_SetBkColor(window, dark ? Field : theme->background);
        ListView_SetTextColor(window, dark ? Text : theme->text);
        ListView_SetTextBkColor(window, dark ? Field : theme->textBackground);
    } else if (IsClass(window, L"ComboBox")) {
        COMBOBOXINFO info = {sizeof(info)};
        if (GetComboBoxInfo(window, &info) && info.hwndList) ApplyWindow(info.hwndList, dark);
    }
}
} // namespace

bool ClassicSettingsUseDarkTheme()
{
    HIGHCONTRASTW contrast = {sizeof(contrast)};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    return AfxGetAppSettings().bUseDarkTheme && !(contrast.dwFlags & HCF_HIGHCONTRASTON);
}
void ApplyClassicSettingsTheme(HWND root)
{
    if (!IsWindow(root)) return;
    const bool dark = ClassicSettingsUseDarkTheme();
    DWORD_PTR previous = 0;
    const bool hadTheme = GetWindowSubclass(root, SettingsSubclass, SubclassId, &previous) != FALSE;
    const bool changed = !hadTheme || reinterpret_cast<WindowTheme*>(previous)->dark != dark;
    ApplyWindow(root, dark);
    EnumChildWindows(root, [](HWND child, LPARAM value) -> BOOL {ApplyWindow(child, value != 0); return TRUE;}, dark);
    if (!changed) return; // Page activation must not erase/reframe the entire sheet.
    const BOOL titleDark = dark;
    DwmSetWindowAttribute(root, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &titleDark, sizeof(titleDark));
    const COLORREF caption = dark ? Background : DWMWA_COLOR_DEFAULT;
    const COLORREF text = dark ? Text : DWMWA_COLOR_DEFAULT;
    DwmSetWindowAttribute(root, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    DwmSetWindowAttribute(root, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
    RedrawWindow(root, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}
