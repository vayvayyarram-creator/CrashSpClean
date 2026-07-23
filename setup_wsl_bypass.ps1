# ============================================================
#  WSL Bypass Setup — Task 1
#  WSL-master özel derlemesini sistem WSL'nin ÖNÜNE koyar.
#  PowerShell profil dosyasına eklenir → her oturumda aktif olur.
# ============================================================

param(
    [string]$WslMasterPath = "C:\Users\tron\Downloads\WSL-master\WSL-master"
)

# --- Olası derleme çıktı dizinleri ---
$buildCandidates = @(
    "$WslMasterPath\build\x64\Release",
    "$WslMasterPath\build\Release",
    "$WslMasterPath\out\Release",
    "$WslMasterPath\x64\Release",
    "$WslMasterPath\bin\Release"
)

$customWsl = $null
foreach ($dir in $buildCandidates) {
    $candidate = Join-Path $dir "wsl.exe"
    if (Test-Path $candidate) {
        $customWsl = $candidate
        Write-Host "[+] Ozel wsl.exe bulundu: $customWsl" -ForegroundColor Green
        break
    }
}

if (-not $customWsl) {
    Write-Host "[!] Derlenmiş wsl.exe bulunamadı." -ForegroundColor Yellow
    Write-Host "    WSL-master'ı derlemek için:" -ForegroundColor Cyan
    Write-Host "    1. Visual Studio 2022 ile WSL-master açın" -ForegroundColor Cyan
    Write-Host "    2. Release x64 olarak derleyin" -ForegroundColor Cyan
    Write-Host "    3. Bu scripti tekrar çalıştırın" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "    Alternatif: mevcut sistem WSL override'ı için wrapper kurulumu yapılıyor..." -ForegroundColor Yellow

    # Wrapper dizini oluştur
    $wrapperDir = "$env:LOCALAPPDATA\WSLBypass"
    if (-not (Test-Path $wrapperDir)) {
        New-Item -ItemType Directory -Path $wrapperDir -Force | Out-Null
    }

    # Wrapper PowerShell script → sistem WSL'yi çağırır (gelecekte özel binary eklenince otomatik değişir)
    $wrapperPs1 = @'
# WSL Bypass Wrapper — sistem wsl.exe'yi çağırır
# Ozel wsl.exe derlenince bu dosya güncellenir
$args2 = $args
& "$env:SystemRoot\System32\wsl.exe" @args2
'@
    $wrapperPs1 | Out-File -FilePath "$wrapperDir\wsl.ps1" -Encoding utf8

    # PATH'e ekle (kullanıcı ortam değişkeni — kalıcı)
    $userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
    if ($userPath -notlike "*$wrapperDir*") {
        [Environment]::SetEnvironmentVariable("PATH", "$wrapperDir;$userPath", "User")
        Write-Host "[+] $wrapperDir PATH'e eklendi" -ForegroundColor Green
    }
    $customWsl = "$wrapperDir\wsl.ps1"
}

# PowerShell profiline function ekle
$profilePath = $PROFILE.CurrentUserAllHosts
if (-not (Test-Path (Split-Path $profilePath))) {
    New-Item -ItemType Directory -Path (Split-Path $profilePath) -Force | Out-Null
}
$profileContent = if (Test-Path $profilePath) { Get-Content $profilePath -Raw } else { "" }

$funcBlock = @"

# ── WSL Bypass (Moon Private) ─────────────────────────────────
function wsl {
    `$customWsl = '$customWsl'
    if (Test-Path `$customWsl) {
        & `$customWsl @args
    } else {
        & "`$env:SystemRoot\System32\wsl.exe" @args
    }
}
# ─────────────────────────────────────────────────────────────
"@

if ($profileContent -notlike "*WSL Bypass (Moon Private)*") {
    Add-Content -Path $profilePath -Value $funcBlock -Encoding utf8
    Write-Host "[+] PowerShell profiline WSL bypass fonksiyonu eklendi: $profilePath" -ForegroundColor Green
} else {
    Write-Host "[=] WSL bypass zaten profilde mevcut" -ForegroundColor Cyan
}

# Mevcut oturum için de aktifleştir
function global:wsl {
    $customWslLocal = $customWsl
    if (Test-Path $customWslLocal) {
        & $customWslLocal @args
    } else {
        & "$env:SystemRoot\System32\wsl.exe" @args
    }
}

Write-Host ""
Write-Host "[✓] WSL bypass aktif." -ForegroundColor Green
Write-Host "    'wsl --version' ile test edebilirsiniz." -ForegroundColor Cyan
