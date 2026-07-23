# github-setup.ps1 — vayvayyarram-creator/CrashSp reposuna push
#
# Kullanım (kullanıcının PAT'ı gerekli):
#   $env:GITHUB_TOKEN = 'ghp_xxxxxxxxxxxxxxxxxxxx'
#   .\github-setup.ps1
#
# PAT al: https://github.com/settings/tokens (scope: repo, workflow)

$ErrorActionPreference = 'Stop'

$token = $env:GITHUB_TOKEN
$remote = $env:GITHUB_REMOTE
$user = $env:GITHUB_USER
$email = $env:GITHUB_EMAIL

if (-not $token) { $token = 'YOUR_TOKEN_HERE' }
if (-not $remote) { $remote = 'https://github.com/vayvayyarram-creator/CrashSp.git' }
if (-not $user)  { $user  = 'vayvayyarram-creator' }
if (-not $email) { $email = 'vayvayyarram-creator@users.noreply.github.com' }

$repoDir = if ($env:REPO_DIR) { $env:REPO_DIR } else { (Get-Location).Path }
Set-Location $repoDir

Write-Host "==> Repo dir: $repoDir" -ForegroundColor Cyan
Write-Host "==> Remote:   $remote" -ForegroundColor Cyan

if ($token -eq 'YOUR_TOKEN_HERE') {
    Write-Host ""
    Write-Host "✘ GITHUB_TOKEN ayarlanmamış. Şunu çalıştır:" -ForegroundColor Red
    Write-Host "    `$env:GITHUB_TOKEN = 'ghp_xxxxxxxxxxxx'" -ForegroundColor Yellow
    Write-Host "    .\github-setup.ps1" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Veya PAT oluştur: https://github.com/settings/tokens" -ForegroundColor Cyan
    exit 1
}

# git yok kontrol
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host "✘ git kurulu değil. Install: https://git-scm.com" -ForegroundColor Red
    exit 1
}

# Repo init
if (-not (Test-Path ".git")) {
    Write-Host "==> Initializing repo..." -ForegroundColor Cyan
    git init -b main
}

git config user.name $user
git config user.email $email

# Remote ayarla
$authRemote = $remote -replace '^https://', "https://${user}:${token}@"
$existing = git config --get remote.origin.url 2>$null
if ($existing) {
    git remote set-url origin $authRemote
} else {
    git remote add origin $authRemote
}

# Add
Write-Host "==> Adding files (gitignore-respecting)..." -ForegroundColor Cyan
git add .

# Commit
Write-Host "==> Committing..." -ForegroundColor Cyan
$msg = @"
init: Moon Private framework + Cloudflare backend

- C++ cheat source (Cheat/, Loader/, Service/, etc.)
- Cloudflare Workers auth service at moon-auth-service.moonsal.workers.dev
- D1 schema + migrations (DB: c7d2290c-...)
- GitHub Actions CI/CD (release.yml, deploy-worker.yml)
- Admin CLI (admin.mjs)
- Smoke test scripts
"@
git commit -m $msg

# Push
Write-Host "==> Pushing to origin main..." -ForegroundColor Cyan
git push -u origin main --force 2>&1 | Select-Object -First 10

Write-Host ""
Write-Host "✔ Repository setup complete." -ForegroundColor Green
Write-Host ""
Write-Host "GitHub Settings → Secrets ekle:" -ForegroundColor Yellow
Write-Host "  - CLOUDFLARE_API_TOKEN"
Write-Host "  - CLOUDFLARE_ACCOUNT_ID = 513c7c6743bf89656c87bb9febfc3d2b"
Write-Host "  - D1_DATABASE_ID = c7d2290c-7db1-473b-8bb6-19a4b48242c2"
Write-Host ""
Write-Host "Tag ile release tetikle:" -ForegroundColor Cyan
Write-Host "  git tag v1.0.0"
Write-Host "  git push origin v1.0.0"
