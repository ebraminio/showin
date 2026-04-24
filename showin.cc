#include <windows.h>

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
  int cursor;
  int count;
  CmpFn cmp;
};

int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd);
int InitResources();
BOOL CleanupResources();
void __cdecl DrawHighlightRect(RECT *a1);
BOOL EraseHighlightRect();
BOOL CALLBACK DialogFunc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT __stdcall CrosshairWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
void __cdecl DrawBitmapPreview(HWND hwndDlg, int a2, DRAWITEMSTRUCT *a3);
int __cdecl LoadBitmapResource(HGDIOBJ h, HGDIOBJ *hdc, HPALETTE *a3, DWORD *a4, DWORD *a5);
BOOL __cdecl PositionWindowBottomRight(HWND hWnd);
HFONT CreateCourierFont();
HFONT CreateSansSerifFont();
LRESULT __cdecl SetControlFont(HWND hDlg, int nIDDlgItem, WPARAM wParam);
HWND HitTestWindowList();
void RebuildWindowList();
BOOL __stdcall EnumFunc(HWND hWnd, LPARAM a2);
int __cdecl CompareByArea(WindowEntry *a1, WindowEntry *a2);
static WindowList *WindowList_Init(WindowList *self, int capacity);
static void WindowList_Push(WindowList *self, HWND hwnd, int area);
static WindowEntry *WindowList_Get(WindowList *self, WindowEntry *out, int idx);
static WindowEntry *WindowList_Next(WindowList *self, WindowEntry *out);
static int WindowList_Seek(WindowList *self, int idx);
static int WindowList_GetPos(WindowList *self);
static int WindowList_Count(WindowList *self);
static void WindowList_Clear(WindowList *self);
static void WindowList_Sort(WindowList *self, CmpFn cmp);
static void WindowList_Quicksort(WindowList *self, int lo, int hi);

//-------------------------------------------------------------------------
// Data declarations

/* Label static controls (left column) and their paired value controls (right column)
   used by the "Copy to clipboard" function (button 1031). */
int g_labelControlIds[] = {1040, 1032, 1033, 1034, 1035, 1036, 1037, 1038, 1039};
int g_valueControlIds[] = {1002, 1004, 1003, 1017, 1025, 1023, 1007, 1005, 1006};
CHAR pwszDriver[] = "DISPLAY";
int g_optCloseWindow = 0;
HWND g_lastHoveredHwnd = NULL;
int g_bitmapHeight = 0;
int g_isDragging = 0;
int g_optIncludeHidden = 0;
int g_optToggleEnabled = 0;
HGDIOBJ g_hFontBold = NULL;
HINSTANCE hInst = NULL;
HGDIOBJ g_h = NULL;
int g_optSetTopmost = 0;
HGDIOBJ g_hFontNormal = NULL;
int g_optRedrawWindow = 0;
HGDIOBJ g_hBgBrush = NULL;
HWND ghWnd = NULL;
RECT rc = {0, 0, 0, 0};
POINT pt = {0, 0};
int g_bitmapWidth = 0;
HGDIOBJ hPal = NULL;
HDC hdc = NULL;
int g_optSetNoActivate = 0;
int g_optToggleVisible = 0;
int g_showHighlight = 0;
HGDIOBJ ho = NULL;
WindowList *g_windowList = NULL;

int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
  hInst = hInstance;
  DialogBoxParamA(hInstance, MAKEINTRESOURCEA(101), NULL, (DLGPROC)DialogFunc, 0);
  return 0;
}

int InitResources()
{
  DWORD SysColor; // eax
  DWORD v1;       // eax

  {
    /* Allocate WindowList object */
    WindowList *_new = (WindowList *)malloc(sizeof(WindowList));
    if (_new)
      g_windowList = WindowList_Init(_new, 1000);
    else
      g_windowList = NULL;
  }
  hdc = CreateDCA(pwszDriver, NULL, NULL, NULL);
  SysColor = GetSysColor(COLOR_BTNSHADOW);
  g_h = CreatePen(PS_DOT, 0, SysColor);
  v1 = GetSysColor(COLOR_BTNFACE);
  g_hBgBrush = CreateSolidBrush(v1);
  g_hFontNormal = (HGDIOBJ)CreateCourierFont();
  g_hFontBold = (HGDIOBJ)CreateSansSerifFont();
  return LoadBitmapResource((HGDIOBJ)102, (HGDIOBJ *)&ho, (HPALETTE *)&hPal, (DWORD *)&g_bitmapWidth, (DWORD *)&g_bitmapHeight);
}

BOOL CleanupResources()
{
  DeleteObject(ho);
  ho = NULL;
  DeleteObject(hPal);
  hPal = NULL;
  DeleteObject(g_hFontNormal);
  DeleteObject(g_hFontBold);
  if (g_windowList)
  {
    free(g_windowList->buf); /* free the entry buffer */
    free(g_windowList);      /* free the object itself */
    g_windowList = NULL;
  }
  EraseHighlightRect();
  DeleteObject(g_hBgBrush);
  DeleteObject(g_h);
  return DeleteDC(hdc);
}

void __cdecl DrawHighlightRect(RECT *a1)
{
  int rop2;  // [esp+0h] [ebp-8h]
  HGDIOBJ h; // [esp+4h] [ebp-4h]

  if (g_showHighlight)
  {
    h = SelectObject(hdc, g_h);
    rop2 = SetROP2(hdc, R2_XORPEN);
    MoveToEx(hdc, a1->left - 1, a1->top - 1, NULL);
    LineTo(hdc, a1->right, a1->top - 1);
    LineTo(hdc, a1->right, a1->bottom);
    LineTo(hdc, a1->left - 1, a1->bottom);
    LineTo(hdc, a1->left - 1, a1->top - 1);
    MoveToEx(hdc, a1->left - 2, a1->top - 2, NULL);
    LineTo(hdc, a1->right + 1, a1->top - 2);
    LineTo(hdc, a1->right + 1, a1->bottom + 1);
    LineTo(hdc, a1->left - 2, a1->bottom + 1);
    LineTo(hdc, a1->left - 2, a1->top - 2);
    SetROP2(hdc, rop2);
    SelectObject(hdc, h);
    SetRect(&rc, a1->left, a1->top, a1->right, a1->bottom);
  }
}

BOOL EraseHighlightRect()
{
  if (!IsRectEmpty(&rc))
    DrawHighlightRect(&rc);
  return SetRect(&rc, 0, 0, 0, 0);
}

BOOL CALLBACK DialogFunc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  LONG WindowLongA;      // eax
  DWORD SysColor;        // eax
  int v7;                // ebx
  unsigned int i;        // esi
  unsigned int v9;       // kr04_4
  char *v10;             // ebx
  unsigned int v11;      // kr0C_4
  char *v12;             // edi
  char *v13;             // ebx
  unsigned int v14;      // kr10_4
  char *v15;             // edi
  char *v16;             // ebx
  LRESULT v17;           // eax
  LRESULT v18;           // eax
  HICON IconA;           // eax
  LONG_PTR v21;          // eax
  HWND v22;              // eax
  HWND DlgItem;          // eax
  CHAR String[256];      // [esp+10h] [ebp-100h] BYREF
  HWND hDlga;            // [esp+118h] [ebp+8h]
  HGLOBAL v26;           // [esp+120h] [ebp+10h]
  unsigned int nResulta; // [esp+124h] [ebp+14h]

  switch (uMsg)
  {
  case WM_ACTIVATE:
    if (!(WORD)wParam)
    {
      DlgItem = GetDlgItem(hDlg, 1016);
      SendMessageA(DlgItem, WM_LBUTTONUP, 0, 0);
    }
    break;
  case WM_CLOSE:
    EraseHighlightRect();
    CleanupResources();
    EndDialog(hDlg, lParam);
    break;
  case WM_DRAWITEM:
    if (wParam == 1001)
      DrawBitmapPreview(hDlg, 1001, (DRAWITEMSTRUCT *)lParam);
    break;
  case WM_INITDIALOG:
    InitResources();
    SetWindowTextA(hDlg, "ShoWin 2.00");
    PositionWindowBottomRight(hDlg);
    hDlga = GetDlgItem(hDlg, 1016);
    IconA = LoadIconA(hInst, MAKEINTRESOURCEA(104));
    SendMessageA(hDlga, BM_SETIMAGE, IMAGE_ICON, (LPARAM)IconA);
    v21 = SetWindowLongPtrA(hDlga, GWLP_WNDPROC, (LONG_PTR)CrosshairWndProc);
    SetWindowLongPtrA(hDlga, GWLP_USERDATA, v21);
    SetControlFont(hDlg, 1002, (WPARAM)g_hFontBold);
    SetControlFont(hDlg, 1003, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, 1017, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, 1025, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, 1023, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, 1005, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, 1006, (WPARAM)g_hFontNormal);
    SetControlFont(hDlg, 1007, (WPARAM)g_hFontNormal);
    v22 = GetDlgItem(hDlg, 1022);
    SendMessageA(v22, BM_SETCHECK, BST_CHECKED, 0);
    g_showHighlight = 1;
    g_lastHoveredHwnd = 0;
    ghWnd = NULL;
    g_isDragging = 0;
    break;
  case WM_COMMAND:
    switch (LOWORD(wParam))
    {
    case 1020:
      g_optToggleVisible = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case 1021:
      g_optToggleEnabled = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case 1022:
      g_showHighlight = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case 1026:
      g_optRedrawWindow = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case 1028:
      g_optIncludeHidden = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case 1030:
      g_optCloseWindow = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0) == 1;
      break;
    case 1031:
      v7 = 0;
      for (i = 0; i < 9; ++i)
      {
        GetDlgItemTextA(hDlg, g_labelControlIds[i], String, 256);
        v9 = strlen(String) + 1;
        GetDlgItemTextA(hDlg, g_valueControlIds[i], String, 256);
        v7 += v9 + 1 + strlen(String) + 2;
      }
      OpenClipboard(hDlg);
      EmptyClipboard();
      v26 = GlobalAlloc(GHND, v7 + 1);
      nResulta = 0;
      v10 = (char *)GlobalLock(v26);
      do
      {
        GetDlgItemTextA(hDlg, g_labelControlIds[nResulta], String, 256);
        v11 = strlen(String) + 1;
        memcpy(v10, String, 4 * ((v11 - 1) >> 2));
        v12 = &v10[4 * ((v11 - 1) >> 2)];
        v13 = &v10[v11 - 1];
        memcpy(v12, &String[4 * ((v11 - 1) >> 2)], ((BYTE)v11 - 1) & 3);
        *v13++ = ':';
        *v13++ = '\t';
        GetDlgItemTextA(hDlg, g_valueControlIds[nResulta], String, 256);
        v14 = strlen(String) + 1;
        memcpy(v13, String, 4 * ((v14 - 1) >> 2));
        v15 = &v13[4 * ((v14 - 1) >> 2)];
        ++nResulta;
        v16 = &v13[v14 - 1];
        memcpy(v15, &String[4 * ((v14 - 1) >> 2)], ((BYTE)v14 - 1) & 3);
        *v16++ = '\r';
        *v16 = '\n';
        v10 = v16 + 1;
      } while (nResulta < 9);
      GlobalUnlock(v26);
      SetClipboardData(CF_TEXT, v26);
      CloseClipboard();
      break;
    case 1045:
      v17 = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0);
      g_optSetNoActivate = v17 == 1;
      if (v17 == 1 && g_optSetTopmost)
        SendDlgItemMessageA(hDlg, 1046, BM_SETCHECK, BST_UNCHECKED, 0);
      break;
    case 1046:
      v18 = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0);
      g_optSetTopmost = v18 == 1;
      if (v18 == 1 && g_optSetNoActivate)
        SendDlgItemMessageA(hDlg, 1045, BM_SETCHECK, BST_UNCHECKED, 0);
      break;
    default:
      return 1;
    }
    break;
  case WM_CTLCOLORSTATIC:
    WindowLongA = GetWindowLongA((HWND)lParam, GWL_ID);
    if (WindowLongA == 1004 || WindowLongA == 1003 || WindowLongA == 1017 || WindowLongA == 1025 || WindowLongA == 1023 || WindowLongA == 1005 || WindowLongA == 1006 || WindowLongA == 1007)
    {
      SetTextColor((HDC)wParam, RGB(0, 0, 0xC0));
      SysColor = GetSysColor(COLOR_BTNFACE);
      SetBkColor((HDC)wParam, SysColor);
      SetWindowLongPtrA(hDlg, DWLP_MSGRESULT, (LONG_PTR)g_hBgBrush);
      return TRUE;
    }
    break;
  default:
    return 0;
  }
  return 1;
}

LRESULT __stdcall CrosshairWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
  HWND v4;                                              // ebx
  BOOL v5;                                              // eax
  int v6;                                               // eax
  HCURSOR v8;                                           // eax
  HWND v9;                                              // eax
  HCURSOR CursorA;                                      // eax
  HWND v11;                                             // eax
  HWND Window;                                          // eax
  LONG WindowLongA;                                     // eax
  LONG_PTR v14;                                        // eax
  LRESULT(__stdcall * v15)(HWND, UINT, WPARAM, LPARAM); // eax
  CHAR String[256];                                     // [esp+Ch] [ebp-128h] BYREF
  struct tagRECT rcDst;                                 // [esp+10Ch] [ebp-28h] BYREF
  struct tagRECT Rect;                                  // [esp+11Ch] [ebp-18h] BYREF
  HWND Parent;                                          // [esp+12Ch] [ebp-8h]
  HWND hDlg;                                            // [esp+130h] [ebp-4h]

  switch (Msg)
  {
  case WM_MOUSEMOVE:
    if (!g_isDragging)
      break;
  LABEL_26:
    pt.x = (__int16)lParam;
    pt.y = (short)HIWORD(lParam);
    ClientToScreen(hWnd, &pt);
    hDlg = GetParent(hWnd);
    wsprintfA(String, "%4hd", pt.x);
    SetDlgItemTextA(hDlg, 1011, String);
    wsprintfA(String, "%4hd", pt.y);
    SetDlgItemTextA(hDlg, 1012, String);
    v11 = (HWND)HitTestWindowList();
    ghWnd = v11;
    if (v11 && v11 != g_lastHoveredHwnd)
    {
      g_lastHoveredHwnd = v11;
      SendMessageA(v11, WM_GETTEXT, 256, (LPARAM)String);
      SetDlgItemTextA(hDlg, 1002, String);
      GetClassNameA(ghWnd, String, 256);
      SetDlgItemTextA(hDlg, 1004, String);
      wsprintfA(String, "%-6d (0x%08X)", ghWnd, ghWnd);
      SetDlgItemTextA(hDlg, 1003, String);
      Parent = GetParent(ghWnd);
      wsprintfA(String, "%-6d (0x%08X)", Parent, Parent);
      SetDlgItemTextA(hDlg, 1017, String);
      Window = GetWindow(ghWnd, GW_OWNER);
      wsprintfA(String, "%-6d (0x%08X)", Window, Window);
      SetDlgItemTextA(hDlg, 1025, String);
      WindowLongA = GetWindowLongA(ghWnd, GWL_ID);
      wsprintfA(String, "%-6d (0x%08X)", WindowLongA, WindowLongA);
      SetDlgItemTextA(hDlg, 1023, String);
      v14 = GetWindowLongPtrA(ghWnd, GWLP_WNDPROC);
      wsprintfA(String, "0x%IX", (SIZE_T)v14);
      SetDlgItemTextA(hDlg, 1007, String);
      GetWindowRect(ghWnd, &Rect);
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
        SetDlgItemTextA(hDlg, 1005, String);
      }
      wsprintfA(
          String,
          "x:%4d y:%4d  w:%4d h:%4d",
          rcDst.left,
          rcDst.top,
          rcDst.right - rcDst.left,
          rcDst.bottom - rcDst.top);
      SetDlgItemTextA(hDlg, 1006, String);
      EraseHighlightRect();
      DrawHighlightRect(&rcDst);
    }
    break;
  case WM_LBUTTONDOWN:
    if (g_isDragging)
      return 0;
    RebuildWindowList();
    g_lastHoveredHwnd = 0;
    SetRect(&rc, 0, 0, 0, 0);
    SetCapture(hWnd);
    CursorA = LoadCursorA(hInst, MAKEINTRESOURCEA(103));
    SetCursor(CursorA);
    g_isDragging = 1;
    goto LABEL_26;
  case WM_LBUTTONUP:
    if (g_isDragging)
    {
      g_isDragging = 0;
      EraseHighlightRect();
      ReleaseCapture();
      v8 = LoadCursorA(NULL, IDC_ARROW);
      SetCursor(v8);
      v9 = GetParent(hWnd);
      SetForegroundWindow(v9);
    }
    return 0;
  case WM_RBUTTONDOWN:
    if (g_isDragging)
    {
      v4 = GetParent(hWnd);
      if (ghWnd != v4 && GetParent(ghWnd) != v4)
      {
        if (g_optToggleEnabled)
        {
          EraseHighlightRect();
          v5 = IsWindowEnabled(ghWnd);
          EnableWindow(ghWnd, !v5);
        }
        if (g_optToggleVisible)
        {
          EraseHighlightRect();
          /* Toggle visibility: SW_SHOW(5) if hidden, SW_HIDE(0) if visible */
          v6 = -IsWindowVisible(ghWnd);
          v6 &= ~4; /* clear bit 2 of low byte */
          ShowWindow(ghWnd, v6 + 5);
        }
        if (g_optRedrawWindow)
        {
          EraseHighlightRect();
          InvalidateRect(ghWnd, NULL, TRUE);
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
  v15 = (LRESULT(__stdcall *)(HWND, UINT, WPARAM, LPARAM))GetWindowLongPtrA(hWnd, GWLP_USERDATA);
  return CallWindowProcA(v15, hWnd, Msg, wParam, lParam);
}

void __cdecl DrawBitmapPreview(HWND hwndDlg, int a2, DRAWITEMSTRUCT *a3)
{
  HDC v3;           // edi
  HDC CompatibleDC; // ebx

  v3 = a3->hDC;
  if (ho)
  {
    CompatibleDC = CreateCompatibleDC(v3);
    SelectObject(CompatibleDC, ho);
    SelectPalette(v3, (HPALETTE)hPal, 0);
    RealizePalette(v3);
    StretchBlt(v3, a3->rcItem.left, a3->rcItem.top,
               a3->rcItem.right - a3->rcItem.left, a3->rcItem.bottom - a3->rcItem.top,
               CompatibleDC, 0, 0, g_bitmapWidth, g_bitmapHeight, SRCCOPY);
    DeleteDC(CompatibleDC);
  }
}

int __cdecl LoadBitmapResource(HGDIOBJ h, HGDIOBJ *hdc, HPALETTE *a3, DWORD *a4, DWORD *a5)
{
  HANDLE ImageA;      // eax
  int v8;             // edi
  LOGPALETTE *v9;     // esi
  BYTE *p_rgbGreen;   // ecx
  BYTE *p_peGreen;    // eax
  HDC DC;             // edi
  RGBQUAD prgbq[256]; // [esp+Ch] [ebp-418h] BYREF -- GetDIBColorTable writes 256 RGBQUAD entries = 1024 bytes
  BITMAP pv;          // [esp+40Ch] [ebp-18h] BYREF -- GetObjectA fills 24 bytes (BITMAP struct)
  HGDIOBJ ha;         // [esp+42Ch] [ebp+8h]
  HDC hdca;           // [esp+430h] [ebp+Ch]

  *hdc = NULL;
  *a3 = NULL;
  ImageA = LoadImageA(hInst, MAKEINTRESOURCEA((WORD)(UINT_PTR)h), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_DEFAULTSIZE);
  *hdc = ImageA;
  if (!ImageA)
    return 0;
  GetObjectA(ImageA, sizeof(BITMAP), &pv);
  if (pv.bmBitsPixel * pv.bmPlanes > 8)
  {
    DC = GetDC(NULL);
    *a3 = CreateHalftonePalette(DC);
    ReleaseDC(NULL, DC);
  }
  else
  {
    hdca = CreateCompatibleDC(NULL);
    ha = SelectObject(hdca, *hdc);
    v8 = 256;
    GetDIBColorTable(hdca, 0, 0x100u, prgbq);
    v9 = (LOGPALETTE *)malloc(sizeof(LOGPALETTE) + 256 * sizeof(PALETTEENTRY));
    p_rgbGreen = &prgbq[0].rgbGreen;
    v9->palVersion = 0x300 /* LOGPALETTE version */;
    v9->palNumEntries = 256;
    p_peGreen = &v9->palPalEntry[0].peGreen;
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

BOOL __cdecl PositionWindowBottomRight(HWND hWnd)
{
  RECT pvParam; // [esp+4h] [ebp-20h] BYREF -- SPI_GETWORKAREA fills a full RECT (16 bytes)
  // v3 (right) and v4 (bottom) were stack spill from SPI filling beyond pvParam[8]
  struct tagRECT Rect; // [esp+14h] [ebp-10h] BYREF

  SystemParametersInfoA(SPI_GETWORKAREA, 0, &pvParam, 0);
  GetWindowRect(hWnd, &Rect);
  return SetWindowPos(hWnd, NULL, pvParam.right + Rect.left - Rect.right, pvParam.bottom + Rect.top - Rect.bottom, 0, 0, SWP_NOSIZE);
}

HFONT CreateCourierFont()
{
  LOGFONTA lf; // [esp+8h] [ebp-3Ch] BYREF

  memset(&lf.lfWidth, 0, 12);
  lf.lfItalic = 0;
  lf.lfUnderline = 0;
  lf.lfStrikeOut = 0;
  lf.lfCharSet = 0;
  lf.lfHeight = -11;
  lf.lfWeight = 400;
  lf.lfOutPrecision = 3;
  lf.lfClipPrecision = 2;
  lf.lfQuality = 1;
  strcpy((char *)&lf.lfPitchAndFamily, "1Courier New");
  return CreateFontIndirectA(&lf);
}

HFONT CreateSansSerifFont()
{
  LOGFONTA lf; // [esp+8h] [ebp-3Ch] BYREF

  memset(&lf.lfWidth, 0, 12);
  lf.lfItalic = 0;
  lf.lfUnderline = 0;
  lf.lfStrikeOut = 0;
  lf.lfCharSet = 0;
  lf.lfHeight = -11;
  lf.lfWeight = 700;
  lf.lfOutPrecision = 1;
  lf.lfClipPrecision = 2;
  lf.lfQuality = 1;
  strcpy((char *)&lf.lfPitchAndFamily, "\"MS Sans Serif");
  return CreateFontIndirectA(&lf);
}

LRESULT __cdecl SetControlFont(HWND hDlg, int nIDDlgItem, WPARAM wParam)
{
  HWND DlgItem; // eax

  DlgItem = GetDlgItem(hDlg, nIDDlgItem);
  return SendMessageA(DlgItem, WM_SETFONT, wParam, TRUE);
}

HWND HitTestWindowList()
{
  struct tagRECT Rect;
  WindowEntry e;

  for (WindowList_Get(g_windowList, &e, 0); e.hwnd; WindowList_Next(g_windowList, &e))
  {
    GetWindowRect(e.hwnd, &Rect);
    if (PtInRect(&Rect, pt))
      return e.hwnd;
  }
  return NULL;
}

void RebuildWindowList()
{
  WindowList_Clear(g_windowList);
  EnumWindows(EnumFunc, 0);
  WindowList_Sort(g_windowList, CompareByArea);
}

BOOL __stdcall EnumFunc(HWND hWnd, LPARAM a2)
{
  struct tagRECT Rect; // [esp+4h] [ebp-10h] BYREF

  if (g_optIncludeHidden || IsWindowVisible(hWnd))
  {
    GetWindowRect(hWnd, &Rect);
    if (!IsRectEmpty(&Rect))
      WindowList_Push(g_windowList, hWnd, (Rect.right - Rect.left) * (Rect.bottom - Rect.top));
    EnumChildWindows(hWnd, EnumFunc, 0);
  }
  return 1;
}

int __cdecl CompareByArea(WindowEntry *a1, WindowEntry *a2)
{
  return a1->area - a2->area;
}

static WindowList *WindowList_Init(WindowList *self, int capacity)
{
  self->capacity = capacity;
  self->buf = (WindowEntry *)malloc(capacity * sizeof(WindowEntry));
  self->cursor = 0;
  self->count = 0;
  self->cmp = NULL;
  return self;
}

static void WindowList_Push(WindowList *self, HWND hwnd, int area)
{
  if (self->count >= self->capacity)
  {
    int newcap = self->capacity + self->capacity / 2;
    self->capacity = newcap;
    self->buf = (WindowEntry *)realloc(self->buf, newcap * sizeof(WindowEntry));
  }
  self->buf[self->count].hwnd = hwnd;
  self->buf[self->count].area = area;
  self->count++;
}

static WindowEntry *WindowList_Get(WindowList *self, WindowEntry *out, int idx)
{
  if (idx < 0)
    idx = self->cursor;
  if (idx >= self->count)
  {
    out->hwnd = NULL;
    out->area = 0;
  }
  else
  {
    self->cursor = idx;
    *out = self->buf[idx];
  }
  return out;
}

static WindowEntry *WindowList_Next(WindowList *self, WindowEntry *out)
{
  WindowList_Get(self, out, -1); /* read entry at current cursor */
  if (self->cursor < self->count)
    self->cursor++;
  return out;
}

static int WindowList_Seek(WindowList *self, int idx)
{
  if (idx >= 0 && idx < self->count)
    self->cursor = idx;
  return idx;
}

static int WindowList_GetPos(WindowList *self)
{
  return self->cursor;
}

static int WindowList_Count(WindowList *self)
{
  return self->count;
}

static void WindowList_Clear(WindowList *self)
{
  self->count = 0;
  self->cursor = 0;
}

static void WindowList_Sort(WindowList *self, CmpFn cmp)
{
  self->cmp = cmp;
  WindowList_Quicksort(self, 0, self->count - 1);
}

static void WindowList_Quicksort(WindowList *self, int lo, int hi)
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
      while (self->cmp(&self->buf[v6], &self->buf[hi]) < 0);
      do
      {
        if (v18 <= 0)
          break;
        --v18;
      } while (self->cmp(&self->buf[v18], &self->buf[hi]) > 0);
      if (v6 >= v18)
        break;
      WindowEntry tmp = self->buf[v6];
      self->buf[v6] = self->buf[v18];
      self->buf[v18] = tmp;
    }
    WindowEntry tmp = self->buf[v6];
    self->buf[v6] = self->buf[hi];
    self->buf[hi] = tmp;
    WindowList_Quicksort(self, a2, v6 - 1);
    result = v6 + 1;
    a2 = v6 + 1;
    if (hi <= v6 + 1)
      break;
    result = hi;
  }
}
