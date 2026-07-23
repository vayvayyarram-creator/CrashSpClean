@echo off
:: Discord klasöründeki version.dll'i sil + monitor'ü durdur

:: Discord temizle
set "DISCORD_BASE=%LOCALAPPDATA%\Discord"
set "DISCORD_APP="
for /f "tokens=*" %%d in ('dir /b /ad "%DISCORD_BASE%\app-*" 2^>nul') do (
    set "DISCORD_APP=%%d"
)
if not "%DISCORD_APP%"=="" (
    if exist "%DISCORD_BASE%\%DISCORD_APP%\version.dll" (
        del /F /Q "%DISCORD_BASE%\%DISCORD_APP%\version.dll"
        echo [OK] version.dll silindi.
    ) else (
        echo [!] Discord klasöründe version.dll yok.
    )
)

:: Başlangıç kısayolunu sil
set "SHORTCUT=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\DiscordHelper.vbs"
if exist "%SHORTCUT%" (
    del /F /Q "%SHORTCUT%"
    echo [OK] Başlangıç kısayolu silindi.
)

:: Monitor PowerShell'i durdur
taskkill /F /FI "IMAGENAME eq powershell.exe" /FI "WINDOWTITLE eq " >nul 2>&1
echo [OK] Monitor durduruldu.

echo.
echo Temizlik tamamlandi. Discord ve hile normal calisir.
pause
