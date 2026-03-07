/**
 * 1yn AutoClick — 金鑰啟動器 (launcher.exe)
 *
 * 此程式取代原本的 .cmd 腳本，用原生 Windows GUI 視窗
 * 讓使用者輸入金鑰，然後啟動 yy_clicker.exe 並傳入金鑰。
 *
 * 優點：
 *   - 不會有 CMD 中文亂碼問題（原生 Unicode 支援）
 *   - 不會觸發 Windows 安全性警告（.exe 不像 .cmd 會被攔截）
 *   - 美觀的 GUI 介面
 *
 * 編譯指令：
 *   cl /O2 /DUNICODE /D_UNICODE launcher.cpp /link user32.lib gdi32.lib kernel32.lib shell32.lib
 *
 * 使用方式：
 *   將 launcher.exe 和 yy_clicker.exe 放在同一資料夾，雙擊 launcher.exe 即可。
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <string>

// ======================================================================
// 常數定義
// ======================================================================
static const wchar_t* const APP_TITLE    = L"1yn AutoClick - 金鑰啟動器";
static const wchar_t* const TARGET_EXE   = L"yy_clicker.exe";

// 視窗尺寸
static const int WIN_W = 420;
static const int WIN_H = 280;

// 控件 ID
#define IDC_EDIT_KEY     1001
#define IDC_BTN_LAUNCH   1002
#define IDC_BTN_EXIT     1003
#define IDC_LABEL_TITLE  1004
#define IDC_LABEL_DESC   1005
#define IDC_LABEL_STATUS 1006

// ======================================================================
// 全域變數
// ======================================================================
static HWND hEditKey    = nullptr;
static HWND hBtnLaunch  = nullptr;
static HWND hBtnExit    = nullptr;
static HWND hLblStatus  = nullptr;
static HFONT hFontNormal = nullptr;
static HFONT hFontTitle  = nullptr;
static HFONT hFontBtn    = nullptr;
static HBRUSH hBrushBg   = nullptr;

// ======================================================================
// 取得 exe 所在目錄
// ======================================================================
static std::wstring GetExeDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    auto pos = s.find_last_of(L"\\/");
    if (pos != std::wstring::npos) s = s.substr(0, pos);
    return s;
}

// ======================================================================
// 啟動 yy_clicker.exe 並傳入金鑰
// ======================================================================
static bool LaunchClicker(const std::wstring& key) {
    std::wstring exeDir = GetExeDir();
    std::wstring exePath = exeDir + L"\\" + TARGET_EXE;

    // 檢查 yy_clicker.exe 是否存在
    DWORD attr = GetFileAttributesW(exePath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    // 組裝命令列：yy_clicker.exe "KEY"
    std::wstring cmdLine = L"\"" + exePath + L"\" \"" + key + L"\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    BOOL ok = CreateProcessW(
        exePath.c_str(),
        &cmdLine[0],
        nullptr, nullptr, FALSE,
        0, nullptr,
        exeDir.c_str(),
        &si, &pi
    );

    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    return ok ? true : false;
}

// ======================================================================
// 視窗程序
// ======================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE: {
        // 建立字型
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

        // 背景色
        hBrushBg = CreateSolidBrush(RGB(240, 240, 240));

        HINSTANCE hi = ((LPCREATESTRUCT)lp)->hInstance;

        // 標題
        HWND hTitle = CreateWindowW(L"STATIC",
            L"1yn AutoClick",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 18, WIN_W, 28, hwnd, (HMENU)IDC_LABEL_TITLE, hi, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)hFontTitle, TRUE);

        // 說明文字
        HWND hDesc = CreateWindowW(L"STATIC",
            L"請輸入您的授權金鑰以啟動程式\r\n金鑰可在 Discord 伺服器中獲取",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 55, WIN_W - 40, 36, hwnd, (HMENU)IDC_LABEL_DESC, hi, nullptr);
        SendMessageW(hDesc, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

        // 分隔線（用靜態控件模擬）
        CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
            30, 100, WIN_W - 60, 2, hwnd, nullptr, hi, nullptr);

        // 金鑰輸入框
        hEditKey = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL,
            40, 118, WIN_W - 80, 28, hwnd, (HMENU)IDC_EDIT_KEY, hi, nullptr);
        SendMessageW(hEditKey, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        SendMessageW(hEditKey, EM_SETCUEBANNER, TRUE, (LPARAM)L"在此輸入金鑰...");

        // 啟動按鈕
        hBtnLaunch = CreateWindowW(L"BUTTON",
            L"啟動程式",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            40, 162, (WIN_W - 100) / 2, 34, hwnd, (HMENU)IDC_BTN_LAUNCH, hi, nullptr);
        SendMessageW(hBtnLaunch, WM_SETFONT, (WPARAM)hFontBtn, TRUE);

        // 離開按鈕
        hBtnExit = CreateWindowW(L"BUTTON",
            L"離開",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            40 + (WIN_W - 100) / 2 + 20, 162, (WIN_W - 100) / 2, 34, hwnd, (HMENU)IDC_BTN_EXIT, hi, nullptr);
        SendMessageW(hBtnExit, WM_SETFONT, (WPARAM)hFontBtn, TRUE);

        // 狀態列
        hLblStatus = CreateWindowW(L"STATIC",
            L"",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 210, WIN_W - 40, 20, hwnd, (HMENU)IDC_LABEL_STATUS, hi, nullptr);
        SendMessageW(hLblStatus, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

        // 聚焦到輸入框
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

        if (id == IDC_BTN_LAUNCH || (id == IDC_EDIT_KEY && HIWORD(wp) == EN_KILLFOCUS)) {
            // 只處理按鈕點擊
            if (id != IDC_BTN_LAUNCH) break;

            // 取得金鑰
            wchar_t keyBuf[512] = {};
            GetWindowTextW(hEditKey, keyBuf, 512);
            std::wstring key(keyBuf);

            // 去除前後空白
            while (!key.empty() && key.front() == L' ') key.erase(key.begin());
            while (!key.empty() && key.back() == L' ') key.pop_back();

            if (key.empty()) {
                SetWindowTextW(hLblStatus, L"請輸入金鑰！");
                SetFocus(hEditKey);
                break;
            }

            // 停用按鈕防止重複點擊
            EnableWindow(hBtnLaunch, FALSE);
            SetWindowTextW(hLblStatus, L"正在啟動程式...");
            UpdateWindow(hwnd);

            // 啟動 yy_clicker.exe
            if (LaunchClicker(key)) {
                SetWindowTextW(hLblStatus, L"程式已啟動，即將關閉啟動器...");
                UpdateWindow(hwnd);
                Sleep(1500);
                PostQuitMessage(0);
            } else {
                // 找不到 yy_clicker.exe
                SetWindowTextW(hLblStatus, L"找不到 yy_clicker.exe，請確認檔案位置！");
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

    // 支援 Enter 鍵觸發啟動
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_BTN_LAUNCH, BN_CLICKED), 0);
            return 0;
        }
        break;

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
// 子類化 Edit 控件以攔截 Enter 鍵
// ======================================================================
static WNDPROC g_origEditProc = nullptr;

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        // 按下 Enter 時，觸發啟動按鈕
        HWND hParent = GetParent(hwnd);
        SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(IDC_BTN_LAUNCH, BN_CLICKED), 0);
        return 0;
    }
    return CallWindowProcW(g_origEditProc, hwnd, msg, wp, lp);
}

// ======================================================================
// WinMain
// ======================================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // 註冊視窗類別
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(240, 240, 240));
    wc.lpszClassName = L"LauncherClass";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    // 計算置中位置
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - WIN_W) / 2;
    int posY = (screenH - WIN_H) / 2;

    // 建立視窗（固定大小、不可調整）
    HWND hwnd = CreateWindowExW(
        0,
        L"LauncherClass",
        APP_TITLE,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY, WIN_W, WIN_H,
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd) return 1;

    // 子類化 Edit 控件（攔截 Enter 鍵）
    if (hEditKey) {
        g_origEditProc = (WNDPROC)SetWindowLongPtrW(hEditKey, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // 訊息迴圈
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return (int)msg.wParam;
}
