#!/usr/bin/env bash
# dev-setup.sh — Linux/macOS local Worker dev setup
set -euo pipefail

WORKER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$WORKER_DIR"

echo "==> [1/5] Checking required tools..."
for t in node npm; do
  if ! command -v "$t" &>/dev/null; then
    echo "✘ Missing: $t"
    exit 1
  fi
done
echo "    Node: $(node --version)"

if ! command -v wrangler &>/dev/null; then
  echo "==> [2/5] Installing wrangler (local)..."
  npm install --save-dev wrangler@^3.60.0
fi

echo "==> [3/5] Installing dependencies..."
npm install

echo "==> [4/5] Type-checking..."
npm run typecheck

echo "==> [5/5] Building bundle..."
npm run build

echo ""
echo "✔ Worker setup complete."
echo ""
echo "Next steps:"
echo "  1. Set env vars in .dev.vars:"
echo "     ADMIN_SECRET=your-secret"
echo "     JWT_SECRET=your-jwt-secret"
echo ""
echo "  2. Run local dev:"
echo "     wrangler dev"
echo ""
echo "  3. Deploy:"
echo "     wrangler deploy"
