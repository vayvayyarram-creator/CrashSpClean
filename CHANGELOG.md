# Changelog — Moon Private

Tüm değişiklikler kronolojik sırayla burada.

## [1.0.0] — Temmuz 2026

### ✨ Yeni (Added)
- **Cloudflare Workers auth servisi** — VDS tamamen kaldırıldı.
  - `/auth` POST → HWID/lisans doğrulama
  - `/ping` POST → heartbeat + otomatik update bildirimi
  - `/download/payload` → R2 proxy (immutable cache)
  - `/release/latest` → version info
  - `/config/:hwid` (GET/POST) → user config sync
  - `/admin/stats`, `/admin/licenses`, `/admin/license/:hwid/action` → admin
  - `/admin/webhook/test` → webhook test endpoint
- **Cloudflare R2** ile DLL dağıtımı (immutable, SHA256 imzalı)
- **Cloudflare D1 (SQLite)** ile lisans/HWID yönetimi
  - 9 tablo: licenses, user_configs, auth_logs, ping_logs, releases, admin_users, admin_sessions, feature_flags, webhooks, settings
- **Cloudflare Workers** rate limiter (60 req/min, per-IP + per-HWID)
- **GitHub Actions** CI/CD:
  - `deploy-worker.yml` → Worker her push'ta deploy
  - `release.yml` → GitHub Release → MSVC build → R2 upload + D1 update
  - `health-check.yml` → 6 saatte bir cron kontrol
- **Webhook notifications** — Discord embed, lisans.expired, lisans.banned, vs.
- **Admin CLI** (`scripts/admin.mjs`):
  - add-license, list-licenses, ban, unban, extend, upload-release, stats
- **Smoke test** (`scripts/smoke.mjs`) — 7 senaryoyu remote endpoint'te çalıştırır
- **Bootstrap script** (`scripts/dev-setup.{sh,ps1}`) — worker local setup
- `.gitignore`, `.gitattributes` (UTF-8, CRLF corr) kuralları
- `Cheat/CfEndpoints.hpp` — merkezi Cloudflare endpoint tanımı

### 🔄 Değişen (Changed)
- Tüm VDS IP (`93.88.201.224:242`, `93.88.201.224:6067`, `93.88.201.224:80`) → `moon-auth-service.moonsal.workers.dev` (HTTPS only)
- `Python server.py` → silindi (D1'e migrate edildi)
- `Server fud/` klasörü → `cloudflare/releases/` olarak adlandırıldı
- Tüm C++ `WinHttpOpenRequest` çağrılarına `WINHTTP_FLAG_SECURE` flag eklendi
- Tüm C++ kodda `FUD → Moon` yeniden adlandırma:
  - `FUDLoader.exe` → `MoonLoader.exe`
  - `FUDInternal.exe` → `MoonInternal.exe`
  - "Hitmare Bypass" → "Moon Private" (UI title, watermark)
  - License key: `FUD-XXXX` → `MOON-XXXX`
- `WslLoader/wsl_cheatload.c` — HTTPS + WINHTTP_FLAG_SECURE
- `Loader/loader.cpp` (Chrome sideload) — HTTPS endpoint
- `monitor.ps1` — TLS 1.2+12 zorlaması
- `Service/svc_dll.cpp` — GoodbyeDPI bug fix (5)
- `setup_wsl_bypass.ps1` — "FUD Private" → "Moon Private"
- `.claude/settings.local.json` — path ref'leri

### 🐛 Düzeltilen (Fixed)
A-hatalar.txt'deki tüm maddeler v1.0.0'a güncellendi:
- ✓ [#1] WSL bypass — `setup_wsl_bypass.ps1` + `WslLoader/wsl_cheatload.c`
- ✓ [#2] FPS drops — Distance culling 300m²+ skip, adaptive sleep
- ✓ [#3] IMGUI → Pure GDI (kaldırıldı)
- ✓ [#4] Damage Boost eklendi (Cheat/Cheat.hpp:DamageBoost)
- ✓ [#5] Magic Bullet — `g_caveMutex` ile thread race fix
- ✓ [#6] ESP lag — `MaxESPDistance` filter (default 400m)
- ✓ [#7] Player List — Web menu tab + `/api/players` endpoint
- ✓ [#8] Teleport — PlayerList → TeleportTo(predefined)
- ✓ [#9] GUI.hpp — kaldırıldı (sadece web menu)
- ✓ [#10] Unlock Car — vehicle lock state bit toggle
- ✓ [#11] Low Damage — ters damage multiplier
- ✓ [#12] Respawn — revive patch
- ✓ [#13] **GoodbyeDPI restart fix** — `StartGoodbyeDPI()` idempotent

### 📦 Yeni Dosyalar

```
/cloudflare/
├── workers/
│   ├── index.ts           # Auth/ping/admin/list/release/download/health
│   └── types.ts           # TypeScript tipler
├── d1/
│   ├── migrations/0001_initial_schema.sql
│   └── seed.sql           # test/license verileri
├── scripts/
│   ├── admin.mjs          # CLI: lisans yönet
│   ├── smoke.mjs          # Remote endpoint test
│   ├── dev-setup.sh       # Linux/macOS
│   └── dev-setup.ps1      # Windows
├── wrangler.toml          # Worker bindings
├── package.json
├── tsconfig.json
├── README.md
├── ARCHITECTURE.md
└── .env.example

/.github/workflows/
├── deploy-worker.yml      # Worker auto-deploy + D1 migrate
├── release.yml            # MSVC build → R2 → D1
└── health-check.yml       # 6h cron

/Cheat/
└── CfEndpoints.hpp        # Merkezi endpoint config
```

### 🔥 Kaldırılan (Removed)
- `Server fud/server.py` (Python auth server)
- `Server fud/license/` (legacy VDS license dosyaları)
- `Loader/FUDPrivate.dll`, `Loader/FUDExternal.exe` (eski adlandırma)
- `Cheat/Gui.hpp` artık kullanılmıyor (web menu ile değiştirildi)

## [0.3] — Önceki (Bahar 2026, Hitmare Bypass)
- VDS 93.88.201.224:242 bazlı mimari
- Sideload yöntemiyle Spotify/Chrome injection
- Python config server
- D1/R2 yok, GitHub Actions yok
- FUD Loader/adı kullanılıyordu
