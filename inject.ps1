# ============================================================
#  inject.ps1 — version.dll'i calisirken Spotify'a inject et
#  Kullanim: inject.bat tarafindan cagirilir
# ============================================================

param(
    [string]$DllPath = "$PSScriptRoot\bin\Release\version.dll"
)

# ── DLL var mi? ─────────────────────────────────────────────
if (-not (Test-Path $DllPath)) {
    Write-Host "[HATA] DLL bulunamadi: $DllPath" -ForegroundColor Red
    Write-Host "       Once 'build.bat' ile derleyin." -ForegroundColor Yellow
    exit 1
}

$fullDllPath = (Resolve-Path $DllPath).Path
Write-Host "[*] DLL: $fullDllPath" -ForegroundColor Cyan

# ── Spotify process'i bul ───────────────────────────────────
$spotify = Get-Process -Name "Spotify" -ErrorAction SilentlyContinue |
           Where-Object { $_.MainWindowHandle -ne 0 } |
           Select-Object -First 1

if (-not $spotify) {
    # MainWindow olmayan Spotify instance'i dene
    $spotify = Get-Process -Name "Spotify" -ErrorAction SilentlyContinue |
               Select-Object -First 1
}

if (-not $spotify) {
    Write-Host "[HATA] Spotify calisiyor degil. Once Spotify'i acin." -ForegroundColor Red
    exit 1
}

Write-Host "[*] Spotify bulundu. PID: $($spotify.Id)" -ForegroundColor Green

# ── Zaten inject edilmis mi kontrol et ──────────────────────
$modules = $spotify.Modules | Where-Object { $_.FileName -like "*version.dll*" }
foreach ($m in $modules) {
    if ($m.FileName -like "*Spotify*") {
        Write-Host "[!] DLL zaten inject edilmis (Spotify klasorundan yuklenmis)." -ForegroundColor Yellow
        Write-Host "    Sideload aktif, tekrar inject gerekmez." -ForegroundColor Yellow
        exit 0
    }
}

# ── Windows API tanimlari ─────────────────────────────────
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class WinInject {
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr VirtualAllocEx(IntPtr proc, IntPtr addr, uint size, uint allocType, uint protect);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool WriteProcessMemory(IntPtr proc, IntPtr addr, byte[] buf, uint size, out IntPtr written);

    [DllImport("kernel32.dll", CharSet=CharSet.Ansi, SetLastError=true)]
    public static extern IntPtr GetProcAddress(IntPtr module, string name);

    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr GetModuleHandleW(string name);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr CreateRemoteThread(IntPtr proc, IntPtr attr, uint stackSize, IntPtr startAddr, IntPtr param, uint flags, out IntPtr threadId);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern uint WaitForSingleObject(IntPtr handle, uint timeout);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool VirtualFreeEx(IntPtr proc, IntPtr addr, uint size, uint freeType);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr handle);
}
"@ -ErrorAction SilentlyContinue

try {
    $PROCESS_ALL_ACCESS = 0x1F0FFF
    $MEM_COMMIT_RESERVE = 0x3000
    $PAGE_READWRITE     = 0x04
    $MEM_RELEASE        = 0x8000

    # Process ac
    $hProc = [WinInject]::OpenProcess($PROCESS_ALL_ACCESS, $false, $spotify.Id)
    if ($hProc -eq [IntPtr]::Zero) {
        throw "OpenProcess basarisiz. Hata: $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }

    # DLL yolunu process memory'ye yaz
    $pathBytes = [System.Text.Encoding]::Unicode.GetBytes($fullDllPath + "`0")
    $allocAddr = [WinInject]::VirtualAllocEx($hProc, [IntPtr]::Zero, $pathBytes.Length, $MEM_COMMIT_RESERVE, $PAGE_READWRITE)
    if ($allocAddr -eq [IntPtr]::Zero) {
        throw "VirtualAllocEx basarisiz."
    }

    $written = [IntPtr]::Zero
    $ok = [WinInject]::WriteProcessMemory($hProc, $allocAddr, $pathBytes, $pathBytes.Length, [ref]$written)
    if (-not $ok) {
        throw "WriteProcessMemory basarisiz."
    }

    # LoadLibraryW adresini al
    $k32     = [WinInject]::GetModuleHandleW("kernel32.dll")
    $llwAddr = [WinInject]::GetProcAddress($k32, "LoadLibraryW")
    if ($llwAddr -eq [IntPtr]::Zero) {
        throw "LoadLibraryW adresi alinamadi."
    }

    # Uzak thread olustur → LoadLibraryW(dllPath)
    $threadId = [IntPtr]::Zero
    $hThread  = [WinInject]::CreateRemoteThread($hProc, [IntPtr]::Zero, 0, $llwAddr, $allocAddr, 0, [ref]$threadId)
    if ($hThread -eq [IntPtr]::Zero) {
        throw "CreateRemoteThread basarisiz."
    }

    Write-Host "[*] Inject ediliyor..." -ForegroundColor Cyan
    [WinInject]::WaitForSingleObject($hThread, 5000) | Out-Null

    # Temizle
    [WinInject]::VirtualFreeEx($hProc, $allocAddr, 0, $MEM_RELEASE) | Out-Null
    [WinInject]::CloseHandle($hThread) | Out-Null
    [WinInject]::CloseHandle($hProc)   | Out-Null

    Write-Host "[OK] Inject tamamlandi! FiveM'i acabilirsiniz." -ForegroundColor Green

} catch {
    Write-Host "[HATA] $_" -ForegroundColor Red
    exit 1
}
