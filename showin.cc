#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "resource.h"

static const unsigned kWindowListCapacity = 8192;

// https://web.archive.org/web/20190205041452/https://blogs.msdn.microsoft.com/oldnewthing/20041025-00/?p=37483
extern "C" IMAGE_DOS_HEADER __ImageBase;

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
  HGDIOBJ hBgBrush;
  HGDIOBJ hSansSerifFont;
  HGDIOBJ hMonospaceFont;
  HWND hWnd;
  HWND lastHoveredHwnd;
  RECT rc;
  WNDPROC origDragButtonProc;
  WNDPROC origGroupBoxProc;

  typedef HRESULT(WINAPI *PFN_SetWindowTheme)(HWND, LPCWSTR, LPCWSTR);
  typedef HRESULT(WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);

  void InitResources()
  {
    hdc = CreateDCA("DISPLAY", nullptr, nullptr, nullptr);
    DWORD SysColor = GetSysColor(COLOR_BTNSHADOW);
    hPen = CreatePen(PS_DOT, 0, SysColor);
    DWORD btnFaceColor = GetSysColor(COLOR_BTNFACE);
    hBgBrush = CreateSolidBrush(btnFaceColor);
    hMonospaceFont = (HGDIOBJ)CreateMonospaceFont();
    hSansSerifFont = (HGDIOBJ)CreateSansSerifFont();
    pfnSetWindowTheme = (PFN_SetWindowTheme)GetProcAddress(GetModuleHandleA("uxtheme.dll"), "SetWindowTheme");
    dwmapi = LoadLibraryA("dwmapi.dll");
    pfnDwmSetWindowAttribute = (PFN_DwmSetWindowAttribute)GetProcAddress(dwmapi, "DwmSetWindowAttribute");
    LoadBanner();
  }

  void CleanupResources()
  {
    FreeLibrary(dwmapi);
    DeleteObject(hBannerBitmap);
    hBannerBitmap = nullptr;
    DeleteObject(hMonospaceFont);
    DeleteObject(hSansSerifFont);
    EraseHighlightRect();
    DeleteObject(hBgBrush);
    DeleteObject(hPen);
    DeleteDC(hdc);
  }

  void EraseHighlightRect()
  {
    if (!IsRectEmpty(&rc))
      DrawHighlightRect(&rc);
    SetRect(&rc, 0, 0, 0, 0);
  }

  void DrawHighlightRect(RECT *rect)
  {
    if (!showHighlight)
      return;
    HGDIOBJ h = SelectObject(hdc, hPen);
    int rop2 = SetROP2(hdc, R2_XORPEN);
    MoveToEx(hdc, rect->left - 1, rect->top - 1, nullptr);
    LineTo(hdc, rect->right, rect->top - 1);
    LineTo(hdc, rect->right, rect->bottom);
    LineTo(hdc, rect->left - 1, rect->bottom);
    LineTo(hdc, rect->left - 1, rect->top - 1);
    MoveToEx(hdc, rect->left - 2, rect->top - 2, nullptr);
    LineTo(hdc, rect->right + 1, rect->top - 2);
    LineTo(hdc, rect->right + 1, rect->bottom + 1);
    LineTo(hdc, rect->left - 2, rect->bottom + 1);
    LineTo(hdc, rect->left - 2, rect->top - 2);
    SetROP2(hdc, rop2);
    SelectObject(hdc, h);
    SetRect(&rc, rect->left, rect->top, rect->right, rect->bottom);
  }

  void RebuildWindowList()
  {
    ClearWindowList();
    EnumWindows(EnumFunc, (LPARAM)this);
    SortWindowList();
  }

  void DrawBitmapPreview(HWND hwndDlg, int ctrlId, DRAWITEMSTRUCT *dis)
  {
    HDC destDC = dis->hDC;
    if (hBannerBitmap)
    {
      HDC compatibleDC = CreateCompatibleDC(destDC);
      SelectObject(compatibleDC, hBannerBitmap);
      StretchBlt(destDC, dis->rcItem.left, dis->rcItem.top,
                 dis->rcItem.right - dis->rcItem.left, dis->rcItem.bottom - dis->rcItem.top,
                 compatibleDC, 0, 0, bannerBitmapWidth, bannerBitmapHeight, SRCCOPY);
      DeleteDC(compatibleDC);
    }
  }

  void ApplyDarkMode(HWND hDlg)
  {
    darkMode = IsDarkModeActive();
    EnumChildWindows(hDlg, ApplyThemeToChild, (LPARAM)this);
    DeleteObject(hBgBrush);
    if (pfnDwmSetWindowAttribute)
    {
      BOOL dark = darkMode ? TRUE : FALSE;
      pfnDwmSetWindowAttribute(hDlg, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    }
    hBgBrush = CreateSolidBrush(darkMode ? RGB(30, 30, 30) : GetSysColor(COLOR_BTNFACE));
    InvalidateRect(hDlg, nullptr, TRUE);
  }

  void UpdateHover(HWND hWnd, LPARAM lParam)
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
    HWND hitHwnd = FindWindow(pt);
    hWnd = hitHwnd;
    if (hitHwnd && hitHwnd != lastHoveredHwnd)
    {
      lastHoveredHwnd = hitHwnd;
      SendMessageA(hitHwnd, WM_GETTEXT, 256, (LPARAM)string);
      SetDlgItemTextA(hDlg, IDC_TITLE, string);
      GetClassNameA(hWnd, string, 256);
      SetDlgItemTextA(hDlg, IDC_CLASSNAME, string);
      wsprintfA(string, "%-6d (0x%08X)", hWnd, hWnd);
      SetDlgItemTextA(hDlg, IDC_HANDLE, string);
      HWND parentId = GetParent(hWnd);
      wsprintfA(string, "%-6d (0x%08X)", parentId, parentId);
      SetDlgItemTextA(hDlg, IDC_PARENT, string);
      HWND windowId = GetWindow(hWnd, GW_OWNER);
      wsprintfA(string, "%-6d (0x%08X)", windowId, windowId);
      SetDlgItemTextA(hDlg, IDC_OWNER, string);
      LONG windowLongA = GetWindowLongA(hWnd, GWL_ID);
      wsprintfA(string, "%-6d (0x%08X)", windowLongA, windowLongA);
      SetDlgItemTextA(hDlg, IDC_WINDOWID, string);
      LONG_PTR wndProc = GetWindowLongPtrA(hWnd, GWLP_WNDPROC);
      wsprintfA(string, "0x%IX", (SIZE_T)wndProc);
      SetDlgItemTextA(hDlg, IDC_WNDPROC, string);
      RECT rect;
      GetWindowRect(hWnd, &rect);
      RECT rcDst;
      CopyRect(&rcDst, &rect);
      if (parentId)
      {
        ScreenToClient(parentId, (LPPOINT)&rect);
        ScreenToClient(parentId, (LPPOINT)&rect.right);
        wsprintfA(
            string,
            "x:%4d y:%4d  w:%4d h:%4d",
            rect.left,
            rect.top,
            rect.right - rect.left,
            rect.bottom - rect.top);
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

private:
  HDC hdc;
  HPEN hPen;
  HGDIOBJ hBannerBitmap;
  HMODULE dwmapi;
  PFN_DwmSetWindowAttribute pfnDwmSetWindowAttribute;
  PFN_SetWindowTheme pfnSetWindowTheme;
  unsigned bannerBitmapHeight;
  unsigned bannerBitmapWidth;
  struct Entry
  {
    HWND hwnd;
    unsigned area;
  } windowList[kWindowListCapacity];
  unsigned windowCount;

  void ClearWindowList() { windowCount = 0; }

  void SortWindowList()
  {
    // Originally it was using a quick sort, let's use a bubble sort!
    for (unsigned i = 0; i < windowCount - 1; ++i)
      for (unsigned j = 0; j < windowCount - 1 - i; ++j)
        if (windowList[j].area > windowList[j + 1].area)
        {
          Entry tmp = windowList[j];
          windowList[j] = windowList[j + 1];
          windowList[j + 1] = tmp;
        }
  }

  void InsertWindowIntoList(HWND hwnd, unsigned area)
  {
    if (windowCount >= kWindowListCapacity)
      return;
    windowList[windowCount].hwnd = hwnd;
    windowList[windowCount].area = area;
    windowCount++;
  }

  void LoadBanner()
  {
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(&__ImageBase);
    hBannerBitmap = LoadImageA(hInst, MAKEINTRESOURCEA(IDB_BANNER), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_DEFAULTSIZE);
    if (!hBannerBitmap)
      return;
    BITMAP pv;
    GetObjectA(hBannerBitmap, sizeof(BITMAP), &pv);
    bannerBitmapWidth = pv.bmWidth;
    bannerBitmapHeight = pv.bmHeight;
  }

  HWND FindWindow(POINT pt)
  {
    RECT rect;
    for (unsigned i = 0; i < windowCount; ++i)
    {
      GetWindowRect(windowList[i].hwnd, &rect);
      if (PtInRect(&rect, pt))
        return windowList[i].hwnd;
    }
    return nullptr;
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

  static UINT GetSystemDpi()
  {
    typedef UINT(WINAPI * PFN)();
    PFN pfn = (PFN)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForSystem");
    return pfn ? pfn() : 96;
  }

  static HFONT CreateMonospaceFont()
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

  static BOOL CALLBACK EnumFunc(HWND hWnd, LPARAM lParam)
  {
    app_state_t &app_state = *(app_state_t *)lParam;
    if (app_state.optIncludeHidden || IsWindowVisible(hWnd))
    {
      RECT rect;
      GetWindowRect(hWnd, &rect);
      if (!IsRectEmpty(&rect))
        app_state.InsertWindowIntoList(hWnd, (rect.right - rect.left) * (rect.bottom - rect.top));
      EnumChildWindows(hWnd, EnumFunc, (LPARAM)&app_state);
    }
    return 1;
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
};

static LRESULT CALLBACK CrosshairWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  app_state_t &app_state = *(app_state_t *)GetWindowLongPtrA(hWnd, GWLP_USERDATA);
  switch (msg)
  {
  case WM_MOUSEMOVE:
    if (app_state.isDragging)
      app_state.UpdateHover(hWnd, lParam);
    break;
  case WM_LBUTTONDOWN:
  {
    if (app_state.isDragging)
      return 0;
    app_state.RebuildWindowList();
    app_state.lastHoveredHwnd = nullptr;
    SetRect(&app_state.rc, 0, 0, 0, 0);
    SetCapture(hWnd);
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(&__ImageBase);
    HCURSOR CursorA = LoadCursorA(hInst, MAKEINTRESOURCEA(IDC_CROSSHAIR));
    SetCursor(CursorA);
    app_state.isDragging = true;
    app_state.UpdateHover(hWnd, lParam);
    break;
  }
  case WM_LBUTTONUP:
    if (app_state.isDragging)
    {
      app_state.isDragging = false;
      app_state.EraseHighlightRect();
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
          app_state.EraseHighlightRect();
          BOOL isEnabled = IsWindowEnabled(app_state.hWnd);
          EnableWindow(app_state.hWnd, !isEnabled);
        }
        if (app_state.optToggleVisible)
        {
          app_state.EraseHighlightRect();
          /* Toggle visibility: SW_SHOW(5) if hidden, SW_HIDE(0) if visible */
          int showCmd = -IsWindowVisible(app_state.hWnd);
          showCmd &= ~4; /* clear bit 2 of low byte */
          ShowWindow(app_state.hWnd, showCmd + 5);
        }
        if (app_state.optRedrawWindow)
        {
          app_state.EraseHighlightRect();
          InvalidateRect(app_state.hWnd, nullptr, TRUE);
          UpdateWindow(app_state.hWnd);
        }
        if (app_state.optSetNoActivate)
        {
          app_state.EraseHighlightRect();
          SetWindowPos(app_state.hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
          UpdateWindow(app_state.hWnd);
        }
        if (app_state.optSetTopmost)
        {
          app_state.EraseHighlightRect();
          SetWindowPos(app_state.hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
          UpdateWindow(app_state.hWnd);
        }
        if (app_state.optCloseWindow)
        {
          app_state.EraseHighlightRect();
          PostMessageA(app_state.hWnd, WM_CLOSE, 0, 0);
        }
      }
    }
    return 0;
  }
  return CallWindowProcA(app_state.origDragButtonProc, hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK DialogFunc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_INITDIALOG)
    // app_state isn't ready on WM_INITDIALOG
    return 1;
  app_state_t &app_state = *(app_state_t *)GetWindowLongPtrA(hDlg, GWLP_USERDATA);
  switch (msg)
  {
  case WM_ACTIVATE:
    if (!(WORD)wParam)
    {
      HWND DlgItem = GetDlgItem(hDlg, IDC_DRAG_BTN);
      SendMessageA(DlgItem, WM_LBUTTONUP, 0, 0);
    }
    break;
  case WM_CLOSE:
    app_state.EraseHighlightRect();
    app_state.CleanupResources();
    EndDialog(hDlg, lParam);
    ExitProcess(0);
    break;
  case WM_DRAWITEM:
    if (wParam == IDC_LOGO_PREVIEW)
      app_state.DrawBitmapPreview(hDlg, IDC_LOGO_PREVIEW, (DRAWITEMSTRUCT *)lParam);
    break;
  case WM_SHOWWINDOW:
  {
    HWND hDlga = GetDlgItem(hDlg, IDC_DRAG_BTN);
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(&__ImageBase);
    HICON IconA = LoadIconA(hInst, MAKEINTRESOURCEA(IDI_APP));
    SendMessageA(hDlga, BM_SETIMAGE, IMAGE_ICON, (LPARAM)IconA);
    app_state.origDragButtonProc = (WNDPROC)SetWindowLongPtrA(hDlga, GWLP_WNDPROC, (LONG_PTR)CrosshairWndProc);
    SetWindowLongPtrA(hDlga, GWLP_USERDATA, (LPARAM)&app_state);
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
    app_state.ApplyDarkMode(hDlg);
    break;
  case WM_CTLCOLORDLG:
  {
    SetBkColor((HDC)wParam, app_state.darkMode ? RGB(30, 30, 30) : GetSysColor(COLOR_BTNFACE));
    return (LRESULT)app_state.hBgBrush;
  }
  case WM_CTLCOLORBTN:
  {
    SetTextColor((HDC)wParam, app_state.darkMode ? RGB(242, 242, 242) : GetSysColor(COLOR_BTNTEXT));
    SetBkColor((HDC)wParam, app_state.darkMode ? RGB(30, 30, 30) : GetSysColor(COLOR_BTNFACE));
    return (LRESULT)app_state.hBgBrush;
  }
  case WM_CTLCOLORSTATIC:
  {
    switch (GetWindowLongA((HWND)lParam, GWL_ID))
    {
    case IDC_CLASSNAME:
    case IDC_HANDLE:
    case IDC_PARENT:
    case IDC_OWNER:
    case IDC_WINDOWID:
    case IDC_CLIENT_COORDS:
    case IDC_WINDOW_COORDS:
    case IDC_WNDPROC:
      SetTextColor((HDC)wParam, app_state.darkMode ? RGB(100, 163, 212) : RGB(0, 0, 0xC0));
      break;
    default:
      SetTextColor((HDC)wParam, app_state.darkMode ? RGB(242, 242, 242) : GetSysColor(COLOR_BTNTEXT));
    }
    SetBkColor((HDC)wParam, app_state.darkMode ? RGB(30, 30, 30) : GetSysColor(COLOR_BTNFACE));
    return (LRESULT)app_state.hBgBrush;
  }
  default:
    return 0;
  }
  return 1;
}

static void PositionWindowBottomRight(HWND hWnd)
{
  RECT pvParam;
  RECT rect;
  SystemParametersInfoA(SPI_GETWORKAREA, 0, &pvParam, 0);
  GetWindowRect(hWnd, &rect);
  SetWindowPos(hWnd, nullptr, pvParam.right + rect.left - rect.right, pvParam.bottom + rect.top - rect.bottom, 0, 0, SWP_NOSIZE);
}

static void TryEnableDpiAwareness()
{
  typedef BOOL(WINAPI * PFN)(INT);
  PFN pfn = (PFN)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
  if (pfn)
    pfn(-2); /* DPI_AWARENESS_CONTEXT_SYSTEM_AWARE */
}

static void SetControlFont(HWND hDlg, int nIDDlgItem, HGDIOBJ font)
{
  SendMessageA(GetDlgItem(hDlg, nIDDlgItem), WM_SETFONT, (WPARAM)font, TRUE);
}

extern "C" void start()
{
  TryEnableDpiAwareness();
  HINSTANCE hInst = reinterpret_cast<HINSTANCE>(&__ImageBase);
  HWND hwnd = CreateDialogParamA(hInst, MAKEINTRESOURCEA(IDD_MAIN), nullptr, (DLGPROC)DialogFunc, 0);
  PositionWindowBottomRight(hwnd);
  app_state_t state;
  SecureZeroMemory(&state, sizeof(app_state_t));
  state.InitResources();
  SetControlFont(hwnd, IDC_TITLE, state.hSansSerifFont);
  SetControlFont(hwnd, IDC_HANDLE, state.hMonospaceFont);
  SetControlFont(hwnd, IDC_PARENT, state.hMonospaceFont);
  SetControlFont(hwnd, IDC_OWNER, state.hMonospaceFont);
  SetControlFont(hwnd, IDC_WINDOWID, state.hMonospaceFont);
  SetControlFont(hwnd, IDC_CLIENT_COORDS, state.hMonospaceFont);
  SetControlFont(hwnd, IDC_WINDOW_COORDS, state.hMonospaceFont);
  SetControlFont(hwnd, IDC_WNDPROC, state.hMonospaceFont);
  HWND highlightCheckbox = GetDlgItem(hwnd, IDC_OPT_HIGHLIGHT);
  SendMessageA(highlightCheckbox, BM_SETCHECK, BST_CHECKED, 0);
  state.showHighlight = true;
  state.ApplyDarkMode(hwnd);
  SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)&state);
  ShowWindow(hwnd, SW_SHOW);
  MSG msg;
  while (GetMessageA(&msg, nullptr, 0, 0) > 0)
  {
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }
  ExitProcess(msg.wParam);
}
