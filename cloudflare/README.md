# Moon Private - Cloudflare Deployment Setup

Cloudflare Workers + R2 + D1 altyapısını tamamlamak için gerekli adımlar:

## 1. Cloudflare D1 Oluştur

```bash
cd cloudflare
wrangler d1 create moon-auth
# Çıkan database_id'yi wrangler.toml'a yaz
# wrangler d1 execute moon-auth --local --file=./d1/migrations/0001_initial_schema.sql
```

## 2. Cloudflare R2 Oluştur

```bash
wrangler r2 bucket create moon-releases
# API token'da R2 read/write izni olmalı
```

## 3. Worker Deploy

```bash
# .dev.vars ile test et
echo 'CF_API_TOKEN=xxx' > .dev.vars
echo 'ADMIN_SECRET=xxx'  >> .dev.vars

npm install
wrangler dev             # local test
wrangler deploy          # prod'a push
```

## 4. GitHub Secrets Ekle

Repo Settings → Secrets:
- `CLOUDFLARE_API_TOKEN` — token
- `CLOUDFLARE_ACCOUNT_ID`
- `D1_DATABASE_ID`

## 5. Worker create (özellikler)

Her Worker ücretsiz plan için:
- 100,000 request/day (Workers free)
- 1,000k R2 read/request/month
- 10GB R2 storage
- 5M D1 read/day, 100k write/day

Moon için beklenen:
- Auth: ~50 req/kullanıcı/gün, 1k kullanıcı = 50k req — limit altında
- Ping: ~30 sn × 8 saat × 1k kullanıcı = 960k req — **PRO için upgrade**

PRO workers plan kullanmak mantıklı:
- Workers Paid: 10M req/ay dahil + sonra $0.50/M
- R2: 10M read/ay + 1M write/ay
- D1: 5GB + 5B row read/day, 50M write/day

## 6. Custom Domain Ekle

```toml
# wrangler.toml
[[routes]]
pattern = "api.moon.example.com/*"
zone_name = "example.com"
```

veya doğrudan `moon-auth.moon.example.com` Workers'ın kendi domainine bağlı.

## 7. Edge Cache

Worker'lar kendiliğinden edge'de çalışır. R2 download için
Cache-Control header koyarak CDN edge cache aktifleştirilir.

## Admin CLI ile Lisans Yönet

```bash
cd cloudflare
npm install

# Lisans ekle
node scripts/admin.js add-license ABCDE12345 "" "VIP User" 30

# Lisansları listele
node scripts/admin.js list-licenses

# Ban
node scripts/admin.js ban ABCDE12345

# Yeni release yükle
node scripts/admin.js upload-release v1.0.1 bin/Release/SpCrashReport.dll
```

Admin secrets env'de:
- `CF_API_TOKEN`
- `CF_ACCOUNT_ID`
- `D1_DATABASE_ID`
- `R2_ACCESS_KEY_ID`
- `R2_SECRET_ACCESS_KEY`
- `R2_BUCKET_NAME`