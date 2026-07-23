# MOON PRIVATE — v0.3

Sunucu altyapısı tamamen **Cloudflare Workers + R2 + D1 + GitHub Releases**'a taşındı.

## Mimari

```
                          Kullanıcı
                              │
                              ▼
                    ┌────────────────────┐
                    │   C++ Client       │
                    │  (External/Internal)│
                    └─────────┬──────────┘
                              │ HTTPS  (TLS 1.3)
                              ▼
        ┌─────────────────────────────────────────────┐
        │      Cloudflare Workers                     │
        │      moon-auth-service.moonsal.workers.dev          │
        │                                             │
        │  Routes:                                    │
        │    POST /auth         (lisans/HWID)          │
        │    POST /ping         (heartbeat)            │
        │    GET  /release/latest                      │
        │    POST /config/:hwid  (kullanıcı ayarı)    │
        │    GET  /download/payload  ← signed URL      │
        │    GET  /admin/stats   (admin key ile)      │
        └────┬───────────────────────┬───────────────┘
             │                       │
   ┌─────────▼─────────┐   ┌─────────▼──────────┐
   │ Cloudflare D1     │   │ Cloudflare R2       │
   │ (SQLite)          │   │ (Object Storage)    │
   │ - licenses        │   │ - SpCrashReport.dll │
   │ - releases        │   │ - sha256.txt        │
   │ - auth_logs       │   │ - (yeni release'ler)│
   │ - ping_logs       │   └──────────┬───────────┘
   │ - settings        │              │
   └───────────────────┘              │
                                      │  (fallback)
                             ┌────────▼───────────┐
                             │ GitHub Releases     │
                             │  SpCrashReport.dll  │
                             │  (via release.yml)  │
                             └────────────────────┘
```

## Free/Paid Tier Yeterliliği

| Servis       | Free Tier         | Beklenen kullanım (1k kullanıcı) | Gereken Tier |
|--------------|-------------------|---------------------------------|--------------|
| Workers      | 100K req/gün      | ~1M req/gün (ping x kullanıcı) | **Paid ($5/ay)** |
| R2           | 10GB + 1M req/ay  | ~10GB (releases) + 30M req/ay  | **Paid $0.015/GB-ay** |
| D1           | 5GB row + 5M read | ~50MB + 1M+ read/day           | **Free yeterli** |
| GitHub Rel.  | unlimited (soft)  | Free'de OK                     | Free |

Free tier yeterli olur **eğer** kullanıcı sayısı ~100'ün altında.
1k kullanıcı için Workers Paid plan gerekir (~$5/ay).

## Migrated Components

| Eski (VDS)                    | Yeni (Cloudflare)               |
|-------------------------------|----------------------------------|
| 93.88.201.224:242 (payload)   | moon-auth-service.moonsal.workers.dev/download/payload |
| 93.88.201.224:6067 (config)   | moon-auth-service.moonsal.workers.dev/config/:hwid     |
| 93.88.201.224:80 (auth)       | moon-auth-service.moonsal.workers.dev/auth              |
| Python server.py              | D1 `licenses` + `user_configs` tables            |
| SpCrashReport.dll VDS link    | R2 bucket `moon-releases/releases/vX.Y.Z/`       |
| (bakım yok)                   | GitHub Actions: release.yml, deploy-worker.yml   |

## Security

- **HTTPS only**: Tüm HTTP requestlerde `WINHTTP_FLAG_SECURE` (Cloudflare TLS).
- **HWID binding**: Lisans sadece 1 cihaza bağlanır (varsayılan).
- **Rate limiting**: Workers terfi — IP + HWID başına dakikada 60 request.
- **D1 audit trail**: auth_logs + ping_logs, her işlem timestamp'li.
- **R2 immutable**: Release dosyaları SHA-256 ile imzalanır.
- **WinHttp OpenRequest TLS 1.3**: tüm C++ loader'lar.

## Developer Deployment

```bash
# 1. D1 oluştur
wrangler d1 create moon-auth
# database_id yaz → wrangler.toml

# 2. R2 bucket oluştur
wrangler r2 bucket create moon-releases

# 3. Schema uygula
npm run db:migrate:prod

# 4. Worker deploy
npm run deploy

# 5. Lisans ekle (env: CF_API_TOKEN, etc)
node scripts/admin.js add-license MYHWID01 "" "Test User" 30

# 6. Release yükle
node scripts/admin.js upload-release v1.0.0 bin/Release/SpCrashReport.dll
```

## C++ Client Yapılandırması

`Cheat/CfEndpoints.hpp` dosyası tüm URL'leri merkeze alır:

```cpp
#define CF_WORKER_PRIMARY    L"moon-auth-service.moonsal.workers.dev"
constexpr const WORD PRIMARY_PORT = 443;
// HTTPS zorunlu
```

Custom domain kullanıyorsanız bu dosyayı güncelleyin — tüm loader/components otomatik takip eder.

## Bug Fixes (v0.2 → v0.3)

Tüm A-hatalar.txt maddeleri güncellendi. Detaylar için: `../A-hatalar.txt`.

## Repo Yapısı

```
/                              ← Proje kökü (C++ client)
├── Cheat/
│   ├── Source.cpp
│   ├── Config.hpp
│   ├── CfEndpoints.hpp        ← Cloudflare endpoints merkezi
│   ├── Stealth.hpp
│   └── ...
├── Loader/
├── WslLoader/
├── Service/
├── ExternalLoader/
├── InternalLoader/
├── Installer/
├── Uefi/
├── cloudflare/                ← Worker + DB + scripts
│   ├── workers/
│   │   ├── index.ts           ← Worker ana kodu
│   │   └── types.ts           ← TypeScript tipleri
│   ├── d1/
│   │   └── migrations/0001_initial_schema.sql
│   ├── wrangler.toml          ← bindings + vars
│   ├── package.json
│   ├── scripts/admin.js       ← Lisans/R2 CLI
│   └── README.md              ← deployment guide
├── bin/Release/                ← build çıktıları (git ignore)
├── .github/workflows/
│   ├── deploy-worker.yml       ← Worker sürekli entegrasyon
│   ├── release.yml            ← yeni tag → R2 upload + D1 güncelleme
│   └── health-check.yml       ← cron: 6 saatte bir kontrol
└── A-hatalar.txt               ← Bug list (güncellenmiş)
```