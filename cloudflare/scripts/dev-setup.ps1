# dev-setup.ps1 — Windows için Worker setup
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$workerDir = Resolve-Path "$scriptDir/.."
Set-Location $workerDir

Write-Host '==> [1/5] Checking required tools...' -ForegroundColor Cyan
foreach ($t in @('node','npm')) {
    if (-not (Get-Command $t -ErrorAction SilentlyContinue)) {
        Write-Host "✘ Missing: $t" -ForegroundColor Red
        exit 1
    }
}
Write-Host "    Node: $(node --version)"

if (-not (Get-Command wrangler -ErrorAction SilentlyContinue)) {
    Write-Host '==> [2/5] Installing wrangler (local)...' -ForegroundColor Cyan
    npm install --save-dev wrangler@^3.60.0
}

Write-Host '==> [3/5] Installing dependencies...' -ForegroundColor Cyan
npm install

Write-Host '==> [4/5] Type-checking...' -ForegroundColor Cyan
npm run typecheck

Write-Host '==> [5/5] Building bundle...' -ForegroundColor Cyan
npm run build

Write-Host ''
Write-Host '✔ Worker setup complete.' -ForegroundColor Green
Write-Host ''
Write-Host 'Next steps:'
Write-Host '  1. Set env vars in .dev.vars:'
Write-Host '     ADMIN_SECRET=your-secret'
Write-Host '     JWT_SECRET=your-jwt-secret'
Write-Host ''
Write-Host '  2. Run local dev:'
Write-Host '     wrangler dev'
Write-Host ''
Write-Host '  3. Deploy:'
Write-Host '     wrangler deploy'
