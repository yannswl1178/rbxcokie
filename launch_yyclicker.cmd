@echo off
chcp 65001 >nul 2>&1
title 1yn AutoClick - 金鑰啟動器
color 0A

echo.
echo  ╔══════════════════════════════════════════════╗
echo  ║                                              ║
echo  ║        1yn AutoClick - 金鑰啟動器            ║
echo  ║                                              ║
echo  ╚══════════════════════════════════════════════╝
echo.
echo  請輸入您的授權金鑰以啟動程式。
echo  金鑰可在 Discord 伺服器中獲取。
echo.
echo  ────────────────────────────────────────────────
echo.

set /p "KEY=  請輸入金鑰: "

if "%KEY%"=="" (
    echo.
    echo  [錯誤] 未輸入金鑰！
    echo.
    pause
    exit /b 1
)

echo.
echo  正在驗證金鑰...
echo.

:: 啟動 .exe 並傳入金鑰作為命令列參數
start "" "yy_clicker.exe" "%KEY%"

:: 等待 2 秒後關閉 cmd 視窗
timeout /t 2 /nobreak >nul
exit
