#include "stdafx.h"
#include "SettingsTheme.h"
#include <uxtheme.h>
#include <dwmapi.h>
#include <windowsx.h>

namespace {
constexpr COLORREF Background = RGB(32, 36, 40);
constexpr COLORREF Field = RGB(23, 26, 29);
constexpr COLORREF Text = RGB(226, 230, 234);
constexpr COLORREF Disabled = RGB(139, 146, 153);
constexpr COLORREF Border = RGB(57, 64, 70);
constexpr COLORREF Accent = RGB(92, 139, 178);
constexpr UINT_PTR SubclassId = 0x46535448;
constexpr UINT_PTR ScrollRepeatTimerId = 0x5343524C;

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
    int capturedBar = -1, scrollPart = 0, grabOffset = 0;
    int dragPosition = 0;
    bool repeating = false;
    HIMAGELIST originalChecks = nullptr, darkChecks = nullptr;
    DWORD originalListStyle = 0;
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
struct ScrollGeometry {
    RECT rect = {};
    int start = 0, end = 0, arrow = 0, thumbStart = 0, thumbEnd = 0;
    bool vertical = false, enabled = false;
};
bool GetScrollGeometry(HWND window, int bar, ScrollGeometry& geometry)
{
    if (!(GetWindowLongW(window, GWL_STYLE) & (bar == SB_VERT ? WS_VSCROLL : WS_HSCROLL))) return false;
    SCROLLBARINFO info = {sizeof(info)};
    if (!GetScrollBarInfo(window, bar == SB_VERT ? OBJID_VSCROLL : OBJID_HSCROLL, &info) ||
        (info.rgstate[0] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN))) return false;
    RECT outer; GetWindowRect(window, &outer);
    geometry.rect = info.rcScrollBar;
    OffsetRect(&geometry.rect, -outer.left, -outer.top);
    geometry.vertical = bar == SB_VERT;
    geometry.start = geometry.vertical ? geometry.rect.top : geometry.rect.left;
    geometry.end = geometry.vertical ? geometry.rect.bottom : geometry.rect.right;
    geometry.arrow = info.dxyLineButton;
    geometry.thumbStart = geometry.start + info.xyThumbTop;
    geometry.thumbEnd = geometry.start + info.xyThumbBottom;
    geometry.enabled = IsWindowEnabled(window) && !(info.rgstate[0] & STATE_SYSTEM_UNAVAILABLE);
    return true;
}
RECT ScrollPartRect(const ScrollGeometry& geometry, int start, int end)
{
    RECT rect = geometry.rect;
    if (geometry.vertical) {rect.top = start; rect.bottom = end;}
    else {rect.left = start; rect.right = end;}
    return rect;
}
void PaintScrollBars(HWND window, const WindowTheme* theme, HDC output = nullptr)
{
    if (!(GetWindowLongW(window, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL))) return;
    HDC dc = output ? output : GetWindowDC(window);
    if (!dc) return;
    const int saved = SaveDC(dc);
    for (int bar : {SB_VERT, SB_HORZ}) {
        ScrollGeometry geometry;
        if (!GetScrollGeometry(window, bar, geometry)) continue;
        Fill(dc, geometry.rect, Field);
        for (int end = 0; end != 2; ++end) {
            RECT button = ScrollPartRect(geometry, end ? geometry.end - geometry.arrow : geometry.start,
                end ? geometry.end : geometry.start + geometry.arrow);
            const bool pressed = theme->capturedBar == bar && theme->scrollPart == (end ? SB_LINEDOWN : SB_LINEUP);
            Fill(dc, button, pressed ? Border : Background);
            const int x = (button.left + button.right) / 2, y = (button.top + button.bottom) / 2;
            const int direction = end ? 1 : -1, size = Scale(window, 3);
            POINT points[3];
            if (geometry.vertical) {
                points[0] = {x - size, y - direction}; points[1] = {x + size, y - direction}; points[2] = {x, y + direction * 2};
            } else {
                points[0] = {x - direction, y - size}; points[1] = {x - direction, y + size}; points[2] = {x + direction * 2, y};
            }
            const COLORREF color = geometry.enabled ? Disabled : Border;
            SetDCPenColor(dc, color); SetDCBrushColor(dc, color);
            SelectObject(dc, GetStockObject(DC_PEN)); SelectObject(dc, GetStockObject(DC_BRUSH));
            Polygon(dc, points, 3);
        }
        if (geometry.enabled && geometry.thumbEnd > geometry.thumbStart) {
            RECT thumb = ScrollPartRect(geometry, geometry.thumbStart, geometry.thumbEnd);
            InflateRect(&thumb, -Scale(window, 3), -Scale(window, 3));
            Fill(dc, thumb, theme->capturedBar == bar ? RGB(120, 130, 139) : RGB(82, 91, 100));
        }
    }
    RestoreDC(dc, saved);
    if (!output) ReleaseDC(window, dc);
}
void PaintControlFrame(HWND window, HDC output = nullptr)
{
    const LONG style = GetWindowLongW(window, GWL_STYLE), exStyle = GetWindowLongW(window, GWL_EXSTYLE);
    if ((style & WS_CAPTION) == WS_CAPTION || !(style & WS_BORDER || exStyle & WS_EX_CLIENTEDGE)) return;
    RECT client = ClientRectInWindow(window), outer; GetWindowRect(window, &outer);
    OffsetRect(&outer, -outer.left, -outer.top);
    HDC dc = output ? output : GetWindowDC(window);
    if (dc) {
        const int saved = SaveDC(dc);
        ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
        // Exclude native scroll-bar rectangles: their full geometry is painted separately.
        for (int bar : {SB_VERT, SB_HORZ}) {
            ScrollGeometry geometry;
            if (GetScrollGeometry(window, bar, geometry)) ExcludeClipRect(dc, geometry.rect.left, geometry.rect.top, geometry.rect.right, geometry.rect.bottom);
        }
        Fill(dc, outer, Background); Outline(dc, outer, Border);
        RestoreDC(dc, saved); if (!output) ReleaseDC(window, dc);
    }
}
void ScrollCommand(HWND window, int bar, int command, int position = 0)
{
    // TreeView reads the system's private tracking position for thumb messages.
    // Our capture must instead set the first visible item through its public API.
    if (bar == SB_VERT && IsClass(window, WC_TREEVIEWW) && (command == SB_THUMBTRACK || command == SB_THUMBPOSITION)) {
        HTREEITEM item = TreeView_GetRoot(window);
        for (int index = 0; item && index < position; ++index) item = TreeView_GetNextVisible(window, item);
        if (item) TreeView_SelectSetFirstVisible(window, item);
        return;
    }
    if (IsClass(window, WC_LISTVIEWW) && (command == SB_THUMBTRACK || command == SB_THUMBPOSITION)) {
        SCROLLINFO info = {sizeof(info), SIF_POS};
        if (GetScrollInfo(window, bar, &info)) {
            if (GetFocus() != window) SetFocus(window);
            int delta = position - info.nPos;
            if (bar == SB_VERT) {
                RECT item = {};
                if (ListView_GetItemRect(window, 0, &item, LVIR_BOUNDS)) delta *= item.bottom - item.top;
                ListView_Scroll(window, 0, delta);
            } else ListView_Scroll(window, delta, 0);
        }
        return;
    }
    SendMessageW(window, bar == SB_VERT ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(command, position), 0);
}
POINT WindowPoint(HWND window, LPARAM clientPoint)
{
    POINT point = {GET_X_LPARAM(clientPoint), GET_Y_LPARAM(clientPoint)};
    ClientToScreen(window, &point);
    RECT outer; GetWindowRect(window, &outer);
    point.x -= outer.left; point.y -= outer.top;
    return point;
}
bool PaintCheckList(DRAWITEMSTRUCT* draw)
{
    if (!draw || draw->CtlType != ODT_LISTBOX) return false;
    auto* list = DYNAMIC_DOWNCAST(CCheckListBox, CWnd::FromHandlePermanent(draw->hwndItem));
    if (!list) return false;
    if (draw->itemID == static_cast<UINT>(-1)) return true;
    const int saved = SaveDC(draw->hDC);
    const bool enabled = IsWindowEnabled(draw->hwndItem) && list->IsEnabled(draw->itemID);
    const COLORREF background = draw->itemState & ODS_SELECTED ? RGB(44, 70, 92) : Field;
    Fill(draw->hDC, draw->rcItem, background);
    const int size = std::min(Scale(draw->hwndItem, 13), static_cast<int>(draw->rcItem.bottom - draw->rcItem.top - 2));
    RECT box = {draw->rcItem.left + 1, draw->rcItem.top + (draw->rcItem.bottom - draw->rcItem.top - size) / 2, 0, 0};
    box.right = box.left + size; box.bottom = box.top + size;
    Fill(draw->hDC, box, Field); Outline(draw->hDC, box, enabled ? Disabled : Border);
    const int check = list->GetCheck(draw->itemID);
    if (check == BST_INDETERMINATE) {
        RECT mark = box; InflateRect(&mark, -3, -3); Fill(draw->hDC, mark, enabled ? Text : Disabled);
    } else if (check == BST_CHECKED) {
        HPEN pen = CreatePen(PS_SOLID, std::max(1, Scale(draw->hwndItem, 2)), enabled ? Text : Disabled);
        SelectObject(draw->hDC, pen);
        MoveToEx(draw->hDC, box.left + 3, (box.top + box.bottom) / 2, nullptr);
        LineTo(draw->hDC, box.left + size / 2, box.bottom - 3); LineTo(draw->hDC, box.right - 2, box.top + 3);
        SelectObject(draw->hDC, GetStockObject(DC_PEN)); DeleteObject(pen);
    }
    CString caption; list->GetText(draw->itemID, caption);
    RECT label = draw->rcItem; label.left = box.right + Scale(draw->hwndItem, 4);
    SelectObject(draw->hDC, reinterpret_cast<HFONT>(SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0)));
    SetTextColor(draw->hDC, enabled ? Text : Disabled); SetBkMode(draw->hDC, TRANSPARENT);
    DrawTextW(draw->hDC, caption, caption.GetLength(), &label, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    if ((draw->itemState & ODS_FOCUS) && !(draw->itemState & ODS_NOFOCUSRECT)) DrawFocusRect(draw->hDC, &label);
    RestoreDC(draw->hDC, saved);
    return true;
}
void Arrow(HDC dc, RECT rect, bool up, COLORREF color, bool horizontal = false)
{
    const int x = (rect.left + rect.right) / 2;
    const int y = (rect.top + rect.bottom) / 2;
    const int size = std::max(2L, (rect.right - rect.left) / 5);
    POINT points[] = {{x - size, y + (up ? size / 2 : -size / 2)},
                      {x + size, y + (up ? size / 2 : -size / 2)},
                      {x, y + (up ? -size / 2 : size / 2)}};
    if (horizontal) {
        const int direction = up ? -1 : 1;
        points[0] = {x - direction, y - size}; points[1] = {x - direction, y + size}; points[2] = {x + direction * size, y};
    }
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
    const int saved = SaveDC(dc);
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
    RestoreDC(dc, saved);
    return true;
}
void PaintDisabledList(HWND window, HDC dc)
{
    const int saved = SaveDC(dc);
    RECT client; GetClientRect(window, &client);
    HWND header = ListView_GetHeader(window);
    if (header && IsWindowVisible(header)) {
        RECT rect; GetWindowRect(header, &rect); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2);
        ExcludeClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);
    }
    Fill(dc, client, Field);
    SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
    SetTextColor(dc, Disabled); SetBkMode(dc, TRANSPARENT);
    const int columns = header ? Header_GetItemCount(header) : 1;
    const int count = ListView_GetItemCount(window);
    const int end = std::min(count, ListView_GetTopIndex(window) + ListView_GetCountPerPage(window) + 2);
    for (int row = ListView_GetTopIndex(window); row < end; ++row) {
        RECT item = {};
        if (!ListView_GetItemRect(window, row, &item, LVIR_BOUNDS)) continue;
        for (int column = 0; column < columns; ++column) {
            RECT rect = {};
            if (column == 0) ListView_GetItemRect(window, row, &rect, LVIR_LABEL);
            else ListView_GetSubItemRect(window, row, column, LVIR_BOUNDS, &rect);
            LVCOLUMNW info = {}; info.mask = LVCF_FMT | LVCF_WIDTH;
            ListView_GetColumn(window, column, &info);
            // LVIR_LABEL for the first column can span the complete row.
            if (column == 0) rect.right = item.left + info.cx;
            InflateRect(&rect, -Scale(window, column == 0 ? 1 : 4), 0);
            WCHAR caption[2048] = {};
            ListView_GetItemText(window, row, column, caption, _countof(caption));
            DrawTextW(dc, caption, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS |
                ((info.fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_RIGHT ? DT_RIGHT : (info.fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_CENTER ? DT_CENTER : DT_LEFT));
        }
        const UINT state = ListView_GetItemState(window, row, LVIS_STATEIMAGEMASK) >> 12;
        if (state) {
            const int size = Scale(window, 13);
            RECT box = {item.left + 1, item.top + (item.bottom - item.top - size) / 2, 0, 0};
            box.right = box.left + size; box.bottom = box.top + size;
            Fill(dc, box, Field); Outline(dc, box, Border);
            if (state == 2) {
                HPEN pen = CreatePen(PS_SOLID, std::max(1, Scale(window, 2)), Disabled);
                HGDIOBJ old = SelectObject(dc, pen);
                MoveToEx(dc, box.left + 3, (box.top + box.bottom) / 2, nullptr);
                LineTo(dc, box.left + size / 2, box.bottom - 3); LineTo(dc, box.right - 2, box.top + 3);
                SelectObject(dc, old); DeleteObject(pen);
            }
        }
    }
    RestoreDC(dc, saved);
}
LRESULT CALLBACK SettingsSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
{
    auto* theme = reinterpret_cast<WindowTheme*>(data);
    if (message == WM_NCDESTROY) {
        KillTimer(window, ScrollRepeatTimerId);
        if (theme->darkChecks) {
            ListView_SetImageList(window, theme->originalChecks, LVSIL_STATE);
            ImageList_Destroy(theme->darkChecks);
        }
        RemoveWindowSubclass(window, SettingsSubclass, SubclassId);
        delete theme;
        return DefSubclassProc(window, message, wParam, lParam);
    }
    if (message == WM_CLASSIC_SETTINGS_THEME) {
        ApplyClassicSettingsTheme(window);
        return 0;
    }
    if (!theme->dark) return DefSubclassProc(window, message, wParam, lParam);
    if (message == WM_DRAWITEM && PaintCheckList(reinterpret_cast<DRAWITEMSTRUCT*>(lParam))) return TRUE;
    if ((message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONDBLCLK) && (wParam == HTVSCROLL || wParam == HTHSCROLL)) {
        const int bar = wParam == HTVSCROLL ? SB_VERT : SB_HORZ;
        ScrollGeometry geometry;
        if (!GetScrollGeometry(window, bar, geometry) || !geometry.enabled) return 0;
        RECT outer; GetWindowRect(window, &outer);
        const int coordinate = geometry.vertical ? GET_Y_LPARAM(lParam) - outer.top : GET_X_LPARAM(lParam) - outer.left;
        theme->capturedBar = bar;
        theme->scrollPart = coordinate < geometry.start + geometry.arrow ? SB_LINEUP :
            coordinate >= geometry.end - geometry.arrow ? SB_LINEDOWN :
            coordinate < geometry.thumbStart ? SB_PAGEUP : coordinate >= geometry.thumbEnd ? SB_PAGEDOWN : SB_THUMBTRACK;
        theme->grabOffset = coordinate - geometry.thumbStart;
        SCROLLINFO info = {sizeof(info), SIF_POS}; GetScrollInfo(window, bar, &info); theme->dragPosition = info.nPos;
        SetCapture(window);
        if (theme->scrollPart != SB_THUMBTRACK) {
            ScrollCommand(window, bar, theme->scrollPart);
            // UI auto-repeat exists only while the user holds a scroll button.
            theme->repeating = false; SetTimer(window, ScrollRepeatTimerId, 350, nullptr);
        }
        PaintScrollBars(window, theme); return 0;
    }
    if (message == WM_NCMOUSEMOVE && (wParam == HTVSCROLL || wParam == HTHSCROLL)) {
        PaintScrollBars(window, theme); return 0; // Never enter native light hover drawing.
    }
    if (message == WM_TIMER && wParam == ScrollRepeatTimerId) {
        if (theme->capturedBar != -1 && theme->scrollPart != SB_THUMBTRACK) {
            if (!theme->repeating) {SetTimer(window, ScrollRepeatTimerId, 60, nullptr); theme->repeating = true;}
            POINT point; GetCursorPos(&point); RECT outer; GetWindowRect(window, &outer);
            ScrollGeometry geometry;
            if (GetScrollGeometry(window, theme->capturedBar, geometry)) {
                point.x -= outer.left; point.y -= outer.top;
                const int coordinate = geometry.vertical ? point.y : point.x;
                const RECT active = theme->scrollPart == SB_LINEUP ? ScrollPartRect(geometry, geometry.start, geometry.start + geometry.arrow) :
                    theme->scrollPart == SB_LINEDOWN ? ScrollPartRect(geometry, geometry.end - geometry.arrow, geometry.end) : geometry.rect;
                if (PtInRect(&active, point) &&
                    (theme->scrollPart != SB_PAGEUP || coordinate < geometry.thumbStart) &&
                    (theme->scrollPart != SB_PAGEDOWN || coordinate >= geometry.thumbEnd)) ScrollCommand(window, theme->capturedBar, theme->scrollPart);
            }
        }
        return 0;
    }
    if (message == WM_MOUSEMOVE && theme->capturedBar != -1 && theme->scrollPart == SB_THUMBTRACK) {
        ScrollGeometry geometry;
        SCROLLINFO info = {sizeof(info), SIF_ALL};
        if (GetScrollGeometry(window, theme->capturedBar, geometry) && GetScrollInfo(window, theme->capturedBar, &info)) {
            const POINT point = WindowPoint(window, lParam);
            const int travel = geometry.end - geometry.start - 2 * geometry.arrow - (geometry.thumbEnd - geometry.thumbStart);
            const int range = std::max(0, info.nMax - info.nMin - static_cast<int>(info.nPage) + 1);
            const int offset = (geometry.vertical ? point.y : point.x) - theme->grabOffset - geometry.start - geometry.arrow;
            theme->dragPosition = info.nMin + (travel > 0 ? MulDiv(std::max(0, std::min(travel, offset)), range, travel) : 0);
            ScrollCommand(window, theme->capturedBar, SB_THUMBTRACK, theme->dragPosition);
        }
        PaintScrollBars(window, theme); return 0;
    }
    if ((message == WM_LBUTTONUP || message == WM_CAPTURECHANGED || message == WM_CANCELMODE) && theme->capturedBar != -1) {
        const int bar = theme->capturedBar, part = theme->scrollPart, position = theme->dragPosition;
        theme->capturedBar = -1; KillTimer(window, ScrollRepeatTimerId);
        if (GetCapture() == window) ReleaseCapture();
        if (part == SB_THUMBTRACK) ScrollCommand(window, bar, SB_THUMBPOSITION, position);
        ScrollCommand(window, bar, SB_ENDSCROLL);
        PaintScrollBars(window, theme); return 0;
    }
    if (message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC) {
        DefSubclassProc(window, message, wParam, lParam); // Preserve control-specific status/preview handling.
        const HWND child = reinterpret_cast<HWND>(lParam);
        const HDC dc = reinterpret_cast<HDC>(wParam);
        const bool field = IsClass(child, L"Edit") || IsClass(child, L"ListBox") || IsClass(child, L"ComboBox");
        const COLORREF old = GetTextColor(dc);
        if (!IsWindowEnabled(child)) SetTextColor(dc, Disabled);
        else if (old == RGB(0, 0, 255)) SetTextColor(dc, RGB(111, 179, 239));
        else if (old == GetSysColor(COLOR_WINDOWTEXT) || old == RGB(0, 0, 0) || old == GetSysColor(COLOR_GRAYTEXT)) SetTextColor(dc, Text);
        SetBkMode(dc, OPAQUE);
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
        const LONG style = GetWindowLongW(window, GWL_STYLE);
        const LRESULT result = (style & WS_CAPTION) == WS_CAPTION ? DefSubclassProc(window, message, wParam, lParam) : 0;
        PaintControlFrame(window); PaintScrollBars(window, theme);
        return result;
    }
    if ((message == WM_PAINT || message == WM_PRINTCLIENT) && IsClass(window, WC_LISTVIEWW) && !IsWindowEnabled(window)) {
        PAINTSTRUCT paint = {};
        HDC dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(wParam);
        PaintDisabledList(window, dc);
        if (message == WM_PAINT) EndPaint(window, &paint);
        PaintControlFrame(window); PaintScrollBars(window, theme);
        return 0;
    }
    if ((message == WM_PAINT || message == WM_PRINTCLIENT) && IsClass(window, L"Static")) {
        const LONG style = GetWindowLongW(window, GWL_STYLE);
        const UINT type = style & SS_TYPEMASK;
        if (type <= SS_RIGHT || type == SS_SIMPLE || type == SS_LEFTNOWORDWRAP) {
            PAINTSTRUCT paint = {};
            HDC dc = message == WM_PAINT ? BeginPaint(window, &paint) : reinterpret_cast<HDC>(wParam);
            const int saved = SaveDC(dc);
            RECT rect; GetClientRect(window, &rect); Fill(dc, rect, Background);
            WCHAR caption[4096] = {}; GetWindowTextW(window, caption, _countof(caption));
            SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, IsWindowEnabled(window) ? Text : Disabled);
            SendMessageW(GetParent(window), WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(window));
            SetBkMode(dc, TRANSPARENT);
            UINT flags = type == SS_CENTER ? DT_CENTER : type == SS_RIGHT ? DT_RIGHT : DT_LEFT;
            flags |= type == SS_SIMPLE || type == SS_LEFTNOWORDWRAP ? DT_SINGLELINE : DT_WORDBREAK;
            if (style & SS_CENTERIMAGE) flags = (flags & ~DT_WORDBREAK) | DT_SINGLELINE | DT_VCENTER;
            if (style & SS_NOPREFIX) flags |= DT_NOPREFIX;
            if (SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL) flags |= DT_HIDEPREFIX;
            if ((style & SS_ELLIPSISMASK) == SS_ENDELLIPSIS) flags |= DT_END_ELLIPSIS;
            else if ((style & SS_ELLIPSISMASK) == SS_PATHELLIPSIS) flags |= DT_PATH_ELLIPSIS;
            else if ((style & SS_ELLIPSISMASK) == SS_WORDELLIPSIS) flags |= DT_WORD_ELLIPSIS;
            DrawTextW(dc, caption, -1, &rect, flags);
            RestoreDC(dc, saved); if (message == WM_PAINT) EndPaint(window, &paint);
            return 0;
        }
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
    if (message == WM_PRINT && (lParam & PRF_NONCLIENT)) {
        PaintControlFrame(window, reinterpret_cast<HDC>(wParam));
        PaintScrollBars(window, theme, reinterpret_cast<HDC>(wParam));
    }
    if (message == WM_NOTIFY && lParam) {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->code == NM_CUSTOMDRAW && IsClass(header->hwndFrom, WC_LISTVIEWW)) {
            auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return result | CDRF_NOTIFYITEMDRAW;
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT || draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                if (draw->clrTextBk == RGB(255, 130, 120)) draw->clrTextBk = RGB(95, 42, 38); // Keep shortcut conflict warnings.
                else if (draw->clrTextBk == GetSysColor(COLOR_WINDOW) || draw->clrTextBk == GetSysColor(COLOR_3DFACE) || draw->clrTextBk == CLR_DEFAULT) draw->clrTextBk = Field;
                draw->clrText = !IsWindowEnabled(header->hwndFrom) || draw->clrText == GetSysColor(COLOR_GRAYTEXT) || draw->clrText == RGB(128, 128, 128) ? Disabled : Text;
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
            Outline(dc, rect, Border); RECT inner = rect; InflateRect(&inner, -1, -1); Outline(dc, inner, Field);
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
            Fill(dc, rect, Background); Outline(dc, first, Border); Outline(dc, second, Border);
            Arrow(dc, first, true, IsWindowEnabled(window) ? Text : Disabled, horizontal);
            Arrow(dc, second, false, IsWindowEnabled(window) ? Text : Disabled, horizontal);
        }
        RestoreDC(dc, saved); if (message != WM_PRINTCLIENT) ReleaseDC(window, dc);
    }
    if (IsClass(window, L"Button") && (message == BM_SETCHECK || message == BM_SETSTATE || message == WM_ENABLE ||
        message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_MOUSEMOVE || message == WM_MOUSELEAVE || message == WM_UPDATEUISTATE)) {
        if (message == WM_MOUSEMOVE) {TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);}
        InvalidateRect(window, nullptr, FALSE);
    }
    if (message == WM_PAINT || message == WM_VSCROLL || message == WM_HSCROLL || message == WM_MOUSEWHEEL ||
        message == WM_MOUSEHWHEEL || message == WM_KEYDOWN || message == WM_SIZE || message == WM_ENABLE) {
        PaintControlFrame(window); PaintScrollBars(window, theme);
    }
    return result;
}
void ApplyWindow(HWND window, bool dark)
{
    DWORD_PTR data = 0;
    const bool existing = GetWindowSubclass(window, SettingsSubclass, SubclassId, &data) != FALSE;
    auto* theme = reinterpret_cast<WindowTheme*>(data);
    const bool changed = !existing || theme->dark != dark;
    // OnInitDialog can configure a list after the first theme pass. Refresh
    // its colours/images on activation without reframing the other controls.
    if (!changed && !IsClass(window, WC_LISTVIEWW)) return;
    if (!existing) {
        theme = new WindowTheme;
        if (!SetWindowSubclass(window, SettingsSubclass, SubclassId, reinterpret_cast<DWORD_PTR>(theme))) {delete theme; return;}
        if (IsClass(window, WC_TREEVIEWW)) {theme->background = TreeView_GetBkColor(window); theme->text = TreeView_GetTextColor(window);}
        if (IsClass(window, WC_LISTVIEWW)) {theme->background = ListView_GetBkColor(window); theme->text = ListView_GetTextColor(window); theme->textBackground = ListView_GetTextBkColor(window); theme->originalListStyle = ListView_GetExtendedListViewStyle(window);}
        if (!dark) return;
    }
    if (!dark && theme->capturedBar != -1) {
        const int bar = theme->capturedBar; theme->capturedBar = -1;
        KillTimer(window, ScrollRepeatTimerId);
        if (GetCapture() == window) ReleaseCapture();
        ScrollCommand(window, bar, SB_ENDSCROLL);
    }
    theme->dark = dark;
    // Do not disable visual styles for dialogs or the page frame: that also
    // disables DWM captions and changes the native frame's caption geometry.
    if (changed && (IsClass(window, L"Button") || IsClass(window, L"Edit") || IsClass(window, L"ComboBox") ||
        IsClass(window, WC_TREEVIEWW) || IsClass(window, WC_LISTVIEWW) || IsClass(window, WC_HEADERW) ||
        IsClass(window, UPDOWN_CLASSW) || IsClass(window, TRACKBAR_CLASSW) || IsClass(window, L"ListBox"))) {
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
        const DWORD decorativeStyles = LVS_EX_GRIDLINES | LVS_EX_FLATSB;
        if (dark) theme->originalListStyle |= ListView_GetExtendedListViewStyle(window) & decorativeStyles;
        ListView_SetExtendedListViewStyleEx(window, decorativeStyles, dark ? 0 : theme->originalListStyle & decorativeStyles);
        if (dark && !theme->darkChecks && (ListView_GetExtendedListViewStyle(window) & LVS_EX_CHECKBOXES)) {
            theme->originalChecks = ListView_GetImageList(window, LVSIL_STATE);
            int width = Scale(window, 16), height = width;
            if (theme->originalChecks) ImageList_GetIconSize(theme->originalChecks, &width, &height);
            theme->darkChecks = ImageList_Create(width, height, ILC_COLOR32 | ILC_MASK, 2, 0);
            HDC screen = GetDC(window), dc = CreateCompatibleDC(screen);
            HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
            HGDIOBJ old = SelectObject(dc, bitmap);
            for (int checked = 0; checked < 2; ++checked) {
                RECT rect = {0, 0, width, height}; Fill(dc, rect, Field);
                InflateRect(&rect, -1, -1); Outline(dc, rect, Disabled);
                if (checked) {
                    HPEN pen = CreatePen(PS_SOLID, std::max(1, Scale(window, 2)), Text);
                    HGDIOBJ oldPen = SelectObject(dc, pen);
                    MoveToEx(dc, 3, height / 2, nullptr); LineTo(dc, width / 2, height - 4); LineTo(dc, width - 3, 3);
                    SelectObject(dc, oldPen); DeleteObject(pen);
                }
                ImageList_AddMasked(theme->darkChecks, bitmap, Field);
            }
            SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window, screen);
        }
        if (theme->darkChecks) ListView_SetImageList(window, dark ? theme->darkChecks : theme->originalChecks, LVSIL_STATE);
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
