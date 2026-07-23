// ============================================================
//  C++ tarafı için Cloudflare Worker endpoint listesi
//  primary URL yanında backup'lar sıralı.
//  Worker proxy → Cloudflare R2'den indirir.
//  Worker API offline olursa GitHub Releases release.yml
//  aracılığıyla yayınlanmış SpCrashReport.dll'i direkt
//  Github CDN'inden indirir.
// ============================================================

#pragma once

// Master worker URL — Workers.dev subdomain. Custom domain varsa override.
#define CF_WORKER_PRIMARY    L"moon-auth-service.moonsal.workers.dev"

// GitHub Releases backup — releases.yml workflow tarafından SpCrashReport.dll
// push'lanır. URL pattern:
//   https://github.com/<owner>/<repo>/releases/download/v1.2.3/SpCrashReport.dll
//   https://github.com/<owner>/<repo>/releases/download/v1.2.3/SpCrashReport.dll.sha256
//
// Aşağıdaki Owner/Repo doldur, GitHub tag'leri Worker'dan gerçek versiyonla senkron tut.
// Worker /release/latest endpoint {
//
//   { "data": { "version": "v1.2.3", "github_url": "https://github.com/...", "download_url": "https://moon-auth.../api/download/payload" }
//
//   ...
// }
//
// Bu yapıyı kullanarak C++ client fallback yapar:
//   1. Worker /api/auth → access token + primary download_url
//   2. primary download_url → fetch_ok? indir. yoksa:
//   3. response'da github_url varsa direkt GitHub Releases'den indir.

namespace cf {

constexpr const wchar_t* PRIMARY_HOST     = CF_WORKER_PRIMARY;
constexpr const WORD      PRIMARY_PORT     = 443;        // HTTPS only
constexpr const wchar_t* AUTH_PATH         = L"/auth";
constexpr const wchar_t* PING_PATH         = L"/ping";
constexpr const wchar_t* PAYLOAD_PATH      = L"/download/payload";
constexpr const wchar_t* VERSION_PATH      = L"/release/latest";

// Header'lar:
constexpr const wchar_t* USER_AGENT        = L"Moon-Client/1.0";
constexpr const wchar_t* HWID_HEADER       = L"X-HWID";
constexpr const wchar_t* VERSION_HEADER    = L"X-Version";
constexpr const wchar_t* BUILD_HASH_HEADER = L"X-Build-Hash";

}  // namespace cf
