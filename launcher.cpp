#pragma warning(disable: 4640)
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <winhttp.h>
#include <commctrl.h>
#include <shellapi.h>
#include <bcrypt.h>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ======================================================================
// Constants
// ======================================================================
#define APP_TITLE   L"1yn AutoClick - \x91D1\x9470\x555F\x52D5\x5668 (1ynkeycheck)"
#define TARGET_EXE  L"yy_clicker.exe"
#define CHECK_HWID_DIR L"checkHWID"
#define HWID_FILE   L"hwid_auth.json"

// Railway Bot server for key verification
#define KEY_SERVER_HOST  L"web-production-a8756.up.railway.app"
#define KEY_VERIFY_PATH  L"/api/verify-key"

// HWID encryption salt (must match bot.js HWID_SECRET concept)
static const char HWID_SALT[] = "1yn-autoclick-hwid-salt-v2-s3cur3K3y!";

static const int WIN_W = 420;
static const int WIN_H = 300;

#define IDC_EDIT_KEY     1001
#define IDC_BTN_LAUNCH   1002
#define IDC_BTN_EXIT     1003
#define IDC_LABEL_TITLE  1004
#define IDC_LABEL_DESC   1005
#define IDC_LABEL_STATUS 1006

// ======================================================================
// Globals
// ======================================================================
static HWND hMainWnd    = NULL;
static HWND hEditKey    = NULL;
static HWND hBtnLaunch  = NULL;
static HWND hBtnExit    = NULL;
static HWND hLblStatus  = NULL;
static HFONT hFontNormal = NULL;
static HFONT hFontTitle  = NULL;
static HFONT hFontBtn    = NULL;
static HBRUSH hBrushBg   = NULL;

// Stored after successful verification (for checkHWID file writing)
static char g_hwidHash[128] = {};
static char g_machineCode[128] = {};

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
// Get raw HWID (computer name + user name)
// ======================================================================
static void GetHWID(wchar_t* hwid, int hwidLen) {
    wchar_t compName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD compSize = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(compName, &compSize);

    wchar_t userName[256] = {};
    DWORD userSize = 256;
    GetUserNameW(userName, &userSize);

    wsprintfW(hwid, L"%s_%s", compName, userName);
}

// ======================================================================
// SHA-256 hash using BCrypt (Windows native, no OpenSSL needed)
// ======================================================================
static BOOL Sha256(const BYTE* data, DWORD dataLen, BYTE* hash, DWORD hashLen) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    BOOL ok = FALSE;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0) {
        if (BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) == 0) {
            if (BCryptHashData(hHash, (PUCHAR)data, dataLen, 0) == 0) {
                if (BCryptFinishHash(hHash, hash, hashLen, 0) == 0) {
                    ok = TRUE;
                }
            }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return ok;
}

// ======================================================================
// HMAC-SHA256 using BCrypt
// ======================================================================
static BOOL HmacSha256(const BYTE* key, DWORD keyLen, const BYTE* data, DWORD dataLen, BYTE* hash, DWORD hashLen) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    BOOL ok = FALSE;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) == 0) {
        if (BCryptCreateHash(hAlg, &hHash, NULL, 0, (PUCHAR)key, keyLen, 0) == 0) {
            if (BCryptHashData(hHash, (PUCHAR)data, dataLen, 0) == 0) {
                if (BCryptFinishHash(hHash, hash, hashLen, 0) == 0) {
                    ok = TRUE;
                }
            }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return ok;
}

// ======================================================================
// SHA-512 hash using BCrypt (for machine code)
// ======================================================================
static BOOL Sha512(const BYTE* data, DWORD dataLen, BYTE* hash, DWORD hashLen) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    BOOL ok = FALSE;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA512_ALGORITHM, NULL, 0) == 0) {
        if (BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) == 0) {
            if (BCryptHashData(hHash, (PUCHAR)data, dataLen, 0) == 0) {
                if (BCryptFinishHash(hHash, hash, hashLen, 0) == 0) {
                    ok = TRUE;
                }
            }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return ok;
}

// ======================================================================
// Bytes to hex string
// ======================================================================
static void BytesToHex(const BYTE* bytes, int len, char* hex) {
    const char* hexChars = "0123456789abcdef";
    for (int i = 0; i < len; i++) {
        hex[i * 2]     = hexChars[(bytes[i] >> 4) & 0xF];
        hex[i * 2 + 1] = hexChars[bytes[i] & 0xF];
    }
    hex[len * 2] = '\0';
}

// ======================================================================
// Compute encrypted HWID (HMAC-SHA256 with salt, matches bot.js encryptHWID)
// ======================================================================
static void ComputeEncryptedHWID(const char* rawHwid, char* outHex64) {
    BYTE hash[32];
    HmacSha256(
        (const BYTE*)HWID_SALT, (DWORD)lstrlenA(HWID_SALT),
        (const BYTE*)rawHwid, (DWORD)lstrlenA(rawHwid),
        hash, 32
    );
    BytesToHex(hash, 32, outHex64);
}

// ======================================================================
// Compute machine code (SHA-512 of key:hwid_hash:salt, first 64 chars)
// Matches bot.js generateMachineCode
// ======================================================================
static void ComputeMachineCode(const char* key, const char* hwidHash, char* outHex64) {
    // Build payload: "key:hwidHash:salt"
    char payload[2048] = {};
    wsprintfA(payload, "%s:%s:%s", key, hwidHash, HWID_SALT);

    BYTE hash[64];
    Sha512((const BYTE*)payload, (DWORD)lstrlenA(payload), hash, 64);

    char fullHex[129];
    BytesToHex(hash, 64, fullHex);
    // Take first 64 chars
    for (int i = 0; i < 64; i++) outHex64[i] = fullHex[i];
    outHex64[64] = '\0';
}

// ======================================================================
// Check required files exist in the same directory
// Returns: TRUE if 1ynkeycheck.exe and yy_clicker.exe both exist
// ======================================================================
static BOOL CheckRequiredFiles() {
    wchar_t exeDir[MAX_PATH];
    GetExeDir(exeDir, MAX_PATH);

    // Check yy_clicker.exe
    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s\\%s", exeDir, TARGET_EXE);
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return FALSE;

    return TRUE;
}

// ======================================================================
// Create checkHWID directory if it doesn't exist
// ======================================================================
static BOOL EnsureCheckHwidDir() {
    wchar_t exeDir[MAX_PATH];
    GetExeDir(exeDir, MAX_PATH);

    wchar_t dirPath[MAX_PATH];
    wsprintfW(dirPath, L"%s\\%s", exeDir, CHECK_HWID_DIR);

    DWORD attr = GetFileAttributesW(dirPath);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return CreateDirectoryW(dirPath, NULL);
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE;
}

// ======================================================================
// Write HWID auth file to checkHWID folder
// JSON format: {"hwid_hash":"...","machine_code":"...","timestamp":"..."}
// ======================================================================
static BOOL WriteHwidAuthFile(const char* hwidHash, const char* machineCode) {
    wchar_t exeDir[MAX_PATH];
    GetExeDir(exeDir, MAX_PATH);

    // Ensure directory exists
    if (!EnsureCheckHwidDir()) return FALSE;

    wchar_t filePath[MAX_PATH];
    wsprintfW(filePath, L"%s\\%s\\%s", exeDir, CHECK_HWID_DIR, HWID_FILE);

    // Get current time
    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeStr[64];
    wsprintfA(timeStr, "%04d-%02d-%02dT%02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    // Build JSON content
    char json[2048];
    wsprintfA(json,
        "{\r\n"
        "  \"hwid_hash\": \"%s\",\r\n"
        "  \"machine_code\": \"%s\",\r\n"
        "  \"timestamp\": \"%s\",\r\n"
        "  \"version\": \"2.0\"\r\n"
        "}",
        hwidHash, machineCode, timeStr);

    // Write file
    HANDLE hFile = CreateFileW(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    DWORD written;
    WriteFile(hFile, json, (DWORD)lstrlenA(json), &written, NULL);
    CloseHandle(hFile);

    // Set file as hidden + system to discourage tampering
    SetFileAttributesW(filePath, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY);

    return TRUE;
}

// ======================================================================
// Read HWID auth file from checkHWID folder
// Returns TRUE if file exists and contains valid data
// ======================================================================
static BOOL ReadHwidAuthFile(char* outHwidHash, int hhLen, char* outMachineCode, int mcLen) {
    wchar_t exeDir[MAX_PATH];
    GetExeDir(exeDir, MAX_PATH);

    wchar_t filePath[MAX_PATH];
    wsprintfW(filePath, L"%s\\%s\\%s", exeDir, CHECK_HWID_DIR, HWID_FILE);

    // Temporarily remove readonly to read
    DWORD origAttr = GetFileAttributesW(filePath);

    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    char buf[4096] = {};
    DWORD bytesRead;
    ReadFile(hFile, buf, sizeof(buf) - 1, &bytesRead, NULL);
    CloseHandle(hFile);

    // Simple JSON parsing: find "hwid_hash":"..." and "machine_code":"..."
    char* p1 = strstr(buf, "\"hwid_hash\"");
    char* p2 = strstr(buf, "\"machine_code\"");
    if (!p1 || !p2) return FALSE;

    // Extract hwid_hash value
    char* v1 = strchr(p1 + 11, '"');
    if (!v1) return FALSE;
    v1++; // skip opening quote
    char* e1 = strchr(v1, '"');
    if (!e1) return FALSE;
    int len1 = (int)(e1 - v1);
    if (len1 >= hhLen) return FALSE;
    for (int i = 0; i < len1; i++) outHwidHash[i] = v1[i];
    outHwidHash[len1] = '\0';

    // Extract machine_code value
    char* v2 = strchr(p2 + 14, '"');
    if (!v2) return FALSE;
    v2++;
    char* e2 = strchr(v2, '"');
    if (!e2) return FALSE;
    int len2 = (int)(e2 - v2);
    if (len2 >= mcLen) return FALSE;
    for (int i = 0; i < len2; i++) outMachineCode[i] = v2[i];
    outMachineCode[len2] = '\0';

    return TRUE;
}

// ======================================================================
// Validate existing checkHWID file against current machine
// Returns: TRUE if the file matches current HWID
// ======================================================================
static BOOL ValidateExistingHwid(const char* key_utf8) {
    char storedHash[128] = {};
    char storedMC[128] = {};

    if (!ReadHwidAuthFile(storedHash, 128, storedMC, 128)) {
        return FALSE; // No file or invalid
    }

    // Compute current HWID hash
    wchar_t hwid_w[256] = {};
    GetHWID(hwid_w, 256);
    char hwid_utf8[512] = {};
    WideCharToMultiByte(CP_UTF8, 0, hwid_w, -1, hwid_utf8, 512, NULL, NULL);

    char currentHash[128] = {};
    ComputeEncryptedHWID(hwid_utf8, currentHash);

    // Compare HWID hash
    if (lstrcmpA(storedHash, currentHash) != 0) {
        return FALSE; // Different machine
    }

    // Verify machine code integrity
    char expectedMC[128] = {};
    ComputeMachineCode(key_utf8, currentHash, expectedMC);

    if (lstrcmpA(storedMC, expectedMC) != 0) {
        return FALSE; // Tampered file
    }

    return TRUE;
}

// ======================================================================
// JSON escape helper
// ======================================================================
static int JsonEscapeToBuffer(const char* src, char* dst, int dstLen) {
    int j = 0;
    for (int i = 0; src[i] != '\0' && j < dstLen - 2; i++) {
        if (src[i] == '\\') { dst[j++] = '\\'; dst[j++] = '\\'; }
        else if (src[i] == '"') { dst[j++] = '\\'; dst[j++] = '"'; }
        else if (src[i] == '\n') { dst[j++] = '\\'; dst[j++] = 'n'; }
        else if (src[i] == '\r') { dst[j++] = '\\'; dst[j++] = 'r'; }
        else if (src[i] == '\t') { dst[j++] = '\\'; dst[j++] = 't'; }
        else { dst[j++] = src[i]; }
    }
    dst[j] = '\0';
    return j;
}

// ======================================================================
// Verify key with Railway server via WinHTTP
// Returns: 0 = invalid, 1 = valid, -1 = network error
// On success, stores hwid_hash and machine_code in globals
// ======================================================================
static int VerifyKeyOnline(const wchar_t* key) {
    wchar_t hwid_w[256] = {};
    GetHWID(hwid_w, 256);

    char key_utf8[512] = {};
    char hwid_utf8[512] = {};
    WideCharToMultiByte(CP_UTF8, 0, key, -1, key_utf8, 512, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, hwid_w, -1, hwid_utf8, 512, NULL, NULL);

    char key_esc[1024] = {};
    char hwid_esc[1024] = {};
    JsonEscapeToBuffer(key_utf8, key_esc, 1024);
    JsonEscapeToBuffer(hwid_utf8, hwid_esc, 1024);

    char json[2048] = {};
    wsprintfA(json, "{\"key\":\"%s\",\"hwid\":\"%s\"}", key_esc, hwid_esc);

    HINTERNET hSession = WinHttpOpen(L"1ynkeycheck/2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return -1;

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET hConnect = WinHttpConnect(hSession, KEY_SERVER_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return -1; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", KEY_VERIFY_PATH,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return -1; }

    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

    DWORD timeout = 15000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    const wchar_t* headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
    DWORD jsonLen = (DWORD)lstrlenA(json);
    BOOL bResult = WinHttpSendRequest(hRequest, headers, (DWORD)lstrlenW(headers),
        (LPVOID)json, jsonLen, jsonLen, 0);

    if (!bResult) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return -1;
    }

    bResult = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResult) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    char respBuf[4096] = {};
    DWORD bytesRead = 0;
    WinHttpReadData(hRequest, respBuf, sizeof(respBuf) - 1, &bytesRead);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (statusCode == 200 && strstr(respBuf, "\"valid\":true")) {
        // Compute and store HWID hash + machine code locally
        ComputeEncryptedHWID(hwid_utf8, g_hwidHash);
        ComputeMachineCode(key_utf8, g_hwidHash, g_machineCode);
        return 1;
    }

    return 0;
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

    DWORD attr = GetFileAttributesW(exePath);
    if (attr == INVALID_FILE_ATTRIBUTES) return FALSE;

    wsprintfW(cmdLine, L"\"%s\" \"%s\"", exePath, key);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    BOOL ok = CreateProcessW(exePath, cmdLine, NULL, NULL, FALSE, 0, NULL, exeDir, &si, &pi);
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
// Background verification thread
// ======================================================================
struct VerifyThreadData {
    wchar_t key[512];
};

static DWORD WINAPI VerifyThread(LPVOID param) {
    VerifyThreadData* data = (VerifyThreadData*)param;

    // Convert key to UTF-8 for checkHWID validation
    char key_utf8[512] = {};
    WideCharToMultiByte(CP_UTF8, 0, data->key, -1, key_utf8, 512, NULL, NULL);

    // Check if checkHWID file already exists and is valid for this machine
    BOOL hwidFileValid = ValidateExistingHwid(key_utf8);

    int result = VerifyKeyOnline(data->key);

    if (result == 1) {
        // Write/update checkHWID auth file
        if (!WriteHwidAuthFile(g_hwidHash, g_machineCode)) {
            // Non-fatal: log but continue
        }

        if (LaunchClicker(data->key)) {
            SetWindowTextW(hLblStatus,
                L"\x2705 \x91D1\x9470\x9A57\x8B49\x6210\x529F\xFF01\x7A0B\x5F0F\x5DF2\x555F\x52D5...");
            UpdateWindow(hMainWnd);
            Sleep(1500);
            PostMessageW(hMainWnd, WM_CLOSE, 0, 0);
        } else {
            SetWindowTextW(hLblStatus,
                L"\x627E\x4E0D\x5230 yy_clicker.exe\xFF0C\x8ACB\x78BA\x8A8D\x6A94\x6848\x4F4D\x7F6E\xFF01");
            EnableWindow(hBtnLaunch, TRUE);
        }
    } else if (result == 0) {
        SetWindowTextW(hLblStatus,
            L"\x274C \x91D1\x9470\x7121\x6548\xFF01\x8ACB\x78BA\x8A8D\x60A8\x7684\x91D1\x9470\x3002");
        EnableWindow(hBtnLaunch, TRUE);
        EnableWindow(hEditKey, TRUE);
        SetFocus(hEditKey);
    } else {
        // Network error — if we have a valid local checkHWID, allow offline launch
        if (hwidFileValid) {
            SetWindowTextW(hLblStatus,
                L"\x26A0 \x96E2\x7DDA\x6A21\x5F0F\xFF1A\x672C\x6A5F HWID \x5DF2\x9A57\x8B49\xFF0C\x555F\x52D5\x4E2D...");
            UpdateWindow(hMainWnd);
            if (LaunchClicker(data->key)) {
                Sleep(1500);
                PostMessageW(hMainWnd, WM_CLOSE, 0, 0);
            } else {
                SetWindowTextW(hLblStatus,
                    L"\x627E\x4E0D\x5230 yy_clicker.exe\xFF0C\x8ACB\x78BA\x8A8D\x6A94\x6848\x4F4D\x7F6E\xFF01");
                EnableWindow(hBtnLaunch, TRUE);
            }
        } else {
            SetWindowTextW(hLblStatus,
                L"\x26A0 \x7DB2\x8DEF\x9023\x7DDA\x5931\x6557\xFF01\x8ACB\x6AA2\x67E5\x7DB2\x8DEF\x5F8C\x91CD\x8A66\x3002");
            EnableWindow(hBtnLaunch, TRUE);
            EnableWindow(hEditKey, TRUE);
            SetFocus(hEditKey);
        }
    }

    delete data;
    return 0;
}

// ======================================================================
// Window procedure
// ======================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE: {
        HINSTANCE hi = ((LPCREATESTRUCT)lp)->hInstance;
        hMainWnd = hwnd;

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

        // Title
        {
            HWND h = CreateWindowW(L"STATIC",
                L"1yn AutoClick",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 18, WIN_W, 28, hwnd, (HMENU)IDC_LABEL_TITLE, hi, NULL);
            SendMessageW(h, WM_SETFONT, (WPARAM)hFontTitle, TRUE);
        }

        // Description
        {
            HWND h = CreateWindowW(L"STATIC",
                L"\x8ACB\x8F38\x5165\x60A8\x7684\x6388\x6B0A\x91D1\x9470\x4EE5\x555F\x52D5\x7A0B\x5F0F\r\n"
                L"\x91D1\x9470\x53EF\x5728 Discord \x4F3A\x670D\x5668\x4E2D\x7372\x53D6",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 55, WIN_W - 40, 36, hwnd, (HMENU)IDC_LABEL_DESC, hi, NULL);
            SendMessageW(h, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        }

        // Separator
        CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
            30, 100, WIN_W - 60, 2, hwnd, NULL, hi, NULL);

        // Key input
        hEditKey = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL,
            40, 118, WIN_W - 80, 28, hwnd, (HMENU)IDC_EDIT_KEY, hi, NULL);
        SendMessageW(hEditKey, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        SendMessageW(hEditKey, 0x1501, TRUE,
            (LPARAM)L"\x5728\x6B64\x8F38\x5165\x91D1\x9470...");

        // Launch button
        {
            int btnW = (WIN_W - 100) / 2;
            hBtnLaunch = CreateWindowW(L"BUTTON",
                L"\x9A57\x8B49\x4E26\x555F\x52D5",
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
            20, 210, WIN_W - 40, 40, hwnd, (HMENU)IDC_LABEL_STATUS, hi, NULL);
        SendMessageW(hLblStatus, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

        // Check required files on startup
        if (!CheckRequiredFiles()) {
            SetWindowTextW(hLblStatus,
                L"\x26A0 \x8ACB\x5C07 1ynkeycheck.exe \x548C yy_clicker.exe \x653E\x5728\x540C\x4E00\x8CC7\x6599\x593E");
        }

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
            // Check required files first
            if (!CheckRequiredFiles()) {
                SetWindowTextW(hLblStatus,
                    L"\x274C \x627E\x4E0D\x5230 yy_clicker.exe\xFF0C\x8ACB\x653E\x5728\x540C\x4E00\x8CC7\x6599\x593E\xFF01");
                return 0;
            }

            wchar_t keyBuf[512];
            ZeroMemory(keyBuf, sizeof(keyBuf));
            GetWindowTextW(hEditKey, keyBuf, 512);

            wchar_t* start = keyBuf;
            while (*start == L' ') start++;
            int len = lstrlenW(start);
            while (len > 0 && start[len - 1] == L' ') { start[--len] = L'\0'; }

            if (len == 0) {
                SetWindowTextW(hLblStatus, L"\x8ACB\x8F38\x5165\x91D1\x9470\xFF01");
                SetFocus(hEditKey);
                break;
            }

            EnableWindow(hBtnLaunch, FALSE);
            EnableWindow(hEditKey, FALSE);
            SetWindowTextW(hLblStatus,
                L"\x23F3 \x6B63\x5728\x9A57\x8B49\x91D1\x9470\xFF0C\x8ACB\x7A0D\x5019...");
            UpdateWindow(hwnd);

            VerifyThreadData* data = new VerifyThreadData();
            lstrcpynW(data->key, start, 512);
            HANDLE hThread = CreateThread(NULL, 0, VerifyThread, data, 0, NULL);
            if (hThread) {
                CloseHandle(hThread);
            } else {
                SetWindowTextW(hLblStatus,
                    L"\x7CFB\x7D71\x932F\x8AA4\xFF0C\x8ACB\x91CD\x8A66\x3002");
                EnableWindow(hBtnLaunch, TRUE);
                EnableWindow(hEditKey, TRUE);
                delete data;
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
