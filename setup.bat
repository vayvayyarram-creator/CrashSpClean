@echo off
:: ============================================================
::  setup.bat — Tek seferlik kurulum
::  Bundan sonra Spotify her acilisinda hile otomatik yukler.
:: ============================================================

setlocal
set "ROOT=%~dp0"
set "DLL=%ROOT%bin\Release\version.dll"
set "SPOTIFY_DIR=%APPDATA%\Spotify"

echo.
echo  ================================================
echo   Hile Kurulum
echo  ================================================
echo.

:: DLL var mi?
if not exist "%DLL%" (
    echo [HATA] version.dll bulunamadi.
    echo        Once Ctrl+Shift+B ile derleyin, sonra tekrar calistirin.
    echo.
    pause
    exit /b 1
)

:: Spotify klasoru var mi?
if not exist "%SPOTIFY_DIR%" (
    echo [HATA] Spotify klasoru bulunamadi: %SPOTIFY_DIR%
    echo        Spotify yuklu degil.
    echo.
    pause
    exit /b 1
)

:: Spotify'i kapat
echo [*] Spotify kapatiliyor...
taskkill /F /IM Spotify.exe >nul 2>&1
timeout /t 2 >nul

:: Kopyala
echo [*] Hile Spotify klasorune kopyalaniyor...
copy /Y "%DLL%" "%SPOTIFY_DIR%\version.dll" >nul
if errorlevel 1 (
    echo [HATA] Kopyalanamadi.
    pause
    exit /b 1
)
echo [OK] Kopyalandi: %SPOTIFY_DIR%\version.dll

:: Spotify'i yeniden ac
echo [*] Spotify yeniden aciliyor...
start "" "%SPOTIFY_DIR%\Spotify.exe"

echo.
echo  ================================================
echo   KURULUM TAMAMLANDI
echo.
echo   Artik her Spotify acilisinda hile otomatik
echo   yuklenecek. Ekstra bir sey yapmaniza gerek yok.
echo.
echo   Kullanim:
echo     - Spotify'i acin  (hile arka planda baslar)
echo     - FiveM'i acin    (hile otomatik baglanir)
echo     - FiveM kapanip tekrar acilirsa oto yeniden baglanir
echo     - END tusu = hileyi tamamen kapat
echo.
echo   Guncelleme icin:
echo     1. Ctrl+Shift+B (yeniden derle)
echo     2. setup.bat'i tekrar calistir
echo  ================================================
echo.
pause
