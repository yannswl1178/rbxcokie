/**
 * 1ynkeycheck.exe — 金鑰驗證啟動器（雙 Key 系統 + 增強型 HWID）
 *
 * 此程式只接受「金鑰」（1YN- 格式，License Key），
 * 不接受「密鑰」（SEC- 格式，Secret Key）。
 *
 * 流程：
 *   1. 用戶輸入金鑰（1YN-XXXX-XXXX-XXXX-XXXX）
 *   2. 向 Discord Bot 伺服器驗證金鑰 + HWID 綁定
 *   3. 驗證成功 → 在 checkHWID 資料夾生成加密 HWID 檔案（含 session_token）
 *   4. 啟動 yy_clicker.exe 並傳入金鑰
 *   5. 自動關閉啟動器
 *
 * 增強型 HWID：電腦名稱 + 使用者名稱 + 磁碟序號 + CPU ID
 * Session Token：UUID 格式，防止 checkHWID 資料夾被複製到其他電腦
 *
 * 編譯：
 *   cl /EHsc /Fe:1ynkeycheck.exe launcher.cpp /link bcrypt.lib
 */

#pragma warning(disable: 4996)
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winhttp.h>
#include <commctrl.h>
#include <bcrypt.h>
#include <objbase.h>
#include <string>
#include <cstdio>
#include <intrin.h>

// ======================================================================
// 常數
// ======================================================================
static const wchar_t* APP_TITLE = L"1yn AutoClick - \x91D1\x9470\x9A57\x8B49\x5668";
static const int WIN_W = 380;
static const int WIN_H = 280;

// Discord Bot 金鑰驗證伺服器
static const wchar_t* KEY_SERVER_HOST = L"web-production-a8756.up.railway.app";
static const wchar_t* KEY_VERIFY_PATH = L"/api/verify-key";
static const wchar_t* HWID_VERIFY_PATH = L"/api/verify-hwid";

// Google Apps Script URL（HWID 同步用）
static const char* GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbxFID2dQMjC5xK228bkORU9ZYXICwtfdJ7gFSuOA3Xe69bULbpN9uKdmSLT_9xECW6usw/exec";

// HWID 加密 salt（必須與 bot.js 和 yy_clicker.cpp 一致）
static const char HWID_SALT[] = "1yn-autoclick-hwid-salt-v2-s3cur3K3y!";

// checkHWID 資料夾和檔案名稱
#define CHECK_HWID_DIR L"checkHWID"
#define HWID_FILE L"hwid_auth.json"

// 控制項 ID
#define IDC_LABEL_TITLE   201
#define IDC_LABEL_DESC    202
#define IDC_EDIT_KEY      203
#define IDC_BTN_LAUNCH    204
#define IDC_BTN_EXIT      205
#define IDC_LABEL_STATUS  206

// ======================================================================
// 全域變數
// ======================================================================
static HWND hEditKey    = nullptr;
static HWND hBtnLaunch  = nullptr;
static HWND hLblStatus  = nullptr;
static HFONT hFontTitle  = nullptr;
static HFONT hFontNormal = nullptr;
static HFONT hFontBtn    = nullptr;
static HBRUSH hBrushBg   = nullptr;

// ======================================================================
// 工具函式：取得 exe 所在目錄
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
// 檢查必要檔案
// ======================================================================
static bool CheckRequiredFiles() {
    wchar_t exeDir[MAX_PATH];
    GetExeDir(exeDir, MAX_PATH);

    // 檢查 yy_clicker.exe
    wchar_t clickerPath[MAX_PATH];
    wsprintfW(clickerPath, L"%s\\yy_clicker.exe", exeDir);
    DWORD attr = GetFileAttributesW(clickerPath);
    if (attr == INVALID_FILE_ATTRIBUTES) return false;

    return true;
}

// ======================================================================
// HMAC-SHA-256 加密（Windows BCrypt API）
// ======================================================================
static bool HmacSha256(const BYTE* key, DWORD keyLen, const BYTE* data, DWORD dataLen, BYTE* hash, DWORD hashLen) {
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

// ======================================================================
// SHA-512 雜湊
// ======================================================================
static bool Sha512(const BYTE* data, DWORD dataLen, BYTE* hash, DWORD hashLen) {
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

// ======================================================================
// 位元組轉十六進位字串
// ======================================================================
static void BytesToHex(const BYTE* bytes, int len, char* hex) {
    const char* hc = "0123456789abcdef";
    for (int i = 0; i < len; i++) {
        hex[i * 2]     = hc[(bytes[i] >> 4) & 0xF];
        hex[i * 2 + 1] = hc[bytes[i] & 0xF];
    }
    hex[len * 2] = '\0';
}

// ======================================================================
// 計算加密 HWID（HMAC-SHA-256）
// ======================================================================
static void ComputeEncryptedHWID(const char* rawHwid, char* outHex64) {
    BYTE hash[32];
    HmacSha256((const BYTE*)HWID_SALT, (DWORD)strlen(HWID_SALT),
        (const BYTE*)rawHwid, (DWORD)strlen(rawHwid), hash, 32);
    BytesToHex(hash, 32, outHex64);
}

// ======================================================================
// 計算機碼（SHA-512）
// ======================================================================
static void ComputeMachineCode(const char* key, const char* hwidHash, char* outHex64) {
    char payload[2048] = {};
    wsprintfA(payload, "%s:%s:%s", key, hwidHash, HWID_SALT);
    BYTE hash[64];
    Sha512((const BYTE*)payload, (DWORD)strlen(payload), hash, 64);
    char fullHex[129];
    BytesToHex(hash, 64, fullHex);
    // 取前 64 字元
    for (int i = 0; i < 64; i++) outHex64[i] = fullHex[i];
    outHex64[64] = '\0';
}

// ======================================================================
// 增強型 HWID 計算（電腦名稱 + 使用者名稱 + 磁碟序號 + CPU ID）
// ======================================================================

// 取得磁碟序號（C: 磁碟）
static void GetDiskSerial(char* outBuf, int bufSize) {
    DWORD serialNumber = 0;
    if (GetVolumeInformationA("C:\\", NULL, 0, &serialNumber, NULL, NULL, NULL, 0)) {
        sprintf_s(outBuf, bufSize, "%08lX", serialNumber);
    } else {
        strcpy_s(outBuf, bufSize, "UNKNOWN_DISK");
    }
}

// 取得 CPU ID
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

// 增強型原始 HWID
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

// 向後相容：舊版原始 HWID
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
// 產生 Session Token（UUID 格式）
// ======================================================================
static void GenerateSessionToken(char* outBuf, int bufSize) {
    GUID guid;
    if (CoCreateGuid(&guid) == S_OK) {
        sprintf_s(outBuf, bufSize,
            "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            guid.Data1, guid.Data2, guid.Data3,
            guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
            guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    } else {
        // Fallback: 使用時間戳 + 隨機數
        SYSTEMTIME st;
        GetLocalTime(&st);
        srand(GetTickCount());
        sprintf_s(outBuf, bufSize, "%04d%02d%02d-%02d%02d%02d-%08x-%08x",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond,
            rand() * rand(), rand() * rand());
    }
}

// ======================================================================
// 建立 checkHWID 資料夾並寫入 hwid_auth.json（含 session_token）
// ======================================================================
static bool CreateCheckHWID(const char* keyUtf8) {
    wchar_t exeDir[MAX_PATH];
    GetExeDir(exeDir, MAX_PATH);

    // 建立 checkHWID 資料夾
    wchar_t dirPath[MAX_PATH];
    wsprintfW(dirPath, L"%s\\%s", exeDir, CHECK_HWID_DIR);
    CreateDirectoryW(dirPath, NULL);

    // 設定資料夾為隱藏+系統
    SetFileAttributesW(dirPath, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

    // 計算增強型加密 HWID
    char rawHwid[512] = {};
    GetEnhancedRawHWID(rawHwid, 512);

    char hwidHash[128] = {};
    ComputeEncryptedHWID(rawHwid, hwidHash);

    char machineCode[128] = {};
    ComputeMachineCode(keyUtf8, hwidHash, machineCode);

    // 產生 Session Token（UUID）
    char sessionToken[64] = {};
    GenerateSessionToken(sessionToken, 64);

    // 取得時間戳
    SYSTEMTIME st;
    GetLocalTime(&st);
    char timestamp[64];
    sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    // 寫入 JSON（含 session_token）
    char json[4096];
    sprintf(json,
        "{\n"
        "  \"hwid_hash\": \"%s\",\n"
        "  \"machine_code\": \"%s\",\n"
        "  \"session_token\": \"%s\",\n"
        "  \"key\": \"%s\",\n"
        "  \"created_at\": \"%s\",\n"
        "  \"version\": \"3.0\"\n"
        "}",
        hwidHash, machineCode, sessionToken, keyUtf8, timestamp);

    wchar_t filePath[MAX_PATH];
    wsprintfW(filePath, L"%s\\%s\\%s", exeDir, CHECK_HWID_DIR, HWID_FILE);

    HANDLE hFile = CreateFileW(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD written;
    WriteFile(hFile, json, (DWORD)strlen(json), &written, NULL);
    CloseHandle(hFile);

    return true;
}

// ======================================================================
// JSON 字串轉義（完整版）
// ======================================================================
static std::string JsonEscape(const char* raw) {
    std::string out;
    if (!raw) return out;
    out.reserve(strlen(raw) + 64);
    for (const char* p = raw; *p; ++p) {
        switch (*p) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if ((unsigned char)*p < 0x20) {
                char hex[8];
                sprintf(hex, "\\u%04x", (unsigned char)*p);
                out += hex;
            } else {
                out += *p;
            }
            break;
        }
    }
    return out;
}

// ======================================================================
// 同步 HWID + Session Token 到 Google Sheets
// ======================================================================
static void SyncHWIDToGoogleSheets(const char* keyUtf8, const char* hwidHash, const char* machineCode, const char* sessionToken) {
    HINTERNET hSession = WinHttpOpen(L"1ynKeyCheck/2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return;

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET hConnect = WinHttpConnect(hSession, L"script.google.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST",
        L"/macros/s/AKfycbxFID2dQMjC5xK228bkORU9ZYXICwtfdJ7gFSuOA3Xe69bULbpN9uKdmSLT_9xECW6usw/exec",
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return; }

    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

    // 允許自動 redirect
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    DWORD timeout = 15000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    std::string json = "{";
    json += "\"action\":\"hwid_bind\",";
    json += "\"license_key\":\"";     json += JsonEscape(keyUtf8);      json += "\",";
    json += "\"hwid_hash\":\"";       json += JsonEscape(hwidHash);     json += "\",";
    json += "\"machine_code\":\"";    json += JsonEscape(machineCode);  json += "\",";
    json += "\"session_token\":\"";   json += JsonEscape(sessionToken); json += "\"";
    json += "}";

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    WinHttpSendRequest(hRequest, headers, (DWORD)wcslen(headers),
        (LPVOID)json.c_str(), (DWORD)json.size(), (DWORD)json.size(), 0);
    WinHttpReceiveResponse(hRequest, NULL);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}

// ======================================================================
// 同步 Session Token 到 Discord Bot 伺服器
// ======================================================================
static void SyncSessionTokenToBot(const char* keyUtf8, const char* hwidHash, const char* machineCode, const char* sessionToken) {
    std::string json = "{";
    json += "\"key\":\"";             json += JsonEscape(keyUtf8);      json += "\",";
    json += "\"hwid_hash\":\"";       json += JsonEscape(hwidHash);     json += "\",";
    json += "\"machine_code\":\"";    json += JsonEscape(machineCode);  json += "\",";
    json += "\"session_token\":\"";   json += JsonEscape(sessionToken); json += "\"";
    json += "}";

    HINTERNET hSession = WinHttpOpen(L"1ynKeyCheck/2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return;

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET hConnect = WinHttpConnect(hSession, KEY_SERVER_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/update-session",
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return; }

    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

    DWORD timeout = 10000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    WinHttpSendRequest(hRequest, headers, (DWORD)wcslen(headers),
        (LPVOID)json.c_str(), (DWORD)json.size(), (DWORD)json.size(), 0);
    WinHttpReceiveResponse(hRequest, NULL);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}

// ======================================================================
// 金鑰驗證（向 Discord Bot 伺服器）
// 回傳值：0=成功, 1=無效, 2=SEC密鑰錯誤, 3=HWID衝突, -1=網路錯誤
// ======================================================================
static int VerifyKey(const wchar_t* key) {
    if (!key || wcslen(key) < 10) return 1;

    // 取得增強型 HWID
    char rawHwid[512] = {};
    GetEnhancedRawHWID(rawHwid, 512);

    // 轉為 UTF-8
    int keyLen = WideCharToMultiByte(CP_UTF8, 0, key, -1, NULL, 0, NULL, NULL);
    char* keyUtf8 = new char[keyLen + 1]();
    WideCharToMultiByte(CP_UTF8, 0, key, -1, keyUtf8, keyLen, NULL, NULL);

    // 檢查是否為 SEC- 格式（密鑰不可用於 exe）
    if (strncmp(keyUtf8, "SEC-", 4) == 0 || strncmp(keyUtf8, "sec-", 4) == 0) {
        delete[] keyUtf8;
        return 2;
    }

    std::string json = "{";
    json += "\"key\":\"";  json += JsonEscape(keyUtf8); json += "\",";
    json += "\"hwid\":\""; json += JsonEscape(rawHwid);  json += "\"";
    json += "}";

    delete[] keyUtf8;

    // WinHTTP 連線
    HINTERNET hSession = WinHttpOpen(L"1ynKeyCheck/2.0",
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

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    BOOL bResult = WinHttpSendRequest(hRequest, headers, (DWORD)wcslen(headers),
        (LPVOID)json.c_str(), (DWORD)json.size(), (DWORD)json.size(), 0);

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

    char respBuf[2048] = {};
    DWORD bytesRead = 0;
    WinHttpReadData(hRequest, respBuf, sizeof(respBuf) - 1, &bytesRead);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (statusCode == 200 && strstr(respBuf, "\"valid\":true"))
        return 0;

    if (strstr(respBuf, "HWID"))
        return 3;

    // [DEBUG] 顯示伺服器回應（除錯用，正式版可移除）
    {
        wchar_t dbgMsg[2048] = {};
        wchar_t respW[1024] = {};
        MultiByteToWideChar(CP_UTF8, 0, respBuf, -1, respW, 1024);
        swprintf_s(dbgMsg, 2048, L"[DEBUG] HTTP %lu\n\nResponse:\n%s", statusCode, respW);
        MessageBoxW(NULL, dbgMsg, L"VerifyKey Debug", MB_ICONINFORMATION | MB_OK);
    }

    return 1;
}

// ======================================================================
// JSON 解析輔助
// ======================================================================
static bool ParseJsonString(const char* json, const char* fieldName, char* outBuf, int bufSize) {
    char searchKey[128] = {};
    sprintf_s(searchKey, 128, "\"%s\"", fieldName);
    const char* p = strstr(json, searchKey);
    if (!p) return false;
    p += strlen(searchKey);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != '"') return false;
    p++;
    const char* end = strchr(p, '"');
    if (!end) return false;
    int len = (int)(end - p);
    if (len >= bufSize) return false;
    for (int i = 0; i < len; i++) outBuf[i] = p[i];
    outBuf[len] = '\0';
    return true;
}

// ======================================================================
// 開機自動啟動：寫入 Windows 啟動登錄
// ======================================================================
static void RegisterStartup() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        wchar_t regValue[MAX_PATH + 4] = {};
        wsprintfW(regValue, L"\"%s\"", exePath);

        RegSetValueExW(hKey, L"1ynAutoClick", 0, REG_SZ,
            (const BYTE*)regValue,
            (DWORD)((wcslen(regValue) + 1) * sizeof(wchar_t)));

        RegCloseKey(hKey);
    }
}

// ======================================================================
// 驗證執行緒資料
// ======================================================================
struct VerifyThreadData {
    wchar_t key[512];
};

static DWORD WINAPI VerifyThread(LPVOID lpParam) {
    VerifyThreadData* data = (VerifyThreadData*)lpParam;
    if (!data) return 1;

    int result = VerifyKey(data->key);

    if (result == 0) {
        // 驗證成功 → 建立 checkHWID 資料夾（含 session_token）
        char keyUtf8[512] = {};
        WideCharToMultiByte(CP_UTF8, 0, data->key, -1, keyUtf8, 512, NULL, NULL);

        CreateCheckHWID(keyUtf8);

        // 讀取剛建立的 session_token
        wchar_t exeDir[MAX_PATH];
        GetExeDir(exeDir, MAX_PATH);
        wchar_t filePath[MAX_PATH];
        wsprintfW(filePath, L"%s\\%s\\%s", exeDir, CHECK_HWID_DIR, HWID_FILE);

        char sessionToken[128] = {};
        HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            char buf[8192] = {};
            DWORD br;
            ReadFile(hFile, buf, sizeof(buf) - 1, &br, NULL);
            CloseHandle(hFile);
            ParseJsonString(buf, "session_token", sessionToken, 128);
        }

        // 計算 HWID 資訊
        char rawHwid[512] = {};
        GetEnhancedRawHWID(rawHwid, 512);
        char hwidHash[128] = {};
        ComputeEncryptedHWID(rawHwid, hwidHash);
        char machineCode[128] = {};
        ComputeMachineCode(keyUtf8, hwidHash, machineCode);

        // 同步到 Google Sheets（含 session_token）
        SyncHWIDToGoogleSheets(keyUtf8, hwidHash, machineCode, sessionToken);

        // 同步 session_token 到 Discord Bot 伺服器
        SyncSessionTokenToBot(keyUtf8, hwidHash, machineCode, sessionToken);

        // 啟動 yy_clicker.exe
        wchar_t clickerPath[MAX_PATH];
        wsprintfW(clickerPath, L"%s\\yy_clicker.exe", exeDir);

        wchar_t cmdLine[1024];
        wsprintfW(cmdLine, L"\"%s\" %s", clickerPath, data->key);

        STARTUPINFOW si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));

        if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            RegisterStartup();
            // 啟動成功，關閉啟動器
            PostQuitMessage(0);
        } else {
            SetWindowTextW(hLblStatus,
                L"\x274C \x7121\x6CD5\x555F\x52D5 yy_clicker.exe\xFF0C\x8ACB\x78BA\x8A8D\x6A94\x6848\x5B58\x5728\xFF01");
            EnableWindow(hBtnLaunch, TRUE);
            EnableWindow(hEditKey, TRUE);
        }
    }
    else if (result == 2) {
        // SEC- 密鑰不可用於 exe
        SetWindowTextW(hLblStatus,
            L"\x26A0 \x9019\x662F\x5BC6\x9470\xFF0C\x4E0D\x662F\x91D1\x9470\xFF01\n"
            L"\x8ACB\x5148\x5728 Discord \x5146\x63DB\x5BC6\x9470\xFF0C\x518D\x6309\x3010\x7372\x53D6\x91D1\x9470\x3011");
        EnableWindow(hBtnLaunch, TRUE);
        EnableWindow(hEditKey, TRUE);
    }
    else if (result == 3) {
        // HWID 衝突
        SetWindowTextW(hLblStatus,
            L"\x274C \x6B64\x91D1\x9470\x5DF2\x7D81\x5B9A\x81F3\x5176\x4ED6\x88DD\x7F6E\xFF01\n"
            L"\x8ACB\x5728 Discord \x91CD\x7F6E HWID \x5F8C\x518D\x8A66");
        EnableWindow(hBtnLaunch, TRUE);
        EnableWindow(hEditKey, TRUE);
    }
    else if (result == -1) {
        // 網路錯誤 → 嘗試離線驗證
        char keyUtf8[512] = {};
        WideCharToMultiByte(CP_UTF8, 0, data->key, -1, keyUtf8, 512, NULL, NULL);

        // 嘗試讀取 checkHWID 資料夾進行離線驗證
        wchar_t exeDir[MAX_PATH];
        GetExeDir(exeDir, MAX_PATH);
        wchar_t filePath[MAX_PATH];
        wsprintfW(filePath, L"%s\\%s\\%s", exeDir, CHECK_HWID_DIR, HWID_FILE);

        HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        bool offlineOk = false;

        if (hFile != INVALID_HANDLE_VALUE) {
            char buf[8192] = {};
            DWORD br;
            ReadFile(hFile, buf, sizeof(buf) - 1, &br, NULL);
            CloseHandle(hFile);

            // 解析欄位
            char storedKey[256] = {};
            char storedHash[128] = {};
            char storedMC[128] = {};

            ParseJsonString(buf, "key", storedKey, 256);
            ParseJsonString(buf, "hwid_hash", storedHash, 128);
            ParseJsonString(buf, "machine_code", storedMC, 128);

            // 驗證金鑰匹配
            if (strcmp(storedKey, keyUtf8) == 0) {
                // 嘗試增強型 HWID
                char rawHwid[512] = {};
                GetEnhancedRawHWID(rawHwid, 512);
                char currentHash[128] = {};
                ComputeEncryptedHWID(rawHwid, currentHash);

                bool hwidMatch = (strcmp(storedHash, currentHash) == 0);

                // 向後相容：嘗試舊版 HWID
                if (!hwidMatch) {
                    char legacyHwid[512] = {};
                    GetLegacyRawHWID(legacyHwid, 512);
                    char legacyHash[128] = {};
                    ComputeEncryptedHWID(legacyHwid, legacyHash);
                    hwidMatch = (strcmp(storedHash, legacyHash) == 0);
                    if (hwidMatch) {
                        char expectedMC[128] = {};
                        ComputeMachineCode(keyUtf8, legacyHash, expectedMC);
                        if (strcmp(storedMC, expectedMC) == 0) offlineOk = true;
                    }
                } else {
                    char expectedMC[128] = {};
                    ComputeMachineCode(keyUtf8, currentHash, expectedMC);
                    if (strcmp(storedMC, expectedMC) == 0) offlineOk = true;
                }
            }
        }

        if (offlineOk) {
            // 離線驗證成功，啟動 yy_clicker.exe
            wchar_t exeDir2[MAX_PATH];
            GetExeDir(exeDir2, MAX_PATH);
            wchar_t clickerPath[MAX_PATH];
            wsprintfW(clickerPath, L"%s\\yy_clicker.exe", exeDir2);
            wchar_t cmdLine[1024];
            wsprintfW(cmdLine, L"\"%s\" %s", clickerPath, data->key);

            STARTUPINFOW si;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi;
            ZeroMemory(&pi, sizeof(pi));

            if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                RegisterStartup();
                PostQuitMessage(0);
            } else {
                SetWindowTextW(hLblStatus,
                    L"\x274C \x7121\x6CD5\x555F\x52D5 yy_clicker.exe");
                EnableWindow(hBtnLaunch, TRUE);
                EnableWindow(hEditKey, TRUE);
            }
        } else {
            SetWindowTextW(hLblStatus,
                L"\x274C \x7DB2\x8DEF\x9023\x7DDA\x5931\x6557\xFF0C\x8ACB\x6AA2\x67E5\x7DB2\x8DEF\x5F8C\x91CD\x8A66");
            EnableWindow(hBtnLaunch, TRUE);
            EnableWindow(hEditKey, TRUE);
        }
    }
    else {
        // 金鑰無效
        SetWindowTextW(hLblStatus,
            L"\x274C \x91D1\x9470\x7121\x6548\xFF0C\x8ACB\x78BA\x8A8D\x60A8\x8F38\x5165\x7684\x91D1\x9470\x662F\x5426\x6B63\x78BA");
        EnableWindow(hBtnLaunch, TRUE);
        EnableWindow(hEditKey, TRUE);
    }

    delete data;
    return 0;
}

// ======================================================================
// Edit 子類別化（Enter 鍵觸發驗證）
// ======================================================================
static WNDPROC g_origEditProc = nullptr;

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        PostMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(IDC_BTN_LAUNCH, BN_CLICKED), (LPARAM)hBtnLaunch);
        return 0;
    }
    return CallWindowProcW(g_origEditProc, hwnd, msg, wp, lp);
}

// ======================================================================
// 視窗程序
// ======================================================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = ((CREATESTRUCT*)lp)->hInstance;

        hFontTitle = CreateFontW(
            22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");

        hFontNormal = CreateFontW(
            14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");

        hFontBtn = CreateFontW(
            16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft JhengHei UI");

        hBrushBg = CreateSolidBrush(RGB(240, 240, 240));

        // 標題
        {
            HWND h = CreateWindowW(L"STATIC",
                L"1yn AutoClick",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 18, WIN_W, 28, hwnd, (HMENU)IDC_LABEL_TITLE, hi, NULL);
            SendMessageW(h, WM_SETFONT, (WPARAM)hFontTitle, TRUE);
        }

        // 說明文字
        {
            HWND h = CreateWindowW(L"STATIC",
                L"\x8ACB\x8F38\x5165\x60A8\x7684\x91D1\x9470\xFF08" L"1YN-" L"\x683C\x5F0F\xFF09\x4EE5\x555F\x52D5\x7A0B\x5F0F\r\n"
                L"\x91D1\x9470\x53EF\x5728 Discord \x63A7\x5236\x9762\x677F\x3010\x7372\x53D6\x91D1\x9470\x3011\x7372\x5F97",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 55, WIN_W - 40, 36, hwnd, (HMENU)IDC_LABEL_DESC, hi, NULL);
            SendMessageW(h, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        }

        // 分隔線
        CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
            30, 100, WIN_W - 60, 2, hwnd, NULL, hi, NULL);

        // 金鑰輸入框
        hEditKey = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL,
            40, 118, WIN_W - 80, 28, hwnd, (HMENU)IDC_EDIT_KEY, hi, NULL);
        SendMessageW(hEditKey, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
        SendMessageW(hEditKey, 0x1501, TRUE,
            (LPARAM)L"1YN-XXXX-XXXX-XXXX-XXXX");

        // 驗證按鈕
        {
            int btnW = (WIN_W - 100) / 2;
            hBtnLaunch = CreateWindowW(L"BUTTON",
                L"\x9A57\x8B49\x4E26\x555F\x52D5",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                40, 162, btnW, 34, hwnd, (HMENU)IDC_BTN_LAUNCH, hi, NULL);
            SendMessageW(hBtnLaunch, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
        }

        // 離開按鈕
        {
            int btnW = (WIN_W - 100) / 2;
            CreateWindowW(L"BUTTON",
                L"\x96E2\x958B",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                40 + btnW + 20, 162, btnW, 34, hwnd, (HMENU)IDC_BTN_EXIT, hi, NULL);
        }

        // 狀態標籤
        hLblStatus = CreateWindowW(L"STATIC",
            L"",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 210, WIN_W - 40, 50, hwnd, (HMENU)IDC_LABEL_STATUS, hi, NULL);
        SendMessageW(hLblStatus, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

        // 啟動時檢查必要檔案
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
            // 檢查必要檔案
            if (!CheckRequiredFiles()) {
                SetWindowTextW(hLblStatus,
                    L"\x274C \x627E\x4E0D\x5230 yy_clicker.exe\xFF0C\x8ACB\x653E\x5728\x540C\x4E00\x8CC7\x6599\x593E\xFF01");
                return 0;
            }

            wchar_t keyBuf[512];
            ZeroMemory(keyBuf, sizeof(keyBuf));
            GetWindowTextW(hEditKey, keyBuf, 512);

            // 去除前後空白
            wchar_t* start = keyBuf;
            while (*start == L' ') start++;
            int len = lstrlenW(start);
            while (len > 0 && start[len - 1] == L' ') { start[--len] = L'\0'; }

            if (len == 0) {
                SetWindowTextW(hLblStatus, L"\x8ACB\x8F38\x5165\x91D1\x9470\xFF01");
                SetFocus(hEditKey);
                break;
            }

            // 轉大寫
            for (int i = 0; start[i]; i++) {
                if (start[i] >= L'a' && start[i] <= L'z')
                    start[i] -= 32;
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
                    L"\x7CFB\x7D71\x932F\x8AA4\xFF0C\x8ACB\x91CD\x8A66");
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
// 自動啟動：偵測 checkHWID + yy_clicker.exe → 直接啟動並關閉 launcher
// ======================================================================
static bool TryAutoLaunch() {
    wchar_t exeDir[MAX_PATH];
    GetExeDir(exeDir, MAX_PATH);

    wchar_t clickerPath[MAX_PATH];
    wsprintfW(clickerPath, L"%s\\yy_clicker.exe", exeDir);
    if (GetFileAttributesW(clickerPath) == INVALID_FILE_ATTRIBUTES)
        return false;

    wchar_t filePath[MAX_PATH];
    wsprintfW(filePath, L"%s\\%s\\%s", exeDir, CHECK_HWID_DIR, HWID_FILE);

    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    char buf[8192] = {};
    DWORD br;
    ReadFile(hFile, buf, sizeof(buf) - 1, &br, NULL);
    CloseHandle(hFile);

    char storedKey[256] = {};
    char storedHash[128] = {};
    char storedMC[128] = {};

    if (!ParseJsonString(buf, "key", storedKey, 256)) return false;
    if (!ParseJsonString(buf, "hwid_hash", storedHash, 128)) return false;
    if (!ParseJsonString(buf, "machine_code", storedMC, 128)) return false;
    if (strlen(storedKey) < 10) return false;

    bool hwidMatch = false;
    char rawHwid[512] = {};
    GetEnhancedRawHWID(rawHwid, 512);
    char currentHash[128] = {};
    ComputeEncryptedHWID(rawHwid, currentHash);

    if (strcmp(storedHash, currentHash) == 0) {
        char expectedMC[128] = {};
        ComputeMachineCode(storedKey, currentHash, expectedMC);
        if (strcmp(storedMC, expectedMC) == 0) hwidMatch = true;
    }

    if (!hwidMatch) {
        char legacyHwid[512] = {};
        GetLegacyRawHWID(legacyHwid, 512);
        char legacyHash[128] = {};
        ComputeEncryptedHWID(legacyHwid, legacyHash);
        if (strcmp(storedHash, legacyHash) == 0) {
            char expectedMC[128] = {};
            ComputeMachineCode(storedKey, legacyHash, expectedMC);
            if (strcmp(storedMC, expectedMC) == 0) hwidMatch = true;
        }
    }

    if (!hwidMatch) return false;

    // ======================================================================
    // 伺服器端 HWID 比對（Layer 4）：向伺服器確認該金鑰的 HWID 是否匹配
    // 如果網路不可用 → 退回本機離線驗證（已通過）
    // 如果伺服器回應 HWID 不匹配 → 顯示提示並返回 false
    // ======================================================================
    {
        char storedToken[128] = {};
        ParseJsonString(buf, "session_token", storedToken, 128);

        // 建立 JSON 請求
        std::string json = "{";
        json += "\"key\":\"";           json += JsonEscape(storedKey);     json += "\",";
        json += "\"hwid_hash\":\"";     json += JsonEscape(storedHash);    json += "\",";
        json += "\"machine_code\":\"";  json += JsonEscape(storedMC);      json += "\"";
        if (storedToken[0]) {
            json += ",\"session_token\":\""; json += JsonEscape(storedToken); json += "\"";
        }
        json += "}";

        HINTERNET hSession = WinHttpOpen(L"1ynKeyCheck/2.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession) {
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

            HINTERNET hConnect = WinHttpConnect(hSession, KEY_SERVER_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (hConnect) {
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", HWID_VERIFY_PATH,
                    NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                if (hRequest) {
                    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
                    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

                    DWORD timeout = 8000;
                    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
                    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
                    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

                    const wchar_t* headers = L"Content-Type: application/json\r\n";
                    BOOL bResult = WinHttpSendRequest(hRequest, headers, (DWORD)wcslen(headers),
                        (LPVOID)json.c_str(), (DWORD)json.size(), (DWORD)json.size(), 0);

                    if (bResult) {
                        bResult = WinHttpReceiveResponse(hRequest, NULL);
                        if (bResult) {
                            DWORD statusCode = 0, statusSize = sizeof(statusCode);
                            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

                            char respBuf[2048] = {};
                            DWORD bytesRead = 0;
                            WinHttpReadData(hRequest, respBuf, sizeof(respBuf) - 1, &bytesRead);

                            if (statusCode == 200 && strstr(respBuf, "\"valid\":true")) {
                                // 伺服器確認 HWID 匹配 → 繼續啟動
                            } else {
                                // HWID 不匹配 → 顯示提示
                                WinHttpCloseHandle(hRequest);
                                WinHttpCloseHandle(hConnect);
                                WinHttpCloseHandle(hSession);
                                MessageBoxW(NULL,
                                    L"HWID \x4E0D\x5339\x914D\xFF01\n\n"
                                    L"\x60A8\x7684\x88DD\x7F6E\x8207\x4F3A\x670D\x5668\x8A18\x9304\x4E0D\x4E00\x81F4\x3002\n"
                                    L"\x8ACB\x524D\x5F80 Discord Bot \x6309\x4E0B\x3010\x91CD\x7F6E HWID\x3011\x6309\x9215\xFF0C\n"
                                    L"\x7136\x5F8C\x91CD\x65B0\x555F\x52D5\x7A0B\x5F0F\x3002",
                                    L"1yn AutoClick - HWID \x9A57\x8B49\x5931\x6557",
                                    MB_ICONERROR | MB_OK);
                                return false;
                            }
                        }
                    }
                    // 網路失敗 → 不阻止，退回本機驗證（已通過）
                    WinHttpCloseHandle(hRequest);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
        }
        // 網路不可用時不阻止，允許離線啟動
    }

    // ======================================================================
    // Google 試算表 HWID 比對（Layer 5）：向 GAS 查詢該金鑰的 HWID 資料
    // 網路不可用 → 顯示「請連線網際網路後再次嘗試」
    // 超過 20 秒無回應 → 顯示「請重新開啟應用程式」
    // HWID 不匹配 → 返回 false（顯示輸入金鑰 UI）
    // ======================================================================
    {
        std::string json = "{";
        json += "\"type\":\"hwid_query\",";
        json += "\"key\":\"";  json += JsonEscape(storedKey);  json += "\"";
        json += "}";

        bool networkOk = false;
        bool gotResponse = false;
        bool hwidMatched = false;
        bool timedOut = false;

        HINTERNET hSession = WinHttpOpen(L"1ynKeyCheck/2.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession) {
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

            HINTERNET hConnect = WinHttpConnect(hSession, L"script.google.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (hConnect) {
                networkOk = true;
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST",
                    L"/macros/s/AKfycbxFID2dQMjC5xK228bkORU9ZYXICwtfdJ7gFSuOA3Xe69bULbpN9uKdmSLT_9xECW6usw/exec",
                    NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                if (hRequest) {
                    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
                    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

                    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
                    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

                    DWORD timeout = 20000;  // 20 秒超時
                    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
                    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
                    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

                    const wchar_t* headers = L"Content-Type: application/json\r\n";
                    BOOL bResult = WinHttpSendRequest(hRequest, headers, (DWORD)wcslen(headers),
                        (LPVOID)json.c_str(), (DWORD)json.size(), (DWORD)json.size(), 0);

                    if (bResult) {
                        bResult = WinHttpReceiveResponse(hRequest, NULL);
                        if (bResult) {
                            gotResponse = true;
                            char respBuf[4096] = {};
                            DWORD bytesRead = 0;
                            WinHttpReadData(hRequest, respBuf, sizeof(respBuf) - 1, &bytesRead);

                            // 解析回應：比對 hwid 和 machine_code
                            char sheetHwid[256] = {};
                            char sheetMC[256] = {};
                            if (ParseJsonString(respBuf, "hwid", sheetHwid, 256) &&
                                ParseJsonString(respBuf, "machine_code", sheetMC, 256)) {
                                // 試算表 HWID 為空 = 已被重置，需要重新綁定
                                if (sheetHwid[0] == '\0' && sheetMC[0] == '\0') {
                                    hwidMatched = false;  // 需要重新輸入金鑰
                                } else if (strcmp(storedHash, sheetHwid) == 0 &&
                                           strcmp(storedMC, sheetMC) == 0) {
                                    hwidMatched = true;
                                }
                            }
                        } else {
                            timedOut = true;
                        }
                    } else {
                        // SendRequest 失敗可能是超時
                        DWORD err = GetLastError();
                        if (err == ERROR_WINHTTP_TIMEOUT) timedOut = true;
                        else networkOk = false;
                    }
                    WinHttpCloseHandle(hRequest);
                }
                WinHttpCloseHandle(hConnect);
            } else {
                networkOk = false;
            }
            WinHttpCloseHandle(hSession);
        } else {
            networkOk = false;
        }

        if (!networkOk) {
            MessageBoxW(NULL,
                L"\x8ACB\x9023\x7DDA\x7DB2\x969B\x7DB2\x8DEF\x5F8C\x518D\x6B21\x5617\x8A66",
                L"1yn AutoClick",
                MB_ICONWARNING | MB_OK);
            return false;
        }

        if (timedOut || (!gotResponse)) {
            MessageBoxW(NULL,
                L"\x8ACB\x91CD\x65B0\x958B\x555F\x61C9\x7528\x7A0B\x5F0F",
                L"1yn AutoClick",
                MB_ICONWARNING | MB_OK);
            return false;
        }

        if (!hwidMatched) {
            // HWID 不匹配或已被重置 → 回到輸入金鑰 UI
            return false;
        }
    }

    wchar_t storedKeyW[512] = {};
    MultiByteToWideChar(CP_UTF8, 0, storedKey, -1, storedKeyW, 512);

    wchar_t cmdLine[1024];
    wsprintfW(cmdLine, L"\"%s\" %s", clickerPath, storedKeyW);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        RegisterStartup();
        return true;
    }

    return false;
}

// ======================================================================
// WinMain
// ======================================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR lpCmd, int nShow) {
    (void)hPrev;
    (void)lpCmd;
    (void)nShow;

    // 初始化 COM（用於 CoCreateGuid）
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    // 已驗證過的裝置直接啟動（不顯示 UI）
    if (TryAutoLaunch()) {
        CoUninitialize();
        return 0;
    }

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

    if (!hwnd) { CoUninitialize(); return 1; }

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

    CoUninitialize();
    return (int)msg.wParam;
}