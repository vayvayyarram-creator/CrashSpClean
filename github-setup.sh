#!/usr/bin/env bash
# ============================================================
#  github-setup.sh — vayvayyarram-creator/CrashSp reposuna
#  Moon Private projesini setup eder.
#
#  Kullanım:
#  1. GitHub'da Personal Access Token (PAT) oluştur (Settings → Developer settings)
#     - scope: repo, workflow
#  2. TOKEN ve REMOTE_URL ayarla
#  3. ./github-setup.sh
# ============================================================

set -euo pipefail

# === AYARLAR ===
GITHUB_TOKEN="${GITHUB_TOKEN:-}"
GITHUB_REMOTE="${GITHUB_REMOTE:-https://github.com/vayvayyarram-creator/CrashSp.git}"
GITHUB_USER="${GITHUB_USER:-vayvayyarram-creator}"
GITHUB_EMAIL="${GITHUB_EMAIL:-vayvayyarram-creator@users.noreply.github.com}"

REPO_DIR="${REPO_DIR:-$(pwd)}"

echo "==> Repo dir:    $REPO_DIR"
echo "==> Remote URL:  $GITHUB_REMOTE"
echo "==> User:        $GITHUB_USER"
echo ""

# Token kontrol
if [ -z "$GITHUB_TOKEN" ]; then
    echo "✘ GITHUB_TOKEN unset. PAT al: https://github.com/settings/tokens"
    exit 1
fi

# Git yok kontrol
if ! command -v git &>/dev/null; then
    echo "✘ git yok. Install: https://git-scm.com"
    exit 1
fi

# Repo yu yeniden clientla
echo "==> Initializing repo..."
cd "$REPO_DIR"

if [ ! -d ".git" ]; then
    git init -b main
fi

git config user.name "$GITHUB_USER"
git config user.email "$GITHUB_EMAIL"

# Remote ayarla
if git remote get-url origin &>/dev/null; then
    git remote set-url origin "https://${GITHUB_USER}:${GITHUB_TOKEN}@${GITHUB_REMOTE#https://}"
else
    git remote add origin "https://${GITHUB_USER}:${GITHUB_TOKEN}@${GITHUB_REMOTE#https://}"
fi

# Dosyaları ekle
echo "==> Adding files (gitignore-respecting)..."
git add .

# Initial commit (veya release commit)
if git rev-parse --verify HEAD >/dev/null 2>&1; then
    echo "==> HEAD var, yeni commit..."
    git commit -m "chore: moon private development snapshot" || true
else
    git commit -m "init: Moon Private framework + Cloudflare backend

- C++ cheat source (Cheat/, Loader/, Service/, etc.)
- Cloudflare Workers auth service
- D1 schema + migrations
- GitHub Actions CI/CD (release.yml, deploy-worker.yml)
- Admin CLI (admin.mjs)
- Smoke test scripts
"
fi

echo "==> Initial commit OK"
echo ""

# Push
echo "==> Pushing to origin main..."
git push -u origin main --force

echo ""
echo "✔ Repository setup complete."
echo ""
echo "Şimdi GitHub Actions için gerekli secrets eklemelisin:"
echo "https://github.com/${GITHUB_USER}/CrashSp/settings/secrets/actions"
echo ""
echo "  - CLOUDFLARE_API_TOKEN (Cloudflare Dashboard → API Tokens)"
echo "  - CLOUDFLARE_ACCOUNT_ID (Cloudflare Dashboard → sağ panel)"
echo "  - D1_DATABASE_ID = c7d2290c-7db1-473b-8bb6-19a4b48242c2"
echo ""
echo "Tag oluşturarak release tetikleyebilirsin:"
echo "  git tag v1.0.0"
echo "  git push origin v1.0.0"
echo "veya workflow_dispatch ile Actions → Release Moon → Run workflow"
