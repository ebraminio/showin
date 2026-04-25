#include <windows.h>
#include "resource.h"

struct WindowEntry
{
  HWND hwnd;
  int area;
};

typedef int (*CmpFn)(WindowEntry *, WindowEntry *);

struct WindowList
{
  WindowEntry *buf;
  int capacity;
  int count;
  CmpFn cmp;

  WindowList(int capacity) : buf(new WindowEntry[capacity]), capacity(capacity), count(0), cmp(nullptr)
  {
  }

  ~WindowList()
  {
    delete[] this->buf;
  }

  void Push(HWND hwnd, int area)
  {
    if (count >= capacity)
    {
      int newcap = capacity + capacity / 2;
      capacity = newcap;
      buf = (WindowEntry *)realloc(buf, newcap * sizeof(WindowEntry));
    }
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

  void Clear()
  {
    count = 0;
  }

  void Sort(CmpFn fn)
  {
    cmp = fn;
    Quicksort(0, count - 1);
  }

  void Quicksort(int lo, int hi)
  {
    if (hi <= lo)
      return;

    int result = hi;
    int a2 = lo;
    while (1)
    {
      int v18 = result;
      int v6 = a2 - 1;
      while (1)
      {
        do
          ++v6;
        while (cmp(&buf[v6], &buf[hi]) < 0);
        do
        {
          if (v18 <= 0)
            break;
          --v18;
        } while (cmp(&buf[v18], &buf[hi]) > 0);
        if (v6 >= v18)
          break;
        WindowEntry tmp = buf[v6];
        buf[v6] = buf[v18];
        buf[v18] = tmp;
      }
      WindowEntry tmp = buf[v6];
      buf[v6] = buf[hi];
      buf[hi] = tmp;
      Quicksort(a2, v6 - 1);
      result = v6 + 1;
      a2 = v6 + 1;
      if (hi <= v6 + 1)
        break;
      result = hi;
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
WindowList *g_windowList = nullptr;

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

void DrawHighlightRect(RECT *a1)
{
  if (!g_showHighlight)
    return;
  HGDIOBJ h = SelectObject(hdc, g_h);
  int rop2 = SetROP2(hdc, R2_XORPEN);
  MoveToEx(hdc, a1->left - 1, a1->top - 1, nullptr);
  LineTo(hdc, a1->right, a1->top - 1);
  LineTo(hdc, a1->right, a1->bottom);
  LineTo(hdc, a1->left - 1, a1->bottom);
  LineTo(hdc, a1->left - 1, a1->top - 1);
  MoveToEx(hdc, a1->left - 2, a1->top - 2, nullptr);
  LineTo(hdc, a1->right + 1, a1->top - 2);
  LineTo(hdc, a1->right + 1, a1->bottom + 1);
  LineTo(hdc, a1->left - 2, a1->bottom + 1);
  LineTo(hdc, a1->left - 2, a1->top - 2);
  SetROP2(hdc, rop2);
  SelectObject(hdc, h);
  SetRect(&rc, a1->left, a1->top, a1->right, a1->bottom);
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
  if (g_windowList)
  {
    delete g_windowList;
    g_windowList = nullptr;
  }
  EraseHighlightRect();
  DeleteObject(g_hBgBrush);
  DeleteObject(g_h);
  return DeleteDC(hdc);
}

HWND HitTestWindowList()
{
  struct tagRECT Rect;

  for (auto &e : *g_windowList)
  {
    GetWindowRect(e.hwnd, &Rect);
    if (PtInRect(&Rect, pt))
      return e.hwnd;
  }
  return nullptr;
}

int __cdecl CompareByArea(WindowEntry *a1, WindowEntry *a2)
{
  return a1->area - a2->area;
}

BOOL CALLBACK EnumFunc(HWND hWnd, LPARAM a2)
{
  if (g_optIncludeHidden || IsWindowVisible(hWnd))
  {
    struct tagRECT Rect;
    GetWindowRect(hWnd, &Rect);
    if (!IsRectEmpty(&Rect))
      g_windowList->Push(hWnd, (Rect.right - Rect.left) * (Rect.bottom - Rect.top));
    EnumChildWindows(hWnd, EnumFunc, 0);
  }
  return 1;
}

void RebuildWindowList()
{
  g_windowList->Clear();
  EnumWindows(EnumFunc, 0);
  g_windowList->Sort(CompareByArea);
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
  HWND v11 = HitTestWindowList();
  ghWnd = v11;
  if (v11 && v11 != g_lastHoveredHwnd)
  {
    g_lastHoveredHwnd = v11;
    SendMessageA(v11, WM_GETTEXT, 256, (LPARAM)String);
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
    LONG_PTR v14 = GetWindowLongPtrA(ghWnd, GWLP_WNDPROC);
    wsprintfA(String, "0x%IX", (SIZE_T)v14);
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
      HCURSOR v8 = LoadCursorA(nullptr, IDC_ARROW);
      SetCursor(v8);
      HWND v9 = GetParent(hWnd);
      SetForegroundWindow(v9);
    }
    return 0;
  case WM_RBUTTONDOWN:
    if (g_isDragging)
    {
      HWND v4 = GetParent(hWnd);
      if (ghWnd != v4 && GetParent(ghWnd) != v4)
      {
        if (g_optToggleEnabled)
        {
          EraseHighlightRect();
          BOOL v5 = IsWindowEnabled(ghWnd);
          EnableWindow(ghWnd, !v5);
        }
        if (g_optToggleVisible)
        {
          EraseHighlightRect();
          /* Toggle visibility: SW_SHOW(5) if hidden, SW_HIDE(0) if visible */
          int v6 = -IsWindowVisible(ghWnd);
          v6 &= ~4; /* clear bit 2 of low byte */
          ShowWindow(ghWnd, v6 + 5);
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
  auto v15 = (LRESULT(__stdcall *)(HWND, UINT, WPARAM, LPARAM))GetWindowLongPtrA(hWnd, GWLP_USERDATA);
  return CallWindowProcA(v15, hWnd, Msg, wParam, lParam);
}

void DrawBitmapPreview(HWND hwndDlg, int a2, DRAWITEMSTRUCT *a3)
{
  HDC v3 = a3->hDC;
  if (ho)
  {
    HDC CompatibleDC = CreateCompatibleDC(v3);
    SelectObject(CompatibleDC, ho);
    SelectPalette(v3, (HPALETTE)hPal, 0);
    RealizePalette(v3);
    StretchBlt(v3, a3->rcItem.left, a3->rcItem.top,
               a3->rcItem.right - a3->rcItem.left, a3->rcItem.bottom - a3->rcItem.top,
               CompatibleDC, 0, 0, g_bitmapWidth, g_bitmapHeight, SRCCOPY);
    DeleteDC(CompatibleDC);
  }
}

int LoadBitmapResource(HGDIOBJ h, HGDIOBJ *hdc, HPALETTE *a3, DWORD *a4, DWORD *a5)
{
  *hdc = nullptr;
  *a3 = nullptr;
  HANDLE ImageA = LoadImageA(hInst, MAKEINTRESOURCEA((WORD)(UINT_PTR)h), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_DEFAULTSIZE);
  *hdc = ImageA;
  if (!ImageA)
    return 0;
  BITMAP pv;
  GetObjectA(ImageA, sizeof(BITMAP), &pv);
  if (pv.bmBitsPixel * pv.bmPlanes > 8)
  {
    HDC DC = GetDC(nullptr);
    *a3 = CreateHalftonePalette(DC);
    ReleaseDC(nullptr, DC);
  }
  else
  {
    HDC hdca = CreateCompatibleDC(nullptr);
    HGDIOBJ ha = SelectObject(hdca, *hdc);
    int v8 = 256;
    RGBQUAD prgbq[256];
    GetDIBColorTable(hdca, 0, 0x100u, prgbq);
    LOGPALETTE *v9 = (LOGPALETTE *)malloc(sizeof(LOGPALETTE) + 256 * sizeof(PALETTEENTRY));
    BYTE *p_rgbGreen = &prgbq[0].rgbGreen;
    v9->palVersion = 0x300 /* LOGPALETTE version */;
    v9->palNumEntries = 256;
    BYTE *p_peGreen = &v9->palPalEntry[0].peGreen;
    do
    {
      *(p_peGreen - 1) = p_rgbGreen[1];
      *p_peGreen = *p_rgbGreen;
      p_peGreen[1] = *(p_rgbGreen - 1);
      p_peGreen[2] = 0;
      p_rgbGreen += 4;
      p_peGreen += 4;
      --v8;
    } while (v8);
    *a3 = CreatePalette(v9);
    free(v9);
    SelectObject(hdca, ha);
    DeleteDC(hdca);
  }
  *a4 = pv.bmWidth;
  *a5 = pv.bmHeight;
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
  g_windowList = new WindowList(1000);
  hdc = CreateDCA(pwszDriver, nullptr, nullptr, nullptr);
  DWORD SysColor = GetSysColor(COLOR_BTNSHADOW);
  g_h = CreatePen(PS_DOT, 0, SysColor);
  DWORD v1 = GetSysColor(COLOR_BTNFACE);
  g_hBgBrush = CreateSolidBrush(v1);
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
    HWND hDlga = GetDlgItem(hDlg, IDC_DRAG_BTN);
    HICON IconA = LoadIconA(hInst, MAKEINTRESOURCEA(IDI_APP));
    SendMessageA(hDlga, BM_SETIMAGE, IMAGE_ICON, (LPARAM)IconA);
    LONG_PTR v21 = SetWindowLongPtrA(hDlga, GWLP_WNDPROC, (LONG_PTR)CrosshairWndProc);
    SetWindowLongPtrA(hDlga, GWLP_USERDATA, v21);
    SetControlFont(hDlg, IDC_TITLE, (WPARAM)g_hFontBold);
    SetControlFont(hDlg, IDC_HANDLE, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, IDC_PARENT, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, IDC_OWNER, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, IDC_WINDOWID, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, IDC_CLIENT_COORDS, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, IDC_WINDOW_COORDS, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, IDC_WNDPROC, (WPARAM)g_hFontNormal);
    HWND v22 = GetDlgItem(hDlg, IDC_OPT_HIGHLIGHT);
    SendMessageA(v22, BM_SETCHECK, BST_CHECKED, 0);
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
      int v7 = 0;
      for (unsigned int i = 0; i < 9; ++i)
      {
        GetDlgItemTextA(hDlg, g_labelControlIds[i], String, 256);
        unsigned int v9 = strlen(String) + 1;
        GetDlgItemTextA(hDlg, g_valueControlIds[i], String, 256);
        v7 += v9 + 1 + strlen(String) + 2;
      }
      OpenClipboard(hDlg);
      EmptyClipboard();
      HGLOBAL v26 = GlobalAlloc(GHND, v7 + 1);
      unsigned int nResulta = 0;
      char *v10 = (char *)GlobalLock(v26);
      do
      {
        GetDlgItemTextA(hDlg, g_labelControlIds[nResulta], String, 256);
        unsigned int v11 = strlen(String) + 1;
        memcpy(v10, String, 4 * ((v11 - 1) >> 2));
        char *v12 = &v10[4 * ((v11 - 1) >> 2)];
        char *v13 = &v10[v11 - 1];
        memcpy(v12, &String[4 * ((v11 - 1) >> 2)], ((BYTE)v11 - 1) & 3);
        *v13++ = ':';
        *v13++ = '\t';
        GetDlgItemTextA(hDlg, g_valueControlIds[nResulta], String, 256);
        unsigned int v14 = strlen(String) + 1;
        memcpy(v13, String, 4 * ((v14 - 1) >> 2));
        char *v15 = &v13[4 * ((v14 - 1) >> 2)];
        ++nResulta;
        char *v16 = &v13[v14 - 1];
        memcpy(v15, &String[4 * ((v14 - 1) >> 2)], ((BYTE)v14 - 1) & 3);
        *v16++ = '\r';
        *v16 = '\n';
        v10 = v16 + 1;
      } while (nResulta < 9);
      GlobalUnlock(v26);
      SetClipboardData(CF_TEXT, v26);
      CloseClipboard();
      break;
    }
    case IDC_OPT_ONTOP:
    {
      LRESULT v17 = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0);
      g_optSetNoActivate = v17 == 1;
      if (v17 == 1 && g_optSetTopmost)
        SendDlgItemMessageA(hDlg, IDC_OPT_NOTONTOP, BM_SETCHECK, BST_UNCHECKED, 0);
      break;
    }
    case IDC_OPT_NOTONTOP:
    {
      LRESULT v18 = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0);
      g_optSetTopmost = v18 == 1;
      if (v18 == 1 && g_optSetNoActivate)
        SendDlgItemMessageA(hDlg, IDC_OPT_ONTOP, BM_SETCHECK, BST_UNCHECKED, 0);
      break;
    }
    default:
      return 1;
    }
    break;
  case WM_CTLCOLORSTATIC:
  {
    LONG WindowLongA = GetWindowLongA((HWND)lParam, GWL_ID);
    if (WindowLongA == IDC_CLASSNAME || WindowLongA == IDC_HANDLE || WindowLongA == IDC_PARENT || WindowLongA == IDC_OWNER || WindowLongA == IDC_WINDOWID || WindowLongA == IDC_CLIENT_COORDS || WindowLongA == IDC_WINDOW_COORDS || WindowLongA == IDC_WNDPROC)
    {
      SetTextColor((HDC)wParam, RGB(0, 0, 0xC0));
      DWORD SysColor = GetSysColor(COLOR_BTNFACE);
      SetBkColor((HDC)wParam, SysColor);
      SetWindowLongPtrA(hDlg, DWLP_MSGRESULT, (LONG_PTR)g_hBgBrush);
      return TRUE;
    }
    break;
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
