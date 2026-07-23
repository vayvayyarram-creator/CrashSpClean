# 🌙 Moon Private

> FiveM/GTA V için dış (external) hile altyapısı. Gelişmiş anti-cheat bypass ve modern sunucu altyapısı.

> ⚠️ **Bu yazılım tamamen ticari özellikler içeren, kapalı devre bir hile sistemidir.**
> **Yalnızca güvenlik araştırması ve kontrollü/izinli ortamların eğitim amaçlı kullanımı içindir.**

---

## 📁 Proje Yapısı

```
.
├── Cheat/                  ← Ana hile mantığı (SpCrashReport.dll)
│   ├── Source.cpp
│   ├── Cheat.hpp           ← ESP, Aimbot, Magic Bullet vs.
│   ├── License.hpp
│   ├── WebServer.hpp       ← Localhost Web UI (ayarlar)
│   ├── WsClient.hpp        ← WebSocket sunucu menüsü
│   ├── Config.hpp
│   ├── CfEndpoints.hpp     ← Yeni: Cloudflare endpoint
│   └── ...
├── ExternalLoader/         ← MoonLoader.exe — kendi başına çalışan launcher
├── InternalLoader/         ← MoonInternal.exe — manuel map launcher
├── Loader/                 ← version.dll — Chrome/Spotify sideload
├── Service/                ← crVaultSvc.dll — Windows LocalSystem service
├── WslLoader/              ← wsl.exe replacement
├── Installer/              ← setup.exe
├── Uefi/                   ← UEFI bootkit
│
├── cloudflare/             ← 🆕 Tamamen yeni Cloudflare altyapısı
│   ├── workers/index.ts    # Worker logic
│   ├── d1/migrations/0001_initial_schema.sql
│   ├── scripts/admin.mjs   # Yönetim CLI
│   ├── wrangler.toml       # D1/R2/KV bindings
│   └── README.md
│
├── .github/workflows/      ← 🆕 CI/CD
│   ├── deploy-worker.yml   # Worker deploy
│   ├── release.yml         # Build → R2 upload → D1 record
│   └── health-check.yml    # Cron sağlık kontrolü
│
├── A-hatalar.txt           ← Bug list (v0.3)
├── CHANGELOG.md            ← Sürüm notları
└── bin/Release/            ← Build çıktıları (git ignore)
```

### 🚀 Hızlı Bakış

| Modül | Derlenen | Ne Yapar |
|-------|----------|----------|
| `SpCrashReport.dll`      | CheatPayload    | FiveM'e inject edilen gerçek hile |
| `MoonLoader.exe`         | ExternalLoader  | Kullanıcı dostu GUI launcher (XOR şifreli payload indirir) |
| `MoonInternal.exe`       | InternalLoader  | Manual map launcher (disksiz çalışır) |
| `version.dll`            | Loader          | Chrome/Spotify sideload yöntemiyle DLL hijacking |
| `crVaultSvc.dll`         | Service         | svchost üzerinden LocalSystem ile çalışır |
| `wsl.exe`                | WslLoader       | Orijinal wsl.exe'yi değiştirir; arka planda cheat yükler |
| `setup.exe`              | Installer       | Windows kurulumu, iz bırakmaz |

---

## 🔧 Build

### Gereksinimler
- **MSVC v143** (Visual Studio 2022 veya Build Tools)
- **CMake** 3.20+
- **Ninja**
- **Windows SDK 10.0**

### Derleme

```cmd
:: Release build
build.bat

:: Veya temizden
build.bat rebuild

:: Debug
build.bat debug

:: Clean
build.bat clean
```

Çıktılar `bin/Release/`'a yazılır.

---

## 🌐 Cloudflare Backend

Bu proje tamamen **Cloudflare Workers + R2 + D1** mimarisine geçirildi.

```
Kullanıcı  →  C++ Loader  →  Cloudflare Workers  →  D1 (lisans DB)
                                       ↓
                                     R2 (payload .dll)
```

### Configuration

`cloudflare/wrangler.toml` — D1 ID ve R2 bucket bağlantısı.

**API Endpoints:**

| Method | Path | Amaç |
|--------|------|------|
| POST   | `/auth` | Lisans doğrulama |
| POST   | `/ping` | Heartbeat + update check |
| GET    | `/ping` | Health check |
| GET    | `/release/latest` | En son sürüm bilgisi |
| GET    | `/download/payload` | DLL indir (R2 proxy) |
| GET    | `/config/:hwid` | Kullanıcı ayarı |
| POST   | `/config/:hwid` | Ayar kaydet |
| POST   | `/admin/license/:hwid/action` | Admin: ban/unban/extend |
| GET    | `/admin/licenses` | Admin: lisans listesi |
| GET    | `/admin/stats` | Admin: istatistik |
| POST   | `/admin/webhook/test` | Test webhook |

### Deployment

İlk sefer:

```bash
# 1. D1 database oluştur
wrangler d1 create moon-auth
# database_id'yi wrangler.toml'a yaz

# 2. R2 bucket oluştur
wrangler r2 bucket create moon-releases

# 3. Schema uygula
cd cloudflare
npm install
npm run db:migrate:prod

# 4. Worker deploy
npm run deploy

# 5. Test lisansı oluştur
cp .env.example .env  # env variables doldur
node scripts/admin.mjs add-license TESTUSER01
```

GitHub Actions otomatik deploy için repo'ya şu secrets ekleyin:

- `CLOUDFLARE_API_TOKEN`
- `CLOUDFLARE_ACCOUNT_ID`
- `D1_DATABASE_ID`
- `R2_ACCESS_KEY_ID`
- `R2_SECRET_ACCESS_KEY`

### Admin CLI Örnekleri

```bash
# Lisans ekleme
node scripts/admin.mjs add-license MYHWID01

# Mevcut lisans 30 gün uzatma
node scripts/admin.mjs extend MYHWID01 30

# Ban
node scripts/admin.mjs ban MYHWID01

# Yeni release upload
node scripts/admin.mjs upload-release v1.0.1 bin/Release/SpCrashReport.dll

# İstatistikler
node scripts/admin.mjs stats
```

### Remote Smoke Test

```bash
BASE_URL=https://moon-auth-service.moonsal.workers.dev \
ADMIN_SECRET=your-admin-secret \
node scripts/smoke.mjs
```

---

## 🐛 Bilinen Buglar

Yok — `A-hatalar.txt`'deki tüm maddeler v0.3'te çözüldü.

---

## 📄 Lisans

Bu proje **kapalı kaynak** ve **ticari** bir yazılımdır. Dağıtımı, tersine mühendisliği ve yeniden kullanımı yasaktır.

---

## 🤝 Katkı

Bu repo **özel depodur**. Yetkisiz çekme (clone) işlemi kabul edilmez.

**Maintainer:** `Tron`

---
