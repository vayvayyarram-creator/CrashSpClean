@echo off
:: ============================================================
::  inject.bat — Tek komutla kur ve inject et
::
::  Kullanim:
::    inject.bat          -> Spotify'a inject et
::    inject.bat install  -> Sadece Spotify klasorune kopyala
::                          (bir sonraki Spotify acilisinda aktif olur)
:: ============================================================

setlocal
set "ROOT=%~dp0"
set "DLL=%ROOT%bin\Release\version.dll"
set "SPOTIFY_DIR=%APPDATA%\Spotify"

:: ── DLL var mi? ─────────────────────────────────────────
if not exist "%DLL%" (
    echo.
    echo [HATA] DLL bulunamadi: %DLL%
    echo        Once Ctrl+Shift+B ile derleyin.
    echo.
    pause
    exit /b 1
)

:: ── Spotify klasorune KOPYALAMIYORUZ ───────────────────
:: (Sideload Spotify'i cokturuyor — sadece runtime inject kullaniyoruz)

:: ── Spotify acik mi? ────────────────────────────────────
tasklist /FI "IMAGENAME eq Spotify.exe" 2>nul | find /I "Spotify.exe" >nul
if errorlevel 1 (
    echo.
    echo [!] Spotify calisiyor degil.
    echo     Spotify'i acin, sonra tekrar calistirin.
    echo     VEYA: inject.bat install ^(bir sonraki acilista aktif olur^)
    echo.
    pause
    exit /b 1
)

:: ── PowerShell ile inject et ─────────────────────────────
echo [*] Spotify'a inject ediliyor...
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%inject.ps1" -DllPath "%DLL%"

if errorlevel 1 (
    echo.
    echo [HATA] Inject basarisiz.
    echo        Yonetici olarak calistirmayi deneyin ^(sag tik → Yönetici olarak calistir^)
    echo.
    pause
    exit /b 1
)

echo.
echo  Kullanim:
echo    - FiveM'i acin, hile otomatik baglanir
echo    - FiveM'den cikip tekrar girerseniz hile oto yeniden baglanir
echo    - END tusuna basin = hileyi tamamen kapat
echo.
timeout /t 3 >nul
exit /b 0
