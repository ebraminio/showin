#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resource.h"

static const unsigned kWindowListCapacity = 8192;

struct WindowList
{
  void Push(HWND hwnd, unsigned area)
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
    unsigned area;
  } buf[kWindowListCapacity];
  unsigned count;
};

typedef HRESULT(WINAPI *PFN_SetWindowTheme)(HWND, LPCWSTR, LPCWSTR);
typedef HRESULT(WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);

struct app_state_t
{
  bool darkMode;
  bool isDragging;
  bool optCloseWindow;
  bool optIncludeHidden;
  bool optRedrawWindow;
  bool optSetNoActivate;
  bool optSetTopmost;
  bool optToggleEnabled;
  bool optToggleVisible;
  bool showHighlight;
  HDC hdc;
  HGDIOBJ h;
  HGDIOBJ hBannerBitmap;
  HGDIOBJ hBgBrush;
  HGDIOBJ hFontBold;
  HGDIOBJ hFontNormal;
  HINSTANCE hInst;
  HWND hWnd;
  HWND lastHoveredHwnd;
  PFN_DwmSetWindowAttribute pfnDwmSetWindowAttribute;
  PFN_SetWindowTheme pfnSetWindowTheme;
  RECT rc;
  unsigned bannerBitmapHeight;
  unsigned bannerBitmapWidth;
  WindowList windowList;
  WNDPROC origGroupBoxProc;
} state;

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

static void DrawHighlightRect(app_state_t &app_state, RECT *rect)
{
  if (!app_state.showHighlight)
    return;
  HGDIOBJ h = SelectObject(app_state.hdc, app_state.h);
  int rop2 = SetROP2(app_state.hdc, R2_XORPEN);
  MoveToEx(app_state.hdc, rect->left - 1, rect->top - 1, nullptr);
  LineTo(app_state.hdc, rect->right, rect->top - 1);
  LineTo(app_state.hdc, rect->right, rect->bottom);
  LineTo(app_state.hdc, rect->left - 1, rect->bottom);
  LineTo(app_state.hdc, rect->left - 1, rect->top - 1);
  MoveToEx(app_state.hdc, rect->left - 2, rect->top - 2, nullptr);
  LineTo(app_state.hdc, rect->right + 1, rect->top - 2);
  LineTo(app_state.hdc, rect->right + 1, rect->bottom + 1);
  LineTo(app_state.hdc, rect->left - 2, rect->bottom + 1);
  LineTo(app_state.hdc, rect->left - 2, rect->top - 2);
  SetROP2(app_state.hdc, rop2);
  SelectObject(app_state.hdc, h);
  SetRect(&app_state.rc, rect->left, rect->top, rect->right, rect->bottom);
}

static BOOL EraseHighlightRect(app_state_t &app_state)
{
  if (!IsRectEmpty(&app_state.rc))
    DrawHighlightRect(app_state, &app_state.rc);
  return SetRect(&app_state.rc, 0, 0, 0, 0);
}

static BOOL CleanupResources(app_state_t &app_state)
{
  DeleteObject(app_state.hBannerBitmap);
  app_state.hBannerBitmap = nullptr;
  DeleteObject(app_state.hFontNormal);
  DeleteObject(app_state.hFontBold);
  EraseHighlightRect(app_state);
  DeleteObject(app_state.hBgBrush);
  DeleteObject(app_state.h);
  return DeleteDC(app_state.hdc);
}

BOOL CALLBACK EnumFunc(HWND hWnd, LPARAM lParam)
{
  app_state_t &app_state = *(app_state_t *)lParam;
  if (app_state.optIncludeHidden || IsWindowVisible(hWnd))
  {
    struct tagRECT rect;
    GetWindowRect(hWnd, &rect);
    if (!IsRectEmpty(&rect))
      app_state.windowList.Push(hWnd, (rect.right - rect.left) * (rect.bottom - rect.top));
    EnumChildWindows(hWnd, EnumFunc, (LPARAM)&app_state);
  }
  return 1;
}

static void RebuildWindowList(app_state_t &app_state)
{
  app_state.windowList.Clear();
  EnumWindows(EnumFunc, (LPARAM)&app_state);
  app_state.windowList.Sort();
}

static void UpdateHover(app_state_t &app_state, HWND hWnd, LPARAM lParam)
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
  HWND hitHwnd = app_state.windowList.HitHwnd(pt);
  app_state.hWnd = hitHwnd;
  if (hitHwnd && hitHwnd != app_state.lastHoveredHwnd)
  {
    app_state.lastHoveredHwnd = hitHwnd;
    SendMessageA(hitHwnd, WM_GETTEXT, 256, (LPARAM)string);
    SetDlgItemTextA(hDlg, IDC_TITLE, string);
    GetClassNameA(app_state.hWnd, string, 256);
    SetDlgItemTextA(hDlg, IDC_CLASSNAME, string);
    wsprintfA(string, "%-6d (0x%08X)", app_state.hWnd, app_state.hWnd);
    SetDlgItemTextA(hDlg, IDC_HANDLE, string);
    HWND Parent = GetParent(app_state.hWnd);
    wsprintfA(string, "%-6d (0x%08X)", Parent, Parent);
    SetDlgItemTextA(hDlg, IDC_PARENT, string);
    HWND Window = GetWindow(app_state.hWnd, GW_OWNER);
    wsprintfA(string, "%-6d (0x%08X)", Window, Window);
    SetDlgItemTextA(hDlg, IDC_OWNER, string);
    LONG WindowLongA = GetWindowLongA(app_state.hWnd, GWL_ID);
    wsprintfA(string, "%-6d (0x%08X)", WindowLongA, WindowLongA);
    SetDlgItemTextA(hDlg, IDC_WINDOWID, string);
    LONG_PTR wndProc = GetWindowLongPtrA(app_state.hWnd, GWLP_WNDPROC);
    wsprintfA(string, "0x%IX", (SIZE_T)wndProc);
    SetDlgItemTextA(hDlg, IDC_WNDPROC, string);
    RECT Rect;
    GetWindowRect(app_state.hWnd, &Rect);
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
    EraseHighlightRect(app_state);
    DrawHighlightRect(app_state, &rcDst);
  }
}

LRESULT CALLBACK CrosshairWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
  app_state_t &app_state = state;
  switch (Msg)
  {
  case WM_MOUSEMOVE:
    if (app_state.isDragging)
      UpdateHover(app_state, hWnd, lParam);
    break;
  case WM_LBUTTONDOWN:
  {
    if (app_state.isDragging)
      return 0;
    RebuildWindowList(app_state);
    app_state.lastHoveredHwnd = 0;
    SetRect(&app_state.rc, 0, 0, 0, 0);
    SetCapture(hWnd);
    HCURSOR CursorA = LoadCursorA(app_state.hInst, MAKEINTRESOURCEA(IDC_CROSSHAIR));
    SetCursor(CursorA);
    app_state.isDragging = 1;
    UpdateHover(app_state, hWnd, lParam);
    break;
  }
  case WM_LBUTTONUP:
    if (app_state.isDragging)
    {
      app_state.isDragging = 0;
      EraseHighlightRect(app_state);
      ReleaseCapture();
      HCURSOR arrowCursor = LoadCursorA(nullptr, IDC_ARROW);
      SetCursor(arrowCursor);
      HWND parentWnd = GetParent(hWnd);
      SetForegroundWindow(parentWnd);
    }
    return 0;
  case WM_RBUTTONDOWN:
    if (app_state.isDragging)
    {
      HWND dlgWnd = GetParent(hWnd);
      if (app_state.hWnd != dlgWnd && GetParent(app_state.hWnd) != dlgWnd)
      {
        if (app_state.optToggleEnabled)
        {
          EraseHighlightRect(app_state);
          BOOL isEnabled = IsWindowEnabled(app_state.hWnd);
          EnableWindow(app_state.hWnd, !isEnabled);
        }
        if (app_state.optToggleVisible)
        {
          EraseHighlightRect(app_state);
          /* Toggle visibility: SW_SHOW(5) if hidden, SW_HIDE(0) if visible */
          int showCmd = -IsWindowVisible(app_state.hWnd);
          showCmd &= ~4; /* clear bit 2 of low byte */
          ShowWindow(app_state.hWnd, showCmd + 5);
        }
        if (app_state.optRedrawWindow)
        {
          EraseHighlightRect(app_state);
          InvalidateRect(app_state.hWnd, nullptr, TRUE);
          UpdateWindow(app_state.hWnd);
        }
        if (app_state.optSetNoActivate)
        {
          EraseHighlightRect(app_state);
          SetWindowPos(app_state.hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
          UpdateWindow(app_state.hWnd);
        }
        if (app_state.optSetTopmost)
        {
          EraseHighlightRect(app_state);
          SetWindowPos(app_state.hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
          UpdateWindow(app_state.hWnd);
        }
        if (app_state.optCloseWindow)
        {
          EraseHighlightRect(app_state);
          PostMessageA(app_state.hWnd, WM_CLOSE, 0, 0);
        }
      }
    }
    return 0;
  }
  auto origProc = (LRESULT(__stdcall *)(HWND, UINT, WPARAM, LPARAM))GetWindowLongPtrA(hWnd, GWLP_USERDATA);
  return CallWindowProcA(origProc, hWnd, Msg, wParam, lParam);
}

static void DrawBitmapPreview(app_state_t &app_state, HWND hwndDlg, int ctrlId, DRAWITEMSTRUCT *dis)
{
  HDC destDC = dis->hDC;
  if (app_state.hBannerBitmap)
  {
    HDC CompatibleDC = CreateCompatibleDC(destDC);
    SelectObject(CompatibleDC, app_state.hBannerBitmap);
    RealizePalette(destDC);
    StretchBlt(destDC, dis->rcItem.left, dis->rcItem.top,
               dis->rcItem.right - dis->rcItem.left, dis->rcItem.bottom - dis->rcItem.top,
               CompatibleDC, 0, 0, app_state.bannerBitmapWidth, app_state.bannerBitmapHeight, SRCCOPY);
    DeleteDC(CompatibleDC);
  }
}

static void LoadBanner(app_state_t &app_state)
{
  app_state.hBannerBitmap = LoadImageA(app_state.hInst, MAKEINTRESOURCEA(IDB_BANNER), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_DEFAULTSIZE);
  if (!app_state.hBannerBitmap)
    return;
  BITMAP pv;
  GetObjectA(app_state.hBannerBitmap, sizeof(BITMAP), &pv);
  app_state.bannerBitmapWidth = pv.bmWidth;
  app_state.bannerBitmapHeight = pv.bmHeight;
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

static void InitResources(app_state_t &app_state)
{
  app_state.hdc = CreateDCA("DISPLAY", nullptr, nullptr, nullptr);
  DWORD SysColor = GetSysColor(COLOR_BTNSHADOW);
  app_state.h = CreatePen(PS_DOT, 0, SysColor);
  DWORD btnFaceColor = GetSysColor(COLOR_BTNFACE);
  app_state.hBgBrush = CreateSolidBrush(btnFaceColor);
  app_state.hFontNormal = (HGDIOBJ)CreateCourierFont();
  app_state.hFontBold = (HGDIOBJ)CreateSansSerifFont();
  app_state.pfnSetWindowTheme = (PFN_SetWindowTheme)GetProcAddress(GetModuleHandleA("uxtheme.dll"), "SetWindowTheme");
  app_state.pfnDwmSetWindowAttribute = (PFN_DwmSetWindowAttribute)GetProcAddress(GetModuleHandleA("dwmapi.dll"), "DwmSetWindowAttribute");
  LoadBanner(app_state);
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
  app_state_t &app_state = *(app_state_t *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
  if (app_state.darkMode)
  {
    if (msg == WM_ERASEBKGND)
    {
      RECT rc;
      GetClientRect(hwnd, &rc);
      FillRect((HDC)wParam, &rc, (HBRUSH)app_state.hBgBrush);
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
      FillRect(hdc, &rc, (HBRUSH)app_state.hBgBrush);
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
  return CallWindowProcA(app_state.origGroupBoxProc, hwnd, msg, wParam, lParam);
}

static BOOL CALLBACK ApplyThemeToChild(HWND hwnd, LPARAM lParam)
{
  app_state_t &app_state = *(app_state_t *)lParam;
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
        if (!app_state.origGroupBoxProc)
          app_state.origGroupBoxProc = cur;
        SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)GroupBoxWndProc);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)&app_state);
      }
    }
    else if (app_state.pfnSetWindowTheme)
      app_state.pfnSetWindowTheme(hwnd, app_state.darkMode ? L"DarkMode_Explorer" : L"", nullptr);
  }
  return TRUE;
}

static void ApplyDarkMode(app_state_t &app_state, HWND hDlg)
{
  app_state.darkMode = IsDarkModeActive();
  EnumChildWindows(hDlg, ApplyThemeToChild, (LPARAM)&app_state);
  DeleteObject(app_state.hBgBrush);
  if (app_state.pfnDwmSetWindowAttribute)
  {
    BOOL dark = app_state.darkMode ? TRUE : FALSE;
    app_state.pfnDwmSetWindowAttribute(hDlg, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
  }
  app_state.hBgBrush = CreateSolidBrush(app_state.darkMode ? RGB(30, 30, 30) : GetSysColor(COLOR_BTNFACE));
  InvalidateRect(hDlg, nullptr, TRUE);
}

static LRESULT SetControlFont(HWND hDlg, int nIDDlgItem, HGDIOBJ font)
{
  return SendMessageA(GetDlgItem(hDlg, nIDDlgItem), WM_SETFONT, (WPARAM)font, TRUE);
}

BOOL CALLBACK DialogFunc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  app_state_t &app_state = state;
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
    EraseHighlightRect(app_state);
    CleanupResources(app_state);
    EndDialog(hDlg, lParam);
    break;
  case WM_DRAWITEM:
    if (wParam == IDC_LOGO_PREVIEW)
      DrawBitmapPreview(app_state, hDlg, IDC_LOGO_PREVIEW, (DRAWITEMSTRUCT *)lParam);
    break;
  case WM_INITDIALOG:
  {
    InitResources(app_state);
    SetWindowTextA(hDlg, "ShoWin");
    PositionWindowBottomRight(hDlg);
    ApplyDarkMode(app_state, hDlg);
    HWND hDlga = GetDlgItem(hDlg, IDC_DRAG_BTN);
    HICON IconA = LoadIconA(app_state.hInst, MAKEINTRESOURCEA(IDI_APP));
    SendMessageA(hDlga, BM_SETIMAGE, IMAGE_ICON, (LPARAM)IconA);
    LONG_PTR prevWndProc = SetWindowLongPtrA(hDlga, GWLP_WNDPROC, (LONG_PTR)CrosshairWndProc);
    SetWindowLongPtrA(hDlga, GWLP_USERDATA, prevWndProc);
    SetControlFont(hDlg, IDC_TITLE, app_state.hFontBold);
    SetControlFont(hDlg, IDC_HANDLE, app_state.hFontNormal);
    SetControlFont(hDlg, IDC_PARENT, app_state.hFontNormal);
    SetControlFont(hDlg, IDC_OWNER, app_state.hFontNormal);
    SetControlFont(hDlg, IDC_WINDOWID, app_state.hFontNormal);
    SetControlFont(hDlg, IDC_CLIENT_COORDS, app_state.hFontNormal);
    SetControlFont(hDlg, IDC_WINDOW_COORDS, app_state.hFontNormal);
    SetControlFont(hDlg, IDC_WNDPROC, app_state.hFontNormal);
    HWND highlightCheckbox = GetDlgItem(hDlg, IDC_OPT_HIGHLIGHT);
    SendMessageA(highlightCheckbox, BM_SETCHECK, BST_CHECKED, 0);
    app_state.showHighlight = 1;
    app_state.lastHoveredHwnd = 0;
    app_state.hWnd = nullptr;
    app_state.isDragging = 0;
    break;
  }
  case WM_COMMAND:
    switch (LOWORD(wParam))
    {
    case IDC_OPT_HIDESHOW:
      app_state.optToggleVisible = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case IDC_OPT_ENABLE:
      app_state.optToggleEnabled = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case IDC_OPT_HIGHLIGHT:
      app_state.showHighlight = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case IDC_OPT_REPAINT:
      app_state.optRedrawWindow = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case IDC_OPT_INVISIBLE:
      app_state.optIncludeHidden = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case IDC_OPT_CLOSE:
      app_state.optCloseWindow = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case IDC_COPY:
    {
      static const unsigned len = 9;
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
      bool onTopChecked = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      app_state.optSetNoActivate = onTopChecked;
      if (onTopChecked && app_state.optSetTopmost)
        SendDlgItemMessageA(hDlg, IDC_OPT_NOTONTOP, BM_SETCHECK, BST_UNCHECKED, 0);
      break;
    }
    case IDC_OPT_NOTONTOP:
    {
      bool notOnTopChecked = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      app_state.optSetTopmost = notOnTopChecked;
      if (notOnTopChecked && app_state.optSetNoActivate)
        SendDlgItemMessageA(hDlg, IDC_OPT_ONTOP, BM_SETCHECK, BST_UNCHECKED, 0);
      break;
    }
    default:
      return 1;
    }
    break;
  case WM_SETTINGCHANGE:
    ApplyDarkMode(app_state, hDlg);
    break;
  case WM_CTLCOLORDLG:
  {
    SetBkColor((HDC)wParam, app_state.darkMode ? RGB(30, 30, 30) : GetSysColor(COLOR_BTNFACE));
    return (BOOL)(LONG_PTR)app_state.hBgBrush;
  }
  case WM_CTLCOLORBTN:
  {
    SetTextColor((HDC)wParam, app_state.darkMode ? RGB(242, 242, 242) : GetSysColor(COLOR_BTNTEXT));
    SetBkColor((HDC)wParam, app_state.darkMode ? RGB(30, 30, 30) : GetSysColor(COLOR_BTNFACE));
    return (BOOL)(LONG_PTR)app_state.hBgBrush;
  }
  case WM_CTLCOLORSTATIC:
  {
    LONG WindowLongA = GetWindowLongA((HWND)lParam, GWL_ID);
    COLORREF bg = app_state.darkMode ? RGB(30, 30, 30) : GetSysColor(COLOR_BTNFACE);
    if (WindowLongA == IDC_CLASSNAME || WindowLongA == IDC_HANDLE || WindowLongA == IDC_PARENT || WindowLongA == IDC_OWNER || WindowLongA == IDC_WINDOWID || WindowLongA == IDC_CLIENT_COORDS || WindowLongA == IDC_WINDOW_COORDS || WindowLongA == IDC_WNDPROC)
    {
      SetTextColor((HDC)wParam, app_state.darkMode ? RGB(100, 163, 212) : RGB(0, 0, 0xC0));
      SetBkColor((HDC)wParam, bg);
      return (BOOL)(LONG_PTR)app_state.hBgBrush;
    }
    SetTextColor((HDC)wParam, app_state.darkMode ? RGB(242, 242, 242) : GetSysColor(COLOR_BTNTEXT));
    SetBkColor((HDC)wParam, bg);
    return (BOOL)(LONG_PTR)app_state.hBgBrush;
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
  SecureZeroMemory(&state, sizeof(app_state_t));
  TryEnableDpiAwareness();
  state.hInst = reinterpret_cast<HINSTANCE>(&__ImageBase);
  DialogBoxParamA(state.hInst, MAKEINTRESOURCEA(IDD_MAIN), nullptr, (DLGPROC)DialogFunc, 0);
}
