#include <windows.h>
#include "resource.h"

struct WindowEntry
{
  HWND hwnd;
  int area;
};

static const int kWindowListCapacity = 1024;

struct WindowList
{
  WindowEntry buf[kWindowListCapacity];
  int count;

  void Push(HWND hwnd, int area)
  {
    if (count >= kWindowListCapacity)
      return;
    buf[count].hwnd = hwnd;
    buf[count].area = area;
    count++;
  }

  WindowEntry &operator[](int idx)
  {
    static WindowEntry sentinel = {};
    if (idx < 0 || idx >= count)
      return sentinel;
    return buf[idx];
  }

  struct Iterator
  {
    WindowList *list;
    int idx;
    WindowEntry &operator*() { return (*list)[idx]; }
    Iterator &operator++()
    {
      idx++;
      return *this;
    }
    bool operator!=(const Iterator &other) const { return idx != other.idx; }
  };

  Iterator begin() { return {this, 0}; }
  Iterator end() { return {this, count}; }

  void Clear() { count = 0; }

  void Sort()
  {
    // Originally it was using a quick sort, let's use a bubble sort anyway
    for (int i = 0; i < count - 1; i++)
      for (int j = 0; j < count - 1 - i; j++)
        if (buf[j].area > buf[j + 1].area)
        {
          WindowEntry tmp = buf[j];
          buf[j] = buf[j + 1];
          buf[j + 1] = tmp;
        }
  }
};

/* Label static controls (left column) and their paired value controls (right column)
   used by the "Copy to clipboard" function (IDC_COPY). */
int g_labelControlIds[] = {IDC_LBL_TITLE, IDC_LBL_CLASSNAME, IDC_LBL_HANDLE, IDC_LBL_PARENT, IDC_LBL_OWNER, IDC_LBL_WINDOWID, IDC_LBL_WNDPROC, IDC_LBL_CLIENT, IDC_LBL_WINDOW};
int g_valueControlIds[] = {IDC_TITLE, IDC_CLASSNAME, IDC_HANDLE, IDC_PARENT, IDC_OWNER, IDC_WINDOWID, IDC_WNDPROC, IDC_CLIENT_COORDS, IDC_WINDOW_COORDS};
CHAR pwszDriver[] = "DISPLAY";
int g_optCloseWindow = 0;
HWND g_lastHoveredHwnd = nullptr;
int g_bitmapHeight = 0;
int g_isDragging = 0;
int g_optIncludeHidden = 0;
int g_optToggleEnabled = 0;
HGDIOBJ g_hFontBold = nullptr;
HINSTANCE hInst = nullptr;
HGDIOBJ g_h = nullptr;
int g_optSetTopmost = 0;
HGDIOBJ g_hFontNormal = nullptr;
int g_optRedrawWindow = 0;
HGDIOBJ g_hBgBrush = nullptr;
HWND ghWnd = nullptr;
RECT rc = {0, 0, 0, 0};
POINT pt = {0, 0};
int g_bitmapWidth = 0;
HGDIOBJ hPal = nullptr;
HDC hdc = nullptr;
int g_optSetNoActivate = 0;
int g_optToggleVisible = 0;
int g_showHighlight = 0;
HGDIOBJ ho = nullptr;
WindowList g_windowList;
bool g_darkMode = false;
WNDPROC g_origGroupBoxProc = nullptr;

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

void DrawHighlightRect(RECT *rect)
{
  if (!g_showHighlight)
    return;
  HGDIOBJ h = SelectObject(hdc, g_h);
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

BOOL EraseHighlightRect()
{
  if (!IsRectEmpty(&rc))
    DrawHighlightRect(&rc);
  return SetRect(&rc, 0, 0, 0, 0);
}

BOOL CleanupResources()
{
  DeleteObject(ho);
  ho = nullptr;
  DeleteObject(hPal);
  hPal = nullptr;
  DeleteObject(g_hFontNormal);
  DeleteObject(g_hFontBold);
  EraseHighlightRect();
  DeleteObject(g_hBgBrush);
  DeleteObject(g_h);
  return DeleteDC(hdc);
}

HWND HitTestWindowList()
{
  struct tagRECT Rect;

  for (auto &e : g_windowList)
  {
    GetWindowRect(e.hwnd, &Rect);
    if (PtInRect(&Rect, pt))
      return e.hwnd;
  }
  return nullptr;
}

BOOL CALLBACK EnumFunc(HWND hWnd, LPARAM lParam)
{
  if (g_optIncludeHidden || IsWindowVisible(hWnd))
  {
    struct tagRECT Rect;
    GetWindowRect(hWnd, &Rect);
    if (!IsRectEmpty(&Rect))
      g_windowList.Push(hWnd, (Rect.right - Rect.left) * (Rect.bottom - Rect.top));
    EnumChildWindows(hWnd, EnumFunc, 0);
  }
  return 1;
}

void RebuildWindowList()
{
  g_windowList.Clear();
  EnumWindows(EnumFunc, 0);
  g_windowList.Sort();
}

static void UpdateHover(HWND hWnd, LPARAM lParam)
{
  pt.x = (__int16)lParam;
  pt.y = (short)HIWORD(lParam);
  ClientToScreen(hWnd, &pt);
  HWND hDlg = GetParent(hWnd);
  CHAR String[256];
  wsprintfA(String, "%4hd", pt.x);
  SetDlgItemTextA(hDlg, IDC_MOUSE_X, String);
  wsprintfA(String, "%4hd", pt.y);
  SetDlgItemTextA(hDlg, IDC_MOUSE_Y, String);
  HWND hitHwnd = HitTestWindowList();
  ghWnd = hitHwnd;
  if (hitHwnd && hitHwnd != g_lastHoveredHwnd)
  {
    g_lastHoveredHwnd = hitHwnd;
    SendMessageA(hitHwnd, WM_GETTEXT, 256, (LPARAM)String);
    SetDlgItemTextA(hDlg, IDC_TITLE, String);
    GetClassNameA(ghWnd, String, 256);
    SetDlgItemTextA(hDlg, IDC_CLASSNAME, String);
    wsprintfA(String, "%-6d (0x%08X)", ghWnd, ghWnd);
    SetDlgItemTextA(hDlg, IDC_HANDLE, String);
    HWND Parent = GetParent(ghWnd);
    wsprintfA(String, "%-6d (0x%08X)", Parent, Parent);
    SetDlgItemTextA(hDlg, IDC_PARENT, String);
    HWND Window = GetWindow(ghWnd, GW_OWNER);
    wsprintfA(String, "%-6d (0x%08X)", Window, Window);
    SetDlgItemTextA(hDlg, IDC_OWNER, String);
    LONG WindowLongA = GetWindowLongA(ghWnd, GWL_ID);
    wsprintfA(String, "%-6d (0x%08X)", WindowLongA, WindowLongA);
    SetDlgItemTextA(hDlg, IDC_WINDOWID, String);
    LONG_PTR wndProc = GetWindowLongPtrA(ghWnd, GWLP_WNDPROC);
    wsprintfA(String, "0x%IX", (SIZE_T)wndProc);
    SetDlgItemTextA(hDlg, IDC_WNDPROC, String);
    RECT Rect;
    GetWindowRect(ghWnd, &Rect);
    RECT rcDst;
    CopyRect(&rcDst, &Rect);
    if (Parent)
    {
      ScreenToClient(Parent, (LPPOINT)&Rect);
      ScreenToClient(Parent, (LPPOINT)&Rect.right);
      wsprintfA(
          String,
          "x:%4d y:%4d  w:%4d h:%4d",
          Rect.left,
          Rect.top,
          Rect.right - Rect.left,
          Rect.bottom - Rect.top);
      SetDlgItemTextA(hDlg, IDC_CLIENT_COORDS, String);
    }
    wsprintfA(
        String,
        "x:%4d y:%4d  w:%4d h:%4d",
        rcDst.left,
        rcDst.top,
        rcDst.right - rcDst.left,
        rcDst.bottom - rcDst.top);
    SetDlgItemTextA(hDlg, IDC_WINDOW_COORDS, String);
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
    SetRect(&rc, 0, 0, 0, 0);
    SetCapture(hWnd);
    HCURSOR CursorA = LoadCursorA(hInst, MAKEINTRESOURCEA(IDC_CROSSHAIR));
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
      if (ghWnd != dlgWnd && GetParent(ghWnd) != dlgWnd)
      {
        if (g_optToggleEnabled)
        {
          EraseHighlightRect();
          BOOL isEnabled = IsWindowEnabled(ghWnd);
          EnableWindow(ghWnd, !isEnabled);
        }
        if (g_optToggleVisible)
        {
          EraseHighlightRect();
          /* Toggle visibility: SW_SHOW(5) if hidden, SW_HIDE(0) if visible */
          int showCmd = -IsWindowVisible(ghWnd);
          showCmd &= ~4; /* clear bit 2 of low byte */
          ShowWindow(ghWnd, showCmd + 5);
        }
        if (g_optRedrawWindow)
        {
          EraseHighlightRect();
          InvalidateRect(ghWnd, nullptr, TRUE);
          UpdateWindow(ghWnd);
        }
        if (g_optSetNoActivate)
        {
          EraseHighlightRect();
          SetWindowPos(ghWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
          UpdateWindow(ghWnd);
        }
        if (g_optSetTopmost)
        {
          EraseHighlightRect();
          SetWindowPos(ghWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
          UpdateWindow(ghWnd);
        }
        if (g_optCloseWindow)
        {
          EraseHighlightRect();
          PostMessageA(ghWnd, WM_CLOSE, 0, 0);
        }
      }
    }
    return 0;
  }
  auto origProc = (LRESULT(__stdcall *)(HWND, UINT, WPARAM, LPARAM))GetWindowLongPtrA(hWnd, GWLP_USERDATA);
  return CallWindowProcA(origProc, hWnd, Msg, wParam, lParam);
}

void DrawBitmapPreview(HWND hwndDlg, int ctrlId, DRAWITEMSTRUCT *dis)
{
  HDC destDC = dis->hDC;
  if (ho)
  {
    HDC CompatibleDC = CreateCompatibleDC(destDC);
    SelectObject(CompatibleDC, ho);
    SelectPalette(destDC, (HPALETTE)hPal, 0);
    RealizePalette(destDC);
    StretchBlt(destDC, dis->rcItem.left, dis->rcItem.top,
               dis->rcItem.right - dis->rcItem.left, dis->rcItem.bottom - dis->rcItem.top,
               CompatibleDC, 0, 0, g_bitmapWidth, g_bitmapHeight, SRCCOPY);
    DeleteDC(CompatibleDC);
  }
}

int LoadBitmapResource(HGDIOBJ h, HGDIOBJ *hdc, HPALETTE *outPalette, DWORD *outWidth, DWORD *outHeight)
{
  *hdc = nullptr;
  *outPalette = nullptr;
  HANDLE ImageA = LoadImageA(hInst, MAKEINTRESOURCEA((WORD)(UINT_PTR)h), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_DEFAULTSIZE);
  *hdc = ImageA;
  if (!ImageA)
    return 0;
  BITMAP pv;
  GetObjectA(ImageA, sizeof(BITMAP), &pv);
  if (pv.bmBitsPixel * pv.bmPlanes > 8)
  {
    HDC DC = GetDC(nullptr);
    *outPalette = CreateHalftonePalette(DC);
    ReleaseDC(nullptr, DC);
  }
  else
  {
    HDC hdca = CreateCompatibleDC(nullptr);
    HGDIOBJ ha = SelectObject(hdca, *hdc);
    RGBQUAD prgbq[256];
    GetDIBColorTable(hdca, 0, 0x100u, prgbq);
    struct
    {
      LOGPALETTE hdr;
      PALETTEENTRY extra[255];
    } logPalBuf;
    LOGPALETTE *logPal = &logPalBuf.hdr;
    logPal->palVersion = 0x300 /* LOGPALETTE version */;
    logPal->palNumEntries = 256;
    for (unsigned i = 0; i < 256; i++)
    {
      logPal->palPalEntry[i].peRed = prgbq[i].rgbRed;
      logPal->palPalEntry[i].peGreen = prgbq[i].rgbGreen;
      logPal->palPalEntry[i].peBlue = prgbq[i].rgbBlue;
      logPal->palPalEntry[i].peFlags = 0;
    }
    *outPalette = CreatePalette(logPal);
    SelectObject(hdca, ha);
    DeleteDC(hdca);
  }
  *outWidth = pv.bmWidth;
  *outHeight = pv.bmHeight;
  return 1;
}

HFONT CreateCourierFont()
{
  LOGFONTA lf = {};
  lf.lfItalic = 0;
  lf.lfUnderline = 0;
  lf.lfStrikeOut = 0;
  lf.lfCharSet = 0;
  lf.lfHeight = -MulDiv(8, GetSystemDpi(), 72);
  lf.lfWeight = 400;
  lf.lfOutPrecision = 3;
  lf.lfClipPrecision = 2;
  lf.lfQuality = 1;
  lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
  strcpy(lf.lfFaceName, "Courier New");
  return CreateFontIndirectA(&lf);
}

HFONT CreateSansSerifFont()
{
  LOGFONTA lf = {};
  lf.lfItalic = 0;
  lf.lfUnderline = 0;
  lf.lfStrikeOut = 0;
  lf.lfCharSet = 0;
  lf.lfHeight = -MulDiv(8, GetSystemDpi(), 72);
  lf.lfWeight = 700;
  lf.lfOutPrecision = 1;
  lf.lfClipPrecision = 2;
  lf.lfQuality = 1;
  lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
  strcpy(lf.lfFaceName, "MS Sans Serif");
  return CreateFontIndirectA(&lf);
}

int InitResources()
{
  g_windowList.count = 0;
  hdc = CreateDCA(pwszDriver, nullptr, nullptr, nullptr);
  DWORD SysColor = GetSysColor(COLOR_BTNSHADOW);
  g_h = CreatePen(PS_DOT, 0, SysColor);
  DWORD btnFaceColor = GetSysColor(COLOR_BTNFACE);
  g_hBgBrush = CreateSolidBrush(btnFaceColor);
  g_hFontNormal = (HGDIOBJ)CreateCourierFont();
  g_hFontBold = (HGDIOBJ)CreateSansSerifFont();
  return LoadBitmapResource((HGDIOBJ)IDB_LOGO, (HGDIOBJ *)&ho, (HPALETTE *)&hPal, (DWORD *)&g_bitmapWidth, (DWORD *)&g_bitmapHeight);
}

BOOL PositionWindowBottomRight(HWND hWnd)
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
  typedef HRESULT(WINAPI * PFN)(HWND, LPCWSTR, LPCWSTR);
  static PFN pfn = (PFN)GetProcAddress(LoadLibraryA("uxtheme.dll"), "SetWindowTheme");
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
    else if (pfn)
      pfn(hwnd, dark ? L"DarkMode_Explorer" : L"", nullptr);
  }
  return TRUE;
}

static void ApplyDarkMode(HWND hDlg)
{
  g_darkMode = IsDarkModeActive();
  {
    typedef HRESULT(WINAPI * PFN)(HWND, DWORD, LPCVOID, DWORD);
    static PFN pfn = (PFN)GetProcAddress(LoadLibraryA("dwmapi.dll"), "DwmSetWindowAttribute");
    if (pfn)
    {
      BOOL dark = g_darkMode ? TRUE : FALSE;
      pfn(hDlg, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    }
  }
  EnumChildWindows(hDlg, ApplyThemeToChild, (LPARAM)g_darkMode);
  DeleteObject(g_hBgBrush);
  g_hBgBrush = CreateSolidBrush(g_darkMode ? RGB(30, 30, 30) : GetSysColor(COLOR_BTNFACE));
  InvalidateRect(hDlg, nullptr, TRUE);
}

LRESULT SetControlFont(HWND hDlg, int nIDDlgItem, WPARAM wParam)
{
  HWND DlgItem = GetDlgItem(hDlg, nIDDlgItem);
  return SendMessageA(DlgItem, WM_SETFONT, wParam, TRUE);
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
    HICON IconA = LoadIconA(hInst, MAKEINTRESOURCEA(IDI_APP));
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
    ghWnd = nullptr;
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
      CHAR String[256];
      int totalSize = 0;
      for (unsigned i = 0; i < 9; ++i)
      {
        GetDlgItemTextA(hDlg, g_labelControlIds[i], String, 256);
        unsigned labelLen = strlen(String) + 1;
        GetDlgItemTextA(hDlg, g_valueControlIds[i], String, 256);
        totalSize += labelLen + 1 + strlen(String) + 2;
      }
      OpenClipboard(hDlg);
      EmptyClipboard();
      HGLOBAL hClipMem = GlobalAlloc(GHND, totalSize + 1);
      unsigned nResulta = 0;
      char *pWrite = (char *)GlobalLock(hClipMem);
      do
      {
        GetDlgItemTextA(hDlg, g_labelControlIds[nResulta], String, 256);
        unsigned labelLen = strlen(String) + 1;
        memcpy(pWrite, String, 4 * ((labelLen - 1) >> 2));
        char *pWriteAligned = &pWrite[4 * ((labelLen - 1) >> 2)];
        char *pAfterLabel = &pWrite[labelLen - 1];
        memcpy(pWriteAligned, &String[4 * ((labelLen - 1) >> 2)], ((BYTE)labelLen - 1) & 3);
        *pAfterLabel++ = ':';
        *pAfterLabel++ = '\t';
        GetDlgItemTextA(hDlg, g_valueControlIds[nResulta], String, 256);
        unsigned valueLen = strlen(String) + 1;
        memcpy(pAfterLabel, String, 4 * ((valueLen - 1) >> 2));
        char *pValueAligned = &pAfterLabel[4 * ((valueLen - 1) >> 2)];
        ++nResulta;
        char *pAfterValue = &pAfterLabel[valueLen - 1];
        memcpy(pValueAligned, &String[4 * ((valueLen - 1) >> 2)], ((BYTE)valueLen - 1) & 3);
        *pAfterValue++ = '\r';
        *pAfterValue = '\n';
        pWrite = pAfterValue + 1;
      } while (nResulta < 9);
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

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
  TryEnableDpiAwareness();
  hInst = hInstance;
  DialogBoxParamA(hInstance, MAKEINTRESOURCEA(IDD_MAIN), nullptr, (DLGPROC)DialogFunc, 0);
  return 0;
}
