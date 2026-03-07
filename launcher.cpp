// 1yn AutoClick - Key Launcher (launcher.exe)
// Compile: cl /O2 /DUNICODE /D_UNICODE launcher.cpp /link user32.lib gdi32.lib kernel32.lib shell32.lib

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// EM_SETCUEBANNER requires CommCtrl v6
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ======================================================================
// Constants
// ======================================================================
#define APP_TITLE   L"1yn AutoClick - \x91D1\x9470\x555F\x52D5\x5668"
#define TARGET_EXE  L"yy_clicker.exe"

static const int WIN_W = 420;
static const int WIN_H = 280;

#define IDC_EDIT_KEY     1001
#define IDC_BTN_LAUNCH   1002
#define IDC_BTN_EXIT     1003
#define IDC_LABEL_TITLE  1004
#define IDC_LABEL_DESC   1005
#define IDC_LABEL_STATUS 1006

// ======================================================================
// Globals
// ======================================================================
static HWND hEditKey    = NULL;
static HWND hBtnLaunch  = NULL;
static HWND hBtnExit    = NULL;
static HWND hLblStatus  = NULL;
static HFONT hFontNormal = NULL;
static HFONT hFontTitle  = NULL;
static HFONT hFontBtn    = NULL;
static HBRUSH hBrushBg   = NULL;

// ======================================================================
// Get exe directory
// ======================================================================
static void GetExeDir(wchar_t* buf, int bufLen) {
    GetModuleFileNameW(NULL, buf, bufLen);
    int i = 0, last = -1;
    while (buf[i] != L'\0') {
        if (buf[i] == L'\\' || buf[i] == L'/') last = i;
        i++;
    }
    if (last >= 0) buf[last] = L'\0';
}

// ======================================================================
// Launch yy_clicker.exe with key
// ======================================================================
static BOOL LaunchClicker(const wchar_t* key) {
    wchar_t exeDir[MAX_PATH];
    wchar_t exePath[MAX_PATH];
    wchar_t cmdLine[1024];

    GetExeDir(exeDir, MAX_PATH);
    wsprintfW(exePath, L"%s\\%s", exeDir, TARGET_EXE);

    // Check if yy_clicker.exe exists
    DWORD attr = GetFileAttributesW(exePath);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return FALSE;
    }

    // Build command line: "path\yy_clicker.exe" "KEY"
    wsprintfW(cmdLine, L"\"%s\" \"%s\"", exePath, key);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    BOOL ok = CreateProcessW(
        exePath,
        cmdLine,
        NULL, NULL, FALSE,
        0, NULL,
        exeDir,
        &si, &pi);

    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    return ok;
}

// ======================================================================
// Edit subclass proc (intercept Enter key)
// ======================================================================
static WNDPROC g_origEditProc = NULL;

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        HWND hParent = GetParent(hwnd);
        SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(IDC_BTN_LAUNCH, BN_CLICKED), 0);
        return 0;
    }
    return CallWindowProcW(g_origEditProc, hwnd, msg, wp, lp);
}

// ======================================================================
// Window procedure
// ======================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE: {
        HINSTANCE hi = ((LPCREATESTRUCT)lp)->hInstance;

        // Create fonts
        hFontTitle = CreateFontW(
            22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");

        hFontNormal = CreateFontW(
            15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");

        hFontBtn = CreateFontW(
            16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");

        hBrushBg = CreateSolidBrush(RGB(240, 240, 240));

        // Title label
        {
            HWND h = CreateWindowW(L"STATIC",
                L"1yn AutoClick",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 18, WIN_W, 28, hwnd, (HMENU)IDC_LABEL_TITLE, hi, NULL);
            SendMessageW(h, WM_SETFONT, (WPARAM)hFontTitle, TRUE);
        }

        // Description label
        {
            HWND h = CreateWindowW(L"STATIC",
                L"\x8ACB\x8F38\x5165\x60A8\x7684\x6388\x6B0A\x91D1\x9470\x4EE5\x555F\x52D5\x7A0B\x5F0F\r\n"
                L"\x91D1\x9470\x53EF\x5728 Discord \x4F3A\x670D\x5668\x4E2D\x7372\x53D6",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 55, WIN_W - 40, 36, hwnd, (HMENU)IDC_LABEL_DESC, hi, NULL);
            SendMessageW(h, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        }

        // Separator line
        CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
            30, 100, WIN_W - 60, 2, hwnd, NULL, hi, NULL);

        // Key input edit box
        hEditKey = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL,
            40, 118, WIN_W - 80, 28, hwnd, (HMENU)IDC_EDIT_KEY, hi, NULL);
        SendMessageW(hEditKey, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        // Set placeholder text
        SendMessageW(hEditKey, 0x1501, TRUE,
            (LPARAM)L"\x5728\x6B64\x8F38\x5165\x91D1\x9470...");

        // Launch button
        {
            int btnW = (WIN_W - 100) / 2;
            hBtnLaunch = CreateWindowW(L"BUTTON",
                L"\x555F\x52D5\x7A0B\x5F0F",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                40, 162, btnW, 34, hwnd, (HMENU)IDC_BTN_LAUNCH, hi, NULL);
            SendMessageW(hBtnLaunch, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
        }

        // Exit button
        {
            int btnW = (WIN_W - 100) / 2;
            hBtnExit = CreateWindowW(L"BUTTON",
                L"\x96E2\x958B",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                40 + btnW + 20, 162, btnW, 34, hwnd, (HMENU)IDC_BTN_EXIT, hi, NULL);
            SendMessageW(hBtnExit, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
        }

        // Status label
        hLblStatus = CreateWindowW(L"STATIC",
            L"",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 210, WIN_W - 40, 20, hwnd, (HMENU)IDC_LABEL_STATUS, hi, NULL);
        SendMessageW(hLblStatus, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

        SetFocus(hEditKey);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, RGB(240, 240, 240));
        SetTextColor(hdc, RGB(30, 30, 30));
        return (LRESULT)hBrushBg;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, RGB(255, 255, 255));
        SetTextColor(hdc, RGB(0, 0, 0));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);

        if (id == IDC_BTN_LAUNCH) {
            wchar_t keyBuf[512];
            ZeroMemory(keyBuf, sizeof(keyBuf));
            GetWindowTextW(hEditKey, keyBuf, 512);

            // Trim spaces
            wchar_t* start = keyBuf;
            while (*start == L' ') start++;
            int len = lstrlenW(start);
            while (len > 0 && start[len - 1] == L' ') { start[--len] = L'\0'; }

            if (len == 0) {
                // "Please enter key!"
                SetWindowTextW(hLblStatus, L"\x8ACB\x8F38\x5165\x91D1\x9470\xFF01");
                SetFocus(hEditKey);
                break;
            }

            EnableWindow(hBtnLaunch, FALSE);
            // "Launching..."
            SetWindowTextW(hLblStatus, L"\x6B63\x5728\x555F\x52D5\x7A0B\x5F0F...");
            UpdateWindow(hwnd);

            if (LaunchClicker(start)) {
                // "Launched, closing launcher..."
                SetWindowTextW(hLblStatus,
                    L"\x7A0B\x5F0F\x5DF2\x555F\x52D5\xFF0C\x5373\x5C07\x95DC\x9589\x555F\x52D5\x5668...");
                UpdateWindow(hwnd);
                Sleep(1500);
                PostQuitMessage(0);
            } else {
                // "Cannot find yy_clicker.exe!"
                SetWindowTextW(hLblStatus,
                    L"\x627E\x4E0D\x5230 yy_clicker.exe\xFF0C\x8ACB\x78BA\x8A8D\x6A94\x6848\x4F4D\x7F6E\xFF01");
                EnableWindow(hBtnLaunch, TRUE);
            }
            break;
        }

        if (id == IDC_BTN_EXIT) {
            PostQuitMessage(0);
            break;
        }
        break;
    }

    case WM_DESTROY:
        if (hFontTitle)  DeleteObject(hFontTitle);
        if (hFontNormal) DeleteObject(hFontNormal);
        if (hFontBtn)    DeleteObject(hFontBtn);
        if (hBrushBg)    DeleteObject(hBrushBg);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ======================================================================
// WinMain
// ======================================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR lpCmd, int nShow) {
    (void)hPrev;
    (void)lpCmd;
    (void)nShow;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(240, 240, 240));
    wc.lpszClassName = L"LauncherClass";
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - WIN_W) / 2;
    int posY = (screenH - WIN_H) / 2;

    HWND hwnd = CreateWindowExW(
        0,
        L"LauncherClass",
        APP_TITLE,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY, WIN_W, WIN_H,
        NULL, NULL, hInst, NULL);

    if (!hwnd) return 1;

    // Subclass edit control to intercept Enter key
    if (hEditKey) {
        g_origEditProc = (WNDPROC)SetWindowLongPtrW(hEditKey, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return (int)msg.wParam;
}
