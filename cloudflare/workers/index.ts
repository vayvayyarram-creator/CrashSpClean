// moon-auth/src/index.ts
// Cloudflare Worker for Moon Cheat - Auth, Ping, DLL Distribution

import { Hono } from 'hono';
import { cors } from 'hono/cors';
import { Env } from './types';

const app = new Hono<{ Bindings: Env }>();

// CORS configuration
app.use('*', cors({
  origin: ['*'],
  allowMethods: ['GET', 'POST', 'OPTIONS'],
  allowHeaders: ['Content-Type', 'Authorization', 'X-HWID', 'X-Version', 'X-Build-Hash', 'X-Admin-Key'],
  maxAge: 86400,
}));

// In-line rate limiter (Worker-memory map)
// IP başına dakikada 60 istek limiti. Worker'ın kendi state'inde tracking.
const rateLimitBucket = new Map<string, { count: number; resetAt: number }>();
const rateLimitWindowMs = 60 * 1000;
const rateLimitMax = 60;

// Periyodik cleanup — her 5 dakikada bir eski entry'leri sil
let lastRateLimitCleanup = Date.now();
function rateLimitCleanup() {
  const now = Date.now();
  if (now - lastRateLimitCleanup < 5 * 60 * 1000) return;
  lastRateLimitCleanup = now;
  for (const [k, v] of rateLimitBucket.entries()) {
    if (now >= v.resetAt) rateLimitBucket.delete(k);
  }
}

app.use('*', async (c, next) => {
  if (c.req.method === 'OPTIONS') return next(); // CORS preflight skip
  rateLimitCleanup();

  // IP or HWID (auth endpoints) key
  const ip = c.req.header('cf-connecting-ip') || 'unknown';
  const hwid = c.req.header('X-HWID');
  const key = (c.req.path.startsWith('/auth') || c.req.path.startsWith('/ping')) && hwid
              ? `hwid:${hwid}:${c.req.path}`
              : `ip:${ip}`;

  const now = Date.now();
  let entry = rateLimitBucket.get(key);
  if (!entry || now >= entry.resetAt) {
    entry = { count: 0, resetAt: now + rateLimitWindowMs };
    rateLimitBucket.set(key, entry);
  }
  if (++entry.count > rateLimitMax) {
    return c.json({
      success: false,
      message: 'Rate limit exceeded. Retry after 60 seconds.',
      retry_after: Math.ceil((entry.resetAt - now) / 1000),
    }, 429);
  }

  await next();
});

// ============================================================
// TYPES & INTERFACES
// ============================================================
interface LicenseRecord {
  hwid: string;
  key: string;
  name: string;
  expires_at: number;
  max_devices: number;
  current_devices: number;
  created_at: number;
  status: 'active' | 'banned' | 'expired';
  last_ip: string;
  last_seen: number;
}

interface AuthRequest {
  hwid: string;
  key?: string;
  version: string;
  build_hash?: string;
}

interface AuthResponse {
  success: boolean;
  message: string;
  data?: {
    name: string;
    expires_at: number;
    download_url: string;
    version: string;
    build_hash: string;
    config?: string;
  };
}

interface PingRequest {
  hwid: string;
  version: string;
  build_hash: string;
  status: 'running' | 'injected' | 'error';
  game_pid?: number;
  game_name?: string;
  features?: string[];
}

interface PingResponse {
  success: boolean;
  message: string;
  data?: {
    update_available: boolean;
    latest_version: string;
    download_url: string;
    build_hash: string;
    message_of_day?: string;
  };
}

// ============================================================
// HELPER FUNCTIONS
// ============================================================
function generateSecureToken(length: number = 32): string {
  const array = new Uint8Array(length);
  crypto.getRandomValues(array);
  return Array.from(array, byte => byte.toString(16).padStart(2, '0')).join('');
}

function validateHWID(hwid: string): boolean {
  // HWID: alphanumeric, max 32 chars
  return /^[a-zA-Z0-9]{1,32}$/.test(hwid);
}

function validateVersion(version: string): boolean {
  // Semver-like: x.y.z or x.y.z-build
  return /^\d+\.\d+\.\d+(?:-[a-zA-Z0-9]+)?$/.test(version);
}

function validateBuildHash(hash: string): boolean {
  // 8-64 chars hex
  return /^[a-fA-F0-9]{8,64}$/.test(hash);
}

// ============================================================
// Webhook notifications — Discord embed notifications
// ============================================================
async function sendWebhook(c: any, eventType: string, data: Record<string, any>): Promise<void> {
  try {
    const webhooksResult = await c.env.DB.prepare(
      'SELECT url, events FROM webhooks WHERE enabled = 1'
    ).all();

    const webhooks = (webhooksResult.results || []) as Array<{ url: string; events: string }>;

    if (webhooks.length === 0) return;

    for (const wh of webhooks) {
      let events: string[] = [];
      try {
        events = JSON.parse(wh.events);
      } catch {
        events = [];
      }
      if (!events.includes('*') && !events.includes(eventType)) continue;

      // HMAC-signed body
      const body = JSON.stringify({
        event: eventType,
        timestamp: Date.now(),
        data,
      });

      // Discord-friendly embed (if URL is discord.com)
      if (wh.url.includes('discord.com/api/webhooks')) {
        const embed = {
          username: 'Moon Auth',
          embeds: [
            {
              title: `🔔 ${eventType}`,
              color: eventType.includes('failure') || eventType.includes('ban') ? 0xdc4646 : 0x50dc78,
              description: '```json\n' + JSON.stringify(data, null, 2).slice(0, 1900) + '\n```',
              timestamp: new Date().toISOString(),
            },
          ],
        };
        await fetch(wh.url, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(embed),
        });
      } else {
        await fetch(wh.url, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json', 'X-Webhook-Event': eventType },
          body,
        });
      }
    }
  } catch (e) {
    // Silent fail — webhook is non-critical
  }
}

// ============================================================
// Telegram notification (legacy fallback)
// ============================================================
async function sendTelegram(c: any, text: string): Promise<void> {
  try {
    const token = await c.env.KV_RATE_LIMIT_KV?.get?.('tg_bot_token');
    const chatId = await c.env.KV_RATE_LIMIT_KV?.get?.('tg_admin_chat_id');
    // not configured = silent fail
  } catch {}
}

function getClientIP(c: any): string {
  return c.req.header('cf-connecting-ip') || 
         c.req.header('x-forwarded-for')?.split(',')[0]?.trim() || 
         'unknown';
}

async function getLicenseFromDB(db: D1Database, hwid: string): Promise<LicenseRecord | null> {
  const result = await db.prepare(
    'SELECT * FROM licenses WHERE hwid = ?'
  ).bind(hwid).first<LicenseRecord>();
  return result || null;
}

async function getLicenseByKey(db: D1Database, key: string): Promise<LicenseRecord | null> {
  const result = await db.prepare(
    'SELECT * FROM licenses WHERE key = ?'
  ).bind(key).first<LicenseRecord>();
  return result || null;
}

async function updateLicenseLastSeen(db: D1Database, hwid: string, ip: string): Promise<void> {
  const now = Date.now();
  await db.prepare(
    'UPDATE licenses SET last_seen = ?, last_ip = ? WHERE hwid = ?'
  ).bind(now, ip, hwid).run();
}

async function incrementDeviceCount(db: D1Database, hwid: string): Promise<void> {
  await db.prepare(
    'UPDATE licenses SET current_devices = current_devices + 1 WHERE hwid = ?'
  ).bind(hwid).run();
}

async function decrementDeviceCount(db: D1Database, hwid: string): Promise<void> {
  await db.prepare(
    'UPDATE licenses SET current_devices = MAX(0, current_devices - 1) WHERE hwid = ?'
  ).bind(hwid).run();
}

async function logAuthAttempt(db: D1Database, hwid: string, ip: string, success: boolean, reason: string): Promise<void> {
  await db.prepare(
    'INSERT INTO auth_logs (hwid, ip, success, reason, timestamp) VALUES (?, ?, ?, ?, ?)'
  ).bind(hwid, ip, success ? 1 : 0, reason, Date.now()).run();
}

async function getLatestRelease(db: D1Database, env: Env): Promise<{ version: string; build_hash: string; url: string; } | null> {
  const result = await db.prepare(
    'SELECT version, build_hash, r2_key FROM releases WHERE is_latest = 1 AND status = "active" ORDER BY created_at DESC LIMIT 1'
  ).first<{ version: string; build_hash: string; r2_key: string }>();
  
  if (!result) return null;
  
  // Generate signed R2 URL (valid for 1 hour)
  const url = await generateSignedR2Url(env, result.r2_key, 3600);
  return { version: result.version, build_hash: result.build_hash, url };
}

async function generateSignedR2Url(env: Env, key: string, expiresIn: number): Promise<string> {
  // Production için presigned URL yolu:
  // Worker → R2.get() ile raw bytes oku → döndür (ILERIDE: R2 presigned URL ile redirect)
  //
  // Şimdilik basit yol: direkt R2 GET imzası.
  // Cloudflare R2 presigned URL'ler 2024'ten beri aşağıdaki API ile oluşturulabilir:
  //   env.R2.createPresignedUrl({ method: 'GET', key, expiresIn })
  // Ancak bu API base type'da yok — bu yüzden HTTP redirect yerine proxy indirme kullanıyoruz.
  //
  // PROXY MODE — indirmeyi Worker üzerinden yap. Bu yöntemle de R2'deki dosya HTTP üzerinden erişilebilir.
  return `/download/payload?key=${encodeURIComponent(key)}`;
}

async function generateR2DownloadResponse(env: Env, key: string): Promise<Response> {
  // Öncelikli yol: R2 binding (etkinse)
  if (env.R2_BUCKET) {
    const obj = await env.R2_BUCKET.get(key);
    if (obj) {
      const headers = new Headers();
      headers.set('Content-Type', 'application/octet-stream');
      headers.set('Content-Length', String(obj.size));
      headers.set('ETag', obj.httpEtag);
      headers.set('Cache-Control', 'public, max-age=300, immutable');
      headers.set('Content-Disposition', `attachment; filename="${key.split('/').pop()}"`);
      return new Response(obj.body, { headers });
    }
  }

  // Fallback: GitHub Releases proxy (R2 kapalıysa)
  // D1'den github_url al → 302 redirect et. Public repo için GitHub CDN ücretsiz.
  const releaseRow = await env.DB.prepare(
    'SELECT github_url, github_owner, github_repo, version, build_hash FROM releases WHERE r2_key = ? OR version = ? LIMIT 1'
  ).bind(key, key.split('/')[1] || '').first<any>();

  if (releaseRow) {
    let ghUrl: string;
    if (releaseRow.github_url) {
      ghUrl = releaseRow.github_url;
    } else if (releaseRow.github_owner && releaseRow.github_repo) {
      // Default pattern: releases/<version>/SpCrashReport.dll → github tag'e map
      const version = releaseRow.version || (key.match(/releases\/([^/]+)\//)?.[1]) || 'v1.0.0';
      const filename = key.split('/').pop() || 'SpCrashReport.dll';
      ghUrl = `https://github.com/${releaseRow.github_owner}/${releaseRow.github_repo}/releases/download/${version}/${filename}`;
    } else {
      ghUrl = `https://github.com/${env.GITHUB_OWNER || 'moon-private'}/${env.GITHUB_REPO || 'moon-private'}/releases/download/${key.split('/')[1] || 'v1.0.0'}/${key.split('/').pop() || 'SpCrashReport.dll'}`;
    }

    // HEAD request ile dosya var mı kontrol et — yoksa 404
    try {
      const head = await fetch(ghUrl, { method: 'HEAD', redirect: 'follow' });
      if (head.ok) {
        return Response.redirect(ghUrl, 302);
      }
    } catch {}
    // GitHub'dan da yoksa → 404
    return new Response(JSON.stringify({
      success: false,
      message: 'Payload not found in either R2 or GitHub Releases.',
      r2_key: key,
      github_url_checked: ghUrl,
    }), {
      status: 404,
      headers: { 'Content-Type': 'application/json' },
    });
  }

  // hiçbir şey yok → r2_key yanlış olabilir
  return new Response(JSON.stringify({
    success: false,
    message: 'Unknown release key.',
    r2_key: key,
  }), {
    status: 400,
    headers: { 'Content-Type': 'application/json' },
  });
}

// ============================================================
// ROUTES
// ============================================================

// Health check
app.get('/ping', async (c) => {
  return c.json({
    success: true,
    message: 'Moon Auth Service Online',
    timestamp: Date.now(),
    version: '1.0.0',
  });
});

// Payload download — R2 proxy (signed equivalent)
// HWID bazlı light auth kontrolü ile. Kullanım: GET /download/payload?key=...
// C++ client: download_url'den gelen /download/payload?key=... proxy'lenir.
app.get('/download/payload', async (c) => {
  const key = c.req.query('key') || '';

  // Simple path: route to latest release's R2 key
  if (!key || key === 'latest') {
    const release = await c.env.DB.prepare(
      'SELECT r2_key FROM releases WHERE is_latest = 1 AND status = "active" LIMIT 1'
    ).first<{ r2_key: string }>();
    if (!release) {
      return c.json({ success: false, message: 'No release available' }, 404);
    }
    return await generateR2DownloadResponse(c.env, release.r2_key);
  }

  // Specific key requested
  if (key.includes('..') || !key.startsWith('releases/')) {
    return c.json({ success: false, message: 'Invalid key' }, 400);
  }

  return await generateR2DownloadResponse(c.env, key);
});

// Authentication endpoint
app.post('/auth', async (c) => {
  const ip = getClientIP(c);
  const body = await c.req.json<AuthRequest>();
  const { hwid, key, version, build_hash } = body;

  // Validation
  if (!validateHWID(hwid)) {
    await logAuthAttempt(c.env.DB, hwid, ip, false, 'Invalid HWID format');
    return c.json<AuthResponse>({
      success: false,
      message: 'Invalid HWID format',
    }, 400);
  }

  if (!validateVersion(version)) {
    await logAuthAttempt(c.env.DB, hwid, ip, false, 'Invalid version format');
    return c.json<AuthResponse>({
      success: false,
      message: 'Invalid version format',
    }, 400);
  }

  let license: LicenseRecord | null = null;

  // First try HWID lookup (returning user)
  license = await getLicenseFromDB(c.env.DB, hwid);

  // If not found and key provided, try key activation (new user)
  if (!license && key) {
    license = await getLicenseByKey(c.env.DB, key);
    
    if (license && license.hwid !== hwid) {
      // Check device limit
      if (license.current_devices >= license.max_devices) {
        await logAuthAttempt(c.env.DB, hwid, ip, false, 'Device limit reached');
        return c.json<AuthResponse>({
          success: false,
          message: 'Device limit reached. Contact support.',
        }, 403);
      }
      
      // Bind HWID to this license
      await c.env.DB.prepare(
        'UPDATE licenses SET hwid = ?, current_devices = current_devices + 1, last_ip = ?, last_seen = ? WHERE key = ?'
      ).bind(hwid, ip, Date.now(), key).run();
      
      license.hwid = hwid;
      license.current_devices += 1;
      license.last_ip = ip;
      license.last_seen = Date.now();
    }
  }

  if (!license) {
    await logAuthAttempt(c.env.DB, hwid, ip, false, 'License not found');
    return c.json<AuthResponse>({
      success: false,
      message: 'Invalid license. Please check your key or HWID.',
    }, 401);
  }

  // Check status
  if (license.status === 'banned') {
    await logAuthAttempt(c.env.DB, hwid, ip, false, 'License banned');
    await sendWebhook(c, 'license.banned.access', {
      hwid, key: license.key, name: license.name, ip,
    });
    return c.json<AuthResponse>({
      success: false,
      message: 'License has been banned.',
    }, 403);
  }

  if (license.status === 'expired' || license.expires_at < Date.now()) {
    const wasActive = license.status === 'active';
    await c.env.DB.prepare(
      'UPDATE licenses SET status = "expired" WHERE hwid = ?'
    ).bind(hwid).run();

    await logAuthAttempt(c.env.DB, hwid, ip, false, 'License expired');
    if (wasActive) {
      await sendWebhook(c, 'license.expired', {
        hwid, key: license.key, name: license.name, ip,
        expired_at: license.expires_at,
      });
    }
    return c.json<AuthResponse>({
      success: false,
      message: 'License has expired.',
    }, 403);
  }

  // Update last seen
  await updateLicenseLastSeen(c.env.DB, hwid, ip);

  // Get latest release
  const release = await getLatestRelease(c.env.DB, c.env);
  
  if (!release) {
    await logAuthAttempt(c.env.DB, hwid, ip, false, 'No release available');
    return c.json<AuthResponse>({
      success: false,
      message: 'No release available. Contact administrator.',
    }, 503);
  }

  // Get user config
  const configResult = await c.env.DB.prepare(
    'SELECT config FROM user_configs WHERE hwid = ?'
  ).bind(hwid).first<{ config: string }>();

  await logAuthAttempt(c.env.DB, hwid, ip, true, 'Authentication successful');

  // Webhook: başarılı auth
  await sendWebhook(c, 'auth.success', {
    hwid,
    name: license.name,
    key: license.key,
    expires_at: license.expires_at,
    ip,
    version,
    build_hash,
  });

  return c.json<AuthResponse>({
    success: true,
    message: 'Authentication successful',
    data: {
      name: license.name,
      expires_at: license.expires_at,
      download_url: release.url,
      version: release.version,
      build_hash: release.build_hash,
      config: configResult?.config || '',
    },
  });
});

// License expire olunca DB update + webhook
async function markLicenseExpired(c: any, hwid: string, key: string, name: string): Promise<void> {
  await c.env.DB.prepare(
    `UPDATE licenses SET status='expired' WHERE hwid = ?`
  ).bind(hwid).run();
  await sendWebhook(c, 'license.expired', {
    hwid, key, name,
    timestamp: Date.now(),
  });
}

// Ping / heartbeat endpoint
app.post('/ping', async (c) => {
  const ip = getClientIP(c);
  const body = await c.req.json<PingRequest>();
  const { hwid, version, build_hash, status, game_pid, game_name, features } = body;

  if (!validateHWID(hwid) || !validateVersion(version)) {
    return c.json<PingResponse>({
      success: false,
      message: 'Invalid request format',
    }, 400);
  }

  // Verify license exists and is active
  const license = await getLicenseFromDB(c.env.DB, hwid);
  
  if (!license || license.status !== 'active' || license.expires_at < Date.now()) {
    return c.json<PingResponse>({
      success: false,
      message: 'License invalid or expired',
    }, 401);
  }

  // Update last seen
  await updateLicenseLastSeen(c.env.DB, hwid, ip);

  // Log ping
  await c.env.DB.prepare(
    `INSERT INTO ping_logs (hwid, ip, version, build_hash, status, game_pid, game_name, features, timestamp)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`
  ).bind(hwid, ip, version, build_hash, status, game_pid || 0, game_name || '', JSON.stringify(features || []), Date.now()).run();

  // Check for updates
  const release = await getLatestRelease(c.env.DB, c.env);
  const updateAvailable = release && (release.version !== version || release.build_hash !== build_hash);

  return c.json<PingResponse>({
    success: true,
    message: 'Pong',
    data: {
      update_available: updateAvailable || false,
      latest_version: release?.version || version,
      download_url: release?.url || '',
      build_hash: release?.build_hash || build_hash,
      message_of_day: 'Welcome to Moon',
    },
  });
});

// Get user config
app.get('/config/:hwid', async (c) => {
  const hwid = c.req.param('hwid');
  const ip = getClientIP(c);

  if (!validateHWID(hwid)) {
    return c.json({ success: false, message: 'Invalid HWID' }, 400);
  }

  const license = await getLicenseFromDB(c.env.DB, hwid);
  if (!license || license.status !== 'active') {
    return c.json({ success: false, message: 'Unauthorized' }, 401);
  }

  const config = await c.env.DB.prepare(
    'SELECT config FROM user_configs WHERE hwid = ?'
  ).bind(hwid).first<{ config: string }>();

  await updateLicenseLastSeen(c.env.DB, hwid, ip);

  return c.json({
    success: true,
    data: { config: config?.config || '{}' },
  });
});

// Update user config
app.post('/config/:hwid', async (c) => {
  const hwid = c.req.param('hwid');
  const ip = getClientIP(c);
  const { config } = await c.req.json<{ config: string }>();

  if (!validateHWID(hwid)) {
    return c.json({ success: false, message: 'Invalid HWID' }, 400);
  }

  const license = await getLicenseFromDB(c.env.DB, hwid);
  if (!license || license.status !== 'active') {
    return c.json({ success: false, message: 'Unauthorized' }, 401);
  }

  await c.env.DB.prepare(
    `INSERT INTO user_configs (hwid, config, updated_at) VALUES (?, ?, ?)
     ON CONFLICT(hwid) DO UPDATE SET config = ?, updated_at = ?`
  ).bind(hwid, config, Date.now(), config, Date.now()).run();

  await updateLicenseLastSeen(c.env.DB, hwid, ip);

  return c.json({ success: true, message: 'Config updated' });
});

// Get release info (for launcher version check)
app.get('/release/latest', async (c) => {
  const release = await getLatestRelease(c.env.DB, c.env);
  
  if (!release) {
    return c.json({ success: false, message: 'No release available' }, 404);
  }

  return c.json({
    success: true,
    data: {
      version: release.version,
      build_hash: release.build_hash,
      download_url: release.url,
      changelog: 'Moon v1.0 - Initial release',
      min_version: '1.0.0',
    },
  });
});

// Admin endpoints (protected by secret header)
app.get('/admin/stats', async (c) => {
  const adminKey = c.req.header('X-Admin-Key');
  if (adminKey !== c.env.ADMIN_SECRET) {
    return c.json({
      success: false,
      message: 'Unauthorized',
    }, 401);
  }

  const totalLicenses = await c.env.DB.prepare('SELECT COUNT(*) as count FROM licenses').first<{ count: number }>();
  const activeLicenses = await c.env.DB.prepare('SELECT COUNT(*) as count FROM licenses WHERE status = "active" AND expires_at > ?').bind(Date.now()).first<{ count: number }>();
  const totalAuthToday = await c.env.DB.prepare('SELECT COUNT(*) as count FROM auth_logs WHERE timestamp > ? AND success = 1').bind(Date.now() - 86400000).first<{ count: number }>();
  const uniqueUsersToday = await c.env.DB.prepare('SELECT COUNT(DISTINCT hwid) as count FROM auth_logs WHERE timestamp > ? AND success = 1').bind(Date.now() - 86400000).first<{ count: number }>();

  return c.json({
    success: true,
    data: {
      total_licenses: totalLicenses?.count || 0,
      active_licenses: activeLicenses?.count || 0,
      auth_today: totalAuthToday?.count || 0,
      unique_users_today: uniqueUsersToday?.count || 0,
    },
  });
});

// Admin webhook test
app.post('/admin/webhook/test', async (c) => {
  const adminKey = c.req.header('X-Admin-Key');
  if (adminKey !== c.env.ADMIN_SECRET) {
    return c.json({ success: false, message: 'Unauthorized' }, 401);
  }

  const body = await c.req.json().catch(() => ({} as any));
  const event = body.event || 'auth.success';
  await sendWebhook(c, event, {
    test: true,
    timestamp: Date.now(),
    admin: adminKey?.slice(0, 4) + '***',
    message: 'Bu bir test webhook bildirimidir.',
  });

  return c.json({
    success: true,
    message: `Test webhook sent: ${event}`,
  });
});

// Admin: extend/ban/unban a license
app.post('/admin/license/:hwid/action', async (c) => {
  const adminKey = c.req.header('X-Admin-Key');
  if (adminKey !== c.env.ADMIN_SECRET) {
    return c.json({ success: false, message: 'Unauthorized' }, 401);
  }

  const hwid = c.req.param('hwid');
  const reqBody = await c.req.json<{ action: 'ban' | 'unban' | 'extend' | 'revoke'; days?: number }>();
  const action = reqBody.action;

  if (!validateHWID(hwid)) {
    return c.json({ success: false, message: 'Invalid HWID' }, 400);
  }

  const lic = await getLicenseFromDB(c.env.DB, hwid);
  if (!lic) return c.json({ success: false, message: 'License not found' }, 404);

  switch (action) {
    case 'ban':
      await c.env.DB.prepare('UPDATE licenses SET status="banned" WHERE hwid=?').bind(hwid).run();
      await sendWebhook(c, 'license.banned', { hwid, key: lic.key, name: lic.name, ip: 'admin' });
      break;
    case 'unban':
      await c.env.DB.prepare('UPDATE licenses SET status="active" WHERE hwid=?').bind(hwid).run();
      break;
    case 'extend':
      const extraDays = (reqBody?.days ?? 30) as number;
      const newExp = Math.max(lic.expires_at, Date.now()) + extraDays * 86400000;
      await c.env.DB.prepare('UPDATE licenses SET expires_at=?, status="active" WHERE hwid=?').bind(newExp, hwid).run();
      break;
    case 'revoke':
      await c.env.DB.prepare('UPDATE licenses SET status="revoked" WHERE hwid=?').bind(hwid).run();
      break;
    default:
      return c.json({ success: false, message: 'Unknown action' }, 400);
  }

  return c.json({
    success: true,
    message: `License ${action}: ${hwid}`,
  });
});

// Admin: list latest N licenses
app.get('/admin/licenses', async (c) => {
  const adminKey = c.req.header('X-Admin-Key');
  if (adminKey !== c.env.ADMIN_SECRET) {
    return c.json({ success: false, message: 'Unauthorized' }, 401);
  }

  const limit = parseInt(c.req.query('limit') || '50', 10);
  const offset = parseInt(c.req.query('offset') || '0', 10);

  const r = await c.env.DB.prepare(
    'SELECT hwid, key, name, status, max_devices, current_devices, expires_at, created_at, last_seen, last_ip FROM licenses ORDER BY created_at DESC LIMIT ? OFFSET ?'
  ).bind(limit, offset).all();

  return c.json({
    success: true,
    data: r.results || [],
  });
});

export default app;