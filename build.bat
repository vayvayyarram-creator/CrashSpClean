@echo off
:: ============================================================
::  Moon Private - Build Script
::  Kullanim:
::    build.bat          -> Release build
::    build.bat debug    -> Debug build
::    build.bat clean    -> Temizle
::    build.bat rebuild  -> Cache sil + temizden Release build
:: ============================================================

setlocal

:: ---- MSVC vcvars64.bat ara (VS 2022 veya Build Tools) ----
set VCVARS=

:: Build Tools 2022  (IDE olmadan sadece compiler - ONCELIK)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    goto :found
)

:: VS 2022 Community
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    goto :found
)

:: VS 2022 Professional
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    goto :found
)

:: VS 2022 Enterprise
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    goto :found
)

:: Hic biri bulunamazsa
echo.
echo [ERROR] MSVC derleyicisi bulunamadi!
echo.
echo Cozum: asagidaki linkten "Build Tools for Visual Studio 2022" indir:
echo   https://aka.ms/vs/17/release/vs_BuildTools.exe
echo.
echo Kurulum sirasinda su workload'u sec:
echo   [x] Desktop development with C++
echo.
echo Kurulum bittikten sonra bu scripti tekrar calistir.
echo.
pause
exit /b 1

:found
echo [*] MSVC bulundu: %VCVARS%
call "%VCVARS%"

:: ---- Ninja var mi kontrol et ----
where ninja >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Ninja bulunamadi. Build Tools kurulumunda
    echo         "CMake tools for Windows" secenegini isaretledin mi?
    echo         Veya: https://github.com/ninja-build/ninja/releases
    pause
    exit /b 1
)

:: ---- CMake var mi kontrol et ----
where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] CMake bulunamadi.
    echo         Build Tools kurulumunda "CMake tools for Windows" secenegini
    echo         isaretledin mi? Veya: https://cmake.org/download/
    pause
    exit /b 1
)

:: ---- Build tipi ----
set BUILD_TYPE=Release
if /i "%1"=="debug"   set BUILD_TYPE=Debug
if /i "%1"=="clean"   goto :clean
if /i "%1"=="rebuild" goto :rebuild

:: ---- Build type degisti mi kontrol et ----
:: CMakeCache varsa icindeki CMAKE_BUILD_TYPE ile karsilastir
set NEED_CONFIGURE=0
if not exist "build\CMakeCache.txt" (
    set NEED_CONFIGURE=1
) else (
    :: Onceki build tipini oku
    for /f "tokens=2 delims==" %%A in ('findstr /i "CMAKE_BUILD_TYPE:STRING" "build\CMakeCache.txt" 2^>nul') do set CACHED_TYPE=%%A
    if /i not "%CACHED_TYPE%"=="%BUILD_TYPE%" (
        echo [*] Build type degisti ^(%CACHED_TYPE% ^→ %BUILD_TYPE%^) - cache siliniyor...
        rmdir /s /q build 2>nul
        set NEED_CONFIGURE=1
    )
)

:: ---- CMake configure (gerekirse) ----
if "%NEED_CONFIGURE%"=="1" (
    echo [*] CMake configure ^(%BUILD_TYPE%^)...
    cmake -S . -B build -G Ninja ^
          -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
          -DCMAKE_C_COMPILER=cl ^
          -DCMAKE_CXX_COMPILER=cl
    if errorlevel 1 (
        echo.
        echo [FAIL] Configure basarisiz.
        pause
        exit /b 1
    )
)

goto :dobuild

:rebuild
:: Temizden build — cache sil, Release
set BUILD_TYPE=Release
echo [*] Cache siliniyor (rebuild)...
if exist build rmdir /s /q build
echo [*] CMake configure ^(Release^)...
cmake -S . -B build -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_C_COMPILER=cl ^
      -DCMAKE_CXX_COMPILER=cl
if errorlevel 1 (
    echo.
    echo [FAIL] Configure basarisiz.
    pause
    exit /b 1
)

:dobuild
:: ---- Build ----
echo [*] Derleniyor ^(%BUILD_TYPE%^)...
cmake --build build --parallel
if errorlevel 1 (
    echo.
    echo [FAIL] Derleme basarisiz.
    pause
    exit /b 1
)

:: ---- Eski build kalintilerini temizle (artik bu isimde target yok) ----
for %%F in (
    "bin\%BUILD_TYPE%\MoonPrivate.dll"
    "bin\%BUILD_TYPE%\NullSyntax.exe"
    "bin\%BUILD_TYPE%\WinscreW.exe"
    "bin\%BUILD_TYPE%\MoonExternal.exe"
    "bin\%BUILD_TYPE%\crVaultSvc.dll"
) do (
    if exist %%F del /q %%F
)

:: ---- SpCrashReport.dll'yi server klasorune otomatik kopyala ----
set SRVDLL=cloudflare\releases\SpCrashReport.dll
if exist "bin\%BUILD_TYPE%\SpCrashReport.dll" (
    copy /y "bin\%BUILD_TYPE%\SpCrashReport.dll" "%SRVDLL%" >nul 2>&1
    if errorlevel 1 (
echo [OK] SpCrashReport.dll ^→ %SRVDLL%  ^(Cloudflare R2'ye yuklenmeye hazir^)
    else (
        echo [WARN] SpCrashReport.dll R2 klasorune kopyalanamadi.
        echo        Manuel kopyala: bin\%BUILD_TYPE%\SpCrashReport.dll ^→ %SRVDLL%
    )
)

echo.
echo [OK] Ciktilar: bin\%BUILD_TYPE%\
echo        SpCrashReport.dll  ^→ ^(otomatik: cloudflare\releases\^)
echo        MoonInternal.exe   ^→ kullaniciya gonder
echo        MoonLoader.exe      ^→ alternatif launcher
echo        version.dll        ^→ Chrome klasorune koy
echo        setup.exe          ^→ kurulum EXE
echo        wsl.exe            ^→ System32 loader
echo.
goto :end

:clean
echo [*] Temizleniyor...
if exist build rmdir /s /q build
if exist bin   rmdir /s /q bin
echo [OK] Temizlendi.

:end
pause
endlocal
