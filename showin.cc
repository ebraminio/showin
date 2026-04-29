#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resource.h"

static const int kWindowListCapacity = 8192;

struct WindowList
{
  void Push(HWND hwnd, int area)
  {
    if (count >= kWindowListCapacity)
      return;
    buf[count].hwnd = hwnd;
    buf[count].area = area;
    count++;
  }

  void Clear() { count = 0; }

  void Sort()
  {
    // Originally it was using a quick sort, let's use a bubble sort!
    for (unsigned i = 0; i < count - 1; ++i)
      for (unsigned j = 0; j < count - 1 - i; ++j)
        if (buf[j].area > buf[j + 1].area)
        {
          Entry tmp = buf[j];
          buf[j] = buf[j + 1];
          buf[j + 1] = tmp;
        }
  }

  HWND HitHwnd(POINT pt)
  {
    struct tagRECT rect;
    for (unsigned i = 0; i < count; ++i)
    {
      GetWindowRect(buf[i].hwnd, &rect);
      if (PtInRect(&rect, pt))
        return buf[i].hwnd;
    }
    return nullptr;
  }

private:
  struct Entry
  {
    HWND hwnd;
    int area;
  } buf[kWindowListCapacity];
  unsigned count;
} g_windowList;

int g_optCloseWindow = 0;
HWND g_lastHoveredHwnd = nullptr;
int g_bitmapHeight = 0;
int g_isDragging = 0;
int g_optIncludeHidden = 0;
int g_optToggleEnabled = 0;
HGDIOBJ g_hFontBold = nullptr;
HINSTANCE g_hInst = nullptr;
HGDIOBJ g_h = nullptr;
int g_optSetTopmost = 0;
HGDIOBJ g_hFontNormal = nullptr;
int g_optRedrawWindow = 0;
HGDIOBJ g_hBgBrush = nullptr;
HWND g_hWnd = nullptr;
RECT g_rc = {0, 0, 0, 0};
int g_bitmapWidth = 0;
HDC g_hdc = nullptr;
int g_optSetNoActivate = 0;
int g_optToggleVisible = 0;
int g_showHighlight = 0;
HGDIOBJ g_hBitmap = nullptr;
bool g_darkMode = false;
WNDPROC g_origGroupBoxProc = nullptr;
typedef HRESULT(WINAPI *PFN_SetWindowTheme)(HWND, LPCWSTR, LPCWSTR);
typedef HRESULT(WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
PFN_SetWindowTheme g_pfnSetWindowTheme = nullptr;
PFN_DwmSetWindowAttribute g_pfnDwmSetWindowAttribute = nullptr;

static void TryEnableDpiAwareness()
{
  typedef BOOL(WINAPI * PFN)(HANDLE);
  PFN pfn = (PFN)GetProcAddress(GetModuleHandleA("user32.dll"),
                                "SetProcessDpiAwarenessContext");
  if (pfn)
    pfn((HANDLE)(LONG_PTR)-2); /* DPI_AWARENESS_CONTEXT_SYSTEM_AWARE */
}

static UINT GetSystemDpi()
{
  typedef UINT(WINAPI * PFN)();
  PFN pfn = (PFN)GetProcAddress(GetModuleHandleA("user32.dll"),
                                "GetDpiForSystem");
  return pfn ? pfn() : 96;
}

static void DrawHighlightRect(RECT *rect)
{
  if (!g_showHighlight)
    return;
  HGDIOBJ h = SelectObject(g_hdc, g_h);
  int rop2 = SetROP2(g_hdc, R2_XORPEN);
  MoveToEx(g_hdc, rect->left - 1, rect->top - 1, nullptr);
  LineTo(g_hdc, rect->right, rect->top - 1);
  LineTo(g_hdc, rect->right, rect->bottom);
  LineTo(g_hdc, rect->left - 1, rect->bottom);
  LineTo(g_hdc, rect->left - 1, rect->top - 1);
  MoveToEx(g_hdc, rect->left - 2, rect->top - 2, nullptr);
  LineTo(g_hdc, rect->right + 1, rect->top - 2);
  LineTo(g_hdc, rect->right + 1, rect->bottom + 1);
  LineTo(g_hdc, rect->left - 2, rect->bottom + 1);
  LineTo(g_hdc, rect->left - 2, rect->top - 2);
  SetROP2(g_hdc, rop2);
  SelectObject(g_hdc, h);
  SetRect(&g_rc, rect->left, rect->top, rect->right, rect->bottom);
}

static BOOL EraseHighlightRect()
{
  if (!IsRectEmpty(&g_rc))
    DrawHighlightRect(&g_rc);
  return SetRect(&g_rc, 0, 0, 0, 0);
}

static BOOL CleanupResources()
{
  DeleteObject(g_hBitmap);
  g_hBitmap = nullptr;
  DeleteObject(g_hFontNormal);
  DeleteObject(g_hFontBold);
  EraseHighlightRect();
  DeleteObject(g_hBgBrush);
  DeleteObject(g_h);
  return DeleteDC(g_hdc);
}

BOOL CALLBACK EnumFunc(HWND hWnd, LPARAM lParam)
{
  if (g_optIncludeHidden || IsWindowVisible(hWnd))
  {
    struct tagRECT rect;
    GetWindowRect(hWnd, &rect);
    if (!IsRectEmpty(&rect))
      g_windowList.Push(hWnd, (rect.right - rect.left) * (rect.bottom - rect.top));
    EnumChildWindows(hWnd, EnumFunc, 0);
  }
  return 1;
}

static void RebuildWindowList()
{
  g_windowList.Clear();
  EnumWindows(EnumFunc, 0);
  g_windowList.Sort();
}

static void UpdateHover(HWND hWnd, LPARAM lParam)
{
  POINT pt;
  pt.x = (__int16)lParam;
  pt.y = (short)HIWORD(lParam);
  ClientToScreen(hWnd, &pt);
  HWND hDlg = GetParent(hWnd);
  CHAR string[256];
  wsprintfA(string, "%4hd", pt.x);
  SetDlgItemTextA(hDlg, IDC_MOUSE_X, string);
  wsprintfA(string, "%4hd", pt.y);
  SetDlgItemTextA(hDlg, IDC_MOUSE_Y, string);
  HWND hitHwnd = g_windowList.HitHwnd(pt);
  g_hWnd = hitHwnd;
  if (hitHwnd && hitHwnd != g_lastHoveredHwnd)
  {
    g_lastHoveredHwnd = hitHwnd;
    SendMessageA(hitHwnd, WM_GETTEXT, 256, (LPARAM)string);
    SetDlgItemTextA(hDlg, IDC_TITLE, string);
    GetClassNameA(g_hWnd, string, 256);
    SetDlgItemTextA(hDlg, IDC_CLASSNAME, string);
    wsprintfA(string, "%-6d (0x%08X)", g_hWnd, g_hWnd);
    SetDlgItemTextA(hDlg, IDC_HANDLE, string);
    HWND Parent = GetParent(g_hWnd);
    wsprintfA(string, "%-6d (0x%08X)", Parent, Parent);
    SetDlgItemTextA(hDlg, IDC_PARENT, string);
    HWND Window = GetWindow(g_hWnd, GW_OWNER);
    wsprintfA(string, "%-6d (0x%08X)", Window, Window);
    SetDlgItemTextA(hDlg, IDC_OWNER, string);
    LONG WindowLongA = GetWindowLongA(g_hWnd, GWL_ID);
    wsprintfA(string, "%-6d (0x%08X)", WindowLongA, WindowLongA);
    SetDlgItemTextA(hDlg, IDC_WINDOWID, string);
    LONG_PTR wndProc = GetWindowLongPtrA(g_hWnd, GWLP_WNDPROC);
    wsprintfA(string, "0x%IX", (SIZE_T)wndProc);
    SetDlgItemTextA(hDlg, IDC_WNDPROC, string);
    RECT Rect;
    GetWindowRect(g_hWnd, &Rect);
    RECT rcDst;
    CopyRect(&rcDst, &Rect);
    if (Parent)
    {
      ScreenToClient(Parent, (LPPOINT)&Rect);
      ScreenToClient(Parent, (LPPOINT)&Rect.right);
      wsprintfA(
          string,
          "x:%4d y:%4d  w:%4d h:%4d",
          Rect.left,
          Rect.top,
          Rect.right - Rect.left,
          Rect.bottom - Rect.top);
      SetDlgItemTextA(hDlg, IDC_CLIENT_COORDS, string);
    }
    wsprintfA(
        string,
        "x:%4d y:%4d  w:%4d h:%4d",
        rcDst.left,
        rcDst.top,
        rcDst.right - rcDst.left,
        rcDst.bottom - rcDst.top);
    SetDlgItemTextA(hDlg, IDC_WINDOW_COORDS, string);
    EraseHighlightRect();
    DrawHighlightRect(&rcDst);
  }
}

LRESULT CALLBACK CrosshairWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
  switch (Msg)
  {
  case WM_MOUSEMOVE:
    if (g_isDragging)
      UpdateHover(hWnd, lParam);
    break;
  case WM_LBUTTONDOWN:
  {
    if (g_isDragging)
      return 0;
    RebuildWindowList();
    g_lastHoveredHwnd = 0;
    SetRect(&g_rc, 0, 0, 0, 0);
    SetCapture(hWnd);
    HCURSOR CursorA = LoadCursorA(g_hInst, MAKEINTRESOURCEA(IDC_CROSSHAIR));
    SetCursor(CursorA);
    g_isDragging = 1;
    UpdateHover(hWnd, lParam);
    break;
  }
  case WM_LBUTTONUP:
    if (g_isDragging)
    {
      g_isDragging = 0;
      EraseHighlightRect();
      ReleaseCapture();
      HCURSOR arrowCursor = LoadCursorA(nullptr, IDC_ARROW);
      SetCursor(arrowCursor);
      HWND parentWnd = GetParent(hWnd);
      SetForegroundWindow(parentWnd);
    }
    return 0;
  case WM_RBUTTONDOWN:
    if (g_isDragging)
    {
      HWND dlgWnd = GetParent(hWnd);
      if (g_hWnd != dlgWnd && GetParent(g_hWnd) != dlgWnd)
      {
        if (g_optToggleEnabled)
        {
          EraseHighlightRect();
          BOOL isEnabled = IsWindowEnabled(g_hWnd);
          EnableWindow(g_hWnd, !isEnabled);
        }
        if (g_optToggleVisible)
        {
          EraseHighlightRect();
          /* Toggle visibility: SW_SHOW(5) if hidden, SW_HIDE(0) if visible */
          int showCmd = -IsWindowVisible(g_hWnd);
          showCmd &= ~4; /* clear bit 2 of low byte */
          ShowWindow(g_hWnd, showCmd + 5);
        }
        if (g_optRedrawWindow)
        {
          EraseHighlightRect();
          InvalidateRect(g_hWnd, nullptr, TRUE);
          UpdateWindow(g_hWnd);
        }
        if (g_optSetNoActivate)
        {
          EraseHighlightRect();
          SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
          UpdateWindow(g_hWnd);
        }
        if (g_optSetTopmost)
        {
          EraseHighlightRect();
          SetWindowPos(g_hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
          UpdateWindow(g_hWnd);
        }
        if (g_optCloseWindow)
        {
          EraseHighlightRect();
          PostMessageA(g_hWnd, WM_CLOSE, 0, 0);
        }
      }
    }
    return 0;
  }
  auto origProc = (LRESULT(__stdcall *)(HWND, UINT, WPARAM, LPARAM))GetWindowLongPtrA(hWnd, GWLP_USERDATA);
  return CallWindowProcA(origProc, hWnd, Msg, wParam, lParam);
}

static void DrawBitmapPreview(HWND hwndDlg, int ctrlId, DRAWITEMSTRUCT *dis)
{
  HDC destDC = dis->hDC;
  if (g_hBitmap)
  {
    HDC CompatibleDC = CreateCompatibleDC(destDC);
    SelectObject(CompatibleDC, g_hBitmap);
    RealizePalette(destDC);
    StretchBlt(destDC, dis->rcItem.left, dis->rcItem.top,
               dis->rcItem.right - dis->rcItem.left, dis->rcItem.bottom - dis->rcItem.top,
               CompatibleDC, 0, 0, g_bitmapWidth, g_bitmapHeight, SRCCOPY);
    DeleteDC(CompatibleDC);
  }
}

static void LoadBanner()
{
  g_hBitmap = LoadImageA(g_hInst, MAKEINTRESOURCEA(IDB_BANNER), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_DEFAULTSIZE);
  if (!g_hBitmap)
    return;
  BITMAP pv;
  GetObjectA(g_hBitmap, sizeof(BITMAP), &pv);
  g_bitmapWidth = pv.bmWidth;
  g_bitmapHeight = pv.bmHeight;
}

static HFONT CreateCourierFont()
{
  LOGFONTA lf;
  SecureZeroMemory(&lf, sizeof(LOGFONTA));
  lf.lfHeight = -MulDiv(8, GetSystemDpi(), 72);
  lf.lfWeight = 400;
  lf.lfOutPrecision = 3;
  lf.lfClipPrecision = 2;
  lf.lfQuality = 1;
  lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
  wsprintfA(lf.lfFaceName, "Courier New");
  return CreateFontIndirectA(&lf);
}

static HFONT CreateSansSerifFont()
{
  LOGFONTA lf;
  SecureZeroMemory(&lf, sizeof(LOGFONTA));
  lf.lfHeight = -MulDiv(8, GetSystemDpi(), 72);
  lf.lfWeight = 700;
  lf.lfOutPrecision = 1;
  lf.lfClipPrecision = 2;
  lf.lfQuality = 1;
  lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
  wsprintfA(lf.lfFaceName, "MS Sans Serif");
  return CreateFontIndirectA(&lf);
}

static void InitResources()
{
  g_windowList.Clear();
  g_hdc = CreateDCA("DISPLAY", nullptr, nullptr, nullptr);
  DWORD SysColor = GetSysColor(COLOR_BTNSHADOW);
  g_h = CreatePen(PS_DOT, 0, SysColor);
  DWORD btnFaceColor = GetSysColor(COLOR_BTNFACE);
  g_hBgBrush = CreateSolidBrush(btnFaceColor);
  g_hFontNormal = (HGDIOBJ)CreateCourierFont();
  g_hFontBold = (HGDIOBJ)CreateSansSerifFont();
  g_pfnSetWindowTheme = (PFN_SetWindowTheme)GetProcAddress(GetModuleHandleA("uxtheme.dll"), "SetWindowTheme");
  g_pfnDwmSetWindowAttribute = (PFN_DwmSetWindowAttribute)GetProcAddress(GetModuleHandleA("dwmapi.dll"), "DwmSetWindowAttribute");
  LoadBanner();
}

static BOOL PositionWindowBottomRight(HWND hWnd)
{
  RECT pvParam;
  struct tagRECT Rect;
  SystemParametersInfoA(SPI_GETWORKAREA, 0, &pvParam, 0);
  GetWindowRect(hWnd, &Rect);
  return SetWindowPos(hWnd, nullptr, pvParam.right + Rect.left - Rect.right, pvParam.bottom + Rect.top - Rect.bottom, 0, 0, SWP_NOSIZE);
}

static bool IsDarkModeActive()
{
  DWORD value = 1;
  DWORD size = sizeof(value);
  HKEY key;
  if (RegOpenKeyExA(HKEY_CURRENT_USER,
                    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    0, KEY_READ, &key) == ERROR_SUCCESS)
  {
    RegQueryValueExA(key, "AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&value, &size);
    RegCloseKey(key);
  }
  return value == 0;
}

static LRESULT CALLBACK GroupBoxWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (g_darkMode)
  {
    if (msg == WM_ERASEBKGND)
    {
      RECT rc;
      GetClientRect(hwnd, &rc);
      FillRect((HDC)wParam, &rc, (HBRUSH)g_hBgBrush);
      return 1;
    }
    if (msg == WM_PAINT)
    {
      CHAR text[256];
      GetWindowTextA(hwnd, text, sizeof(text));
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);
      FillRect(hdc, &rc, (HBRUSH)g_hBgBrush);
      HFONT hFont = (HFONT)SendMessageA(hwnd, WM_GETFONT, 0, 0);
      HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
      SIZE sz;
      GetTextExtentPoint32A(hdc, "A", 1, &sz);
      /* Draw a simple gray border; top edge sits at mid-height of the caption */
      HPEN hPen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
      HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
      HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
      Rectangle(hdc, rc.left, rc.top + sz.cy / 2, rc.right - 1, rc.bottom - 1);
      SelectObject(hdc, hOldBrush);
      SelectObject(hdc, hOldPen);
      DeleteObject(hPen);
      /* Draw caption text over the top border line */
      if (text[0])
      {
        SetTextColor(hdc, RGB(242, 242, 242));
        SetBkMode(hdc, TRANSPARENT);
        UINT dtFlags = DT_TOP | DT_SINGLELINE;
        dtFlags |= (GetWindowLongA(hwnd, GWL_STYLE) & BS_CENTER) ? DT_CENTER : DT_LEFT;
        RECT textRc = {rc.left + 8, rc.top, rc.right - 8, rc.top + sz.cy};
        DrawTextA(hdc, text, -1, &textRc, dtFlags);
      }
      SelectObject(hdc, hOldFont);
      EndPaint(hwnd, &ps);
      return 0;
    }
  }
  return CallWindowProcA(g_origGroupBoxProc, hwnd, msg, wParam, lParam);
}

static BOOL CALLBACK ApplyThemeToChild(HWND hwnd, LPARAM dark)
{
  CHAR cls[64];
  GetClassNameA(hwnd, cls, sizeof(cls));
  if (lstrcmpiA(cls, "Button") == 0)
  {
    DWORD style = (DWORD)GetWindowLongA(hwnd, GWL_STYLE) & 0x0F;
    if (style == BS_GROUPBOX)
    {
      /* Subclass once for custom dark WM_PAINT; no theme override needed */
      WNDPROC cur = (WNDPROC)GetWindowLongPtrA(hwnd, GWLP_WNDPROC);
      if (cur != GroupBoxWndProc)
      {
        if (!g_origGroupBoxProc)
          g_origGroupBoxProc = cur;
        SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)GroupBoxWndProc);
      }
    }
    else if (g_pfnSetWindowTheme)
      g_pfnSetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"", nullptr);
  }
  return TRUE;
}

static void ApplyDarkMode(HWND hDlg)
{
  g_darkMode = IsDarkModeActive();
  EnumChildWindows(hDlg, ApplyThemeToChild, (LPARAM)g_darkMode);
  DeleteObject(g_hBgBrush);
  if (g_pfnDwmSetWindowAttribute)
  {
    BOOL dark = g_darkMode ? TRUE : FALSE;
    g_pfnDwmSetWindowAttribute(hDlg, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
  }
  g_hBgBrush = CreateSolidBrush(g_darkMode ? RGB(30, 30, 30) : GetSysColor(COLOR_BTNFACE));
  InvalidateRect(hDlg, nullptr, TRUE);
}

static LRESULT SetControlFont(HWND hDlg, int nIDDlgItem, WPARAM wParam)
{
  return SendMessageA(GetDlgItem(hDlg, nIDDlgItem), WM_SETFONT, wParam, TRUE);
}

BOOL CALLBACK DialogFunc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
  case WM_ACTIVATE:
    if (!(WORD)wParam)
    {
      HWND DlgItem = GetDlgItem(hDlg, IDC_DRAG_BTN);
      SendMessageA(DlgItem, WM_LBUTTONUP, 0, 0);
    }
    break;
  case WM_CLOSE:
    EraseHighlightRect();
    CleanupResources();
    EndDialog(hDlg, lParam);
    break;
  case WM_DRAWITEM:
    if (wParam == IDC_LOGO_PREVIEW)
      DrawBitmapPreview(hDlg, IDC_LOGO_PREVIEW, (DRAWITEMSTRUCT *)lParam);
    break;
  case WM_INITDIALOG:
  {
    InitResources();
    SetWindowTextA(hDlg, "ShoWin");
    PositionWindowBottomRight(hDlg);
    ApplyDarkMode(hDlg);
    HWND hDlga = GetDlgItem(hDlg, IDC_DRAG_BTN);
    HICON IconA = LoadIconA(g_hInst, MAKEINTRESOURCEA(IDI_APP));
    SendMessageA(hDlga, BM_SETIMAGE, IMAGE_ICON, (LPARAM)IconA);
    LONG_PTR prevWndProc = SetWindowLongPtrA(hDlga, GWLP_WNDPROC, (LONG_PTR)CrosshairWndProc);
    SetWindowLongPtrA(hDlga, GWLP_USERDATA, prevWndProc);
    SetControlFont(hDlg, IDC_TITLE, (WPARAM)g_hFontBold);
    SetControlFont(hDlg, IDC_HANDLE, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, IDC_PARENT, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, IDC_OWNER, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, IDC_WINDOWID, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, IDC_CLIENT_COORDS, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, IDC_WINDOW_COORDS, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, IDC_WNDPROC, (WPARAM)g_hFontNormal);
    HWND highlightCheckbox = GetDlgItem(hDlg, IDC_OPT_HIGHLIGHT);
    SendMessageA(highlightCheckbox, BM_SETCHECK, BST_CHECKED, 0);
    g_showHighlight = 1;
    g_lastHoveredHwnd = 0;
    g_hWnd = nullptr;
    g_isDragging = 0;
    break;
  }
  case WM_COMMAND:
    switch (LOWORD(wParam))
    {
    case IDC_OPT_HIDESHOW:
      g_optToggleVisible = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case IDC_OPT_ENABLE:
      g_optToggleEnabled = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case IDC_OPT_HIGHLIGHT:
      g_showHighlight = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case IDC_OPT_REPAINT:
      g_optRedrawWindow = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case IDC_OPT_INVISIBLE:
      g_optIncludeHidden = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case IDC_OPT_CLOSE:
      g_optCloseWindow = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case IDC_COPY:
    {
      static const int len = 9;
      static const int labelIds[len] = {IDC_LBL_TITLE, IDC_LBL_CLASSNAME, IDC_LBL_HANDLE, IDC_LBL_PARENT, IDC_LBL_OWNER, IDC_LBL_WINDOWID, IDC_LBL_WNDPROC, IDC_LBL_CLIENT, IDC_LBL_WINDOW};
      static const int valueIds[len] = {IDC_TITLE, IDC_CLASSNAME, IDC_HANDLE, IDC_PARENT, IDC_OWNER, IDC_WINDOWID, IDC_WNDPROC, IDC_CLIENT_COORDS, IDC_WINDOW_COORDS};
      CHAR string[len * 2][256];
      for (unsigned i = 0; i < len; ++i)
      {
        GetDlgItemTextA(hDlg, labelIds[i], string[0 + i * 2], 256);
        GetDlgItemTextA(hDlg, valueIds[i], string[1 + i * 2], 256);
      }
      CHAR result[4096];
      unsigned result_len = wsprintfA(
          result,
#define P "%s:\t%s\r\n"
          P P P P P P P P "%s:\t%s",
#undef P
          string[0], string[1], string[2], string[3], string[4], string[5], string[6], string[7], string[8], string[9],
          string[10], string[11], string[12], string[13], string[14], string[15], string[16], string[17]);
      OpenClipboard(hDlg);
      EmptyClipboard();
      HGLOBAL hClipMem = GlobalAlloc(GHND, result_len + 1);
      wsprintfA((char *)GlobalLock(hClipMem), "%s", result);
      GlobalUnlock(hClipMem);
      SetClipboardData(CF_TEXT, hClipMem);
      CloseClipboard();
      break;
    }
    case IDC_OPT_ONTOP:
    {
      LRESULT onTopChecked = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0);
      g_optSetNoActivate = onTopChecked == 1;
      if (onTopChecked == 1 && g_optSetTopmost)
        SendDlgItemMessageA(hDlg, IDC_OPT_NOTONTOP, BM_SETCHECK, BST_UNCHECKED, 0);
      break;
    }
    case IDC_OPT_NOTONTOP:
    {
      LRESULT notOnTopChecked = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0);
      g_optSetTopmost = notOnTopChecked == 1;
      if (notOnTopChecked == 1 && g_optSetNoActivate)
        SendDlgItemMessageA(hDlg, IDC_OPT_ONTOP, BM_SETCHECK, BST_UNCHECKED, 0);
      break;
    }
    default:
      return 1;
    }
    break;
  case WM_SETTINGCHANGE:
    ApplyDarkMode(hDlg);
    break;
  case WM_CTLCOLORDLG:
  {
    SetBkColor((HDC)wParam, g_darkMode ? RGB(30, 30, 30) : GetSysColor(COLOR_BTNFACE));
    return (BOOL)(LONG_PTR)g_hBgBrush;
  }
  case WM_CTLCOLORBTN:
  {
    SetTextColor((HDC)wParam, g_darkMode ? RGB(242, 242, 242) : GetSysColor(COLOR_BTNTEXT));
    SetBkColor((HDC)wParam, g_darkMode ? RGB(30, 30, 30) : GetSysColor(COLOR_BTNFACE));
    return (BOOL)(LONG_PTR)g_hBgBrush;
  }
  case WM_CTLCOLORSTATIC:
  {
    LONG WindowLongA = GetWindowLongA((HWND)lParam, GWL_ID);
    COLORREF bg = g_darkMode ? RGB(30, 30, 30) : GetSysColor(COLOR_BTNFACE);
    if (WindowLongA == IDC_CLASSNAME || WindowLongA == IDC_HANDLE || WindowLongA == IDC_PARENT || WindowLongA == IDC_OWNER || WindowLongA == IDC_WINDOWID || WindowLongA == IDC_CLIENT_COORDS || WindowLongA == IDC_WINDOW_COORDS || WindowLongA == IDC_WNDPROC)
    {
      SetTextColor((HDC)wParam, g_darkMode ? RGB(100, 163, 212) : RGB(0, 0, 0xC0));
      SetBkColor((HDC)wParam, bg);
      return (BOOL)(LONG_PTR)g_hBgBrush;
    }
    SetTextColor((HDC)wParam, g_darkMode ? RGB(242, 242, 242) : GetSysColor(COLOR_BTNTEXT));
    SetBkColor((HDC)wParam, bg);
    return (BOOL)(LONG_PTR)g_hBgBrush;
  }
  default:
    return 0;
  }
  return 1;
}

// https://web.archive.org/web/20190205041452/https://blogs.msdn.microsoft.com/oldnewthing/20041025-00/?p=37483
extern "C" IMAGE_DOS_HEADER __ImageBase;

extern "C" void start()
{
  TryEnableDpiAwareness();
  g_hInst = reinterpret_cast<HINSTANCE>(&__ImageBase);
  DialogBoxParamA(g_hInst, MAKEINTRESOURCEA(IDD_MAIN), nullptr, (DLGPROC)DialogFunc, 0);
}
