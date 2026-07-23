// Quick admin tool — D1'e lisans ekler, R2'ye release yükler.
// Kullanım:
//   node scripts/admin.mjs add-license HWID key name days
//   node scripts/admin.mjs upload-release v1.0.0 bin/Release/SpCrashReport.dll
//   node scripts/admin.mjs list-licenses
//   node scripts/admin.mjs ban HWID
//   node scripts/admin.mjs extend HWID 30

import { S3Client, PutObjectCommand } from '@aws-sdk/client-s3';
import fs from 'node:fs';
import crypto from 'node:crypto';
import 'dotenv/config';

const CF_ACCOUNT_ID = process.env.CF_ACCOUNT_ID;
const CF_API_TOKEN = process.env.CF_API_TOKEN;
const D1_DB_ID = process.env.D1_DATABASE_ID;
const R2_BUCKET = process.env.R2_BUCKET_NAME || 'moon-releases';
const R2_ACCESS_KEY = process.env.R2_ACCESS_KEY_ID;
const R2_SECRET_KEY = process.env.R2_SECRET_ACCESS_KEY;

if (!CF_ACCOUNT_ID || !CF_API_TOKEN || !D1_DB_ID) {
  console.error('CF_ACCOUNT_ID, CF_API_TOKEN ve D1_DATABASE_ID env variable gerekli.');
  console.error('.env.example dosyasını .env olarak kopyala.');
  process.exit(1);
}

const CF_API = `https://api.cloudflare.com/client/v4/accounts/${CF_ACCOUNT_ID}/d1/database/${D1_DB_ID}/query`;

async function d1Query(sql, params = []) {
  const resp = await fetch(CF_API, {
    method: 'POST',
    headers: {
      'Authorization': `Bearer ${CF_API_TOKEN}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ sql, params }),
  });
  const j = await resp.json();
  if (!j.success) {
    throw new Error('D1 error: ' + JSON.stringify(j.errors) + '\nSQL: ' + sql);
  }
  return j.result;
}

function generateLicenseKey() {
  const bytes = crypto.randomBytes(12);
  const hex = bytes.toString('hex').toUpperCase();
  return `MOON-${hex.slice(0,4)}-${hex.slice(4,8)}-${hex.slice(8,12)}-${hex.slice(12,16)}`;
}

async function addLicense(hwid, key, name, days) {
  if (!hwid) throw new Error('HWID gerekli');
  if (!key || key.startsWith('--') || key === '') key = generateLicenseKey();
  if (!name) name = 'User';
  const ts = Date.now();
  const expires = ts + (days || 30) * 86400000;
  try {
    await d1Query(`
      INSERT INTO licenses (hwid, key, name, status, max_devices, current_devices, expires_at, created_at, last_seen)
      VALUES (?, ?, ?, 'active', 1, 0, ?, ?, ?)
    `, [hwid, key, name, expires, ts, ts]);
  } catch (err) {
    if (String(err.message).includes('UNIQUE')) {
      console.log(`[!] HWID zaten mevcut, güncelleniyor: ${hwid}`);
      await d1Query(`
        UPDATE licenses SET key=?, name=?, expires_at=?, status='active' WHERE hwid=?
      `, [key, name, expires, hwid]);
    } else {
      throw err;
    }
  }
  console.log(`[+] License added: ${key}`);
  console.log(`    HWID:      ${hwid}`);
  console.log(`    Name:      ${name}`);
  console.log(`    Expires:   ${new Date(expires).toISOString()}`);
}

async function listLicenses(limit = 50) {
  const r = await d1Query(`SELECT hwid, key, name, status, expires_at FROM licenses ORDER BY created_at DESC LIMIT ?`, [limit]);
  const rows = (r && r[0] && r[0].results) || [];
  if (rows.length === 0) {
    console.log('Henüz lisans yok.');
    return;
  }
  const formatted = rows.map(r => ({
    ...r,
    expires_at: new Date(r.expires_at).toISOString().slice(0, 10),
  }));
  console.table(formatted);
}

async function ban(hwid) {
  const r = await d1Query(`UPDATE licenses SET status='banned' WHERE hwid=? RETURNING key, name`, [hwid]);
  if (!r || r[0]?.results?.length === 0) {
    console.log(`[!] HWID bulunamadı: ${hwid}`);
    return;
  }
  console.log(`[+] Banned: ${hwid} (${r[0].results[0].name})`);
}

async function extend(hwid, days) {
  const exp = Date.now() + (days || 30) * 86400000;
  const r = await d1Query(
    `UPDATE licenses SET expires_at=?, status='active' WHERE hwid=? RETURNING key, name`,
    [exp, hwid]
  );
  if (!r || r[0]?.results?.length === 0) {
    console.log(`[!] HWID bulunamadı: ${hwid}`);
    return;
  }
  console.log(`[+] Extended: ${hwid} → +${days}d → ${new Date(exp).toISOString().slice(0,10)}`);
}

async function unban(hwid) {
  const r = await d1Query(`UPDATE licenses SET status='active' WHERE hwid=? RETURNING key`, [hwid]);
  if (!r || r[0]?.results?.length === 0) {
    console.log(`[!] HWID bulunamadı: ${hwid}`);
    return;
  }
  console.log(`[+] Unbanned: ${hwid}`);
}

async function uploadRelease(version, dllPath) {
  if (!fs.existsSync(dllPath)) {
    console.error('DLL bulunamadı:', dllPath);
    process.exit(1);
  }
  if (!R2_ACCESS_KEY || !R2_SECRET_KEY) {
    throw new Error('R2 credentials eksik (R2_ACCESS_KEY_ID, R2_SECRET_ACCESS_KEY)');
  }

  const buf = fs.readFileSync(dllPath);
  const size = buf.length;
  const sha256 = crypto.createHash('sha256').update(buf).digest('hex');
  const r2Key = `releases/${version}/SpCrashReport.dll`;

  const r2 = new S3Client({
    region: 'auto',
    endpoint: `https://${CF_ACCOUNT_ID}.r2.cloudflarestorage.com`,
    credentials: {
      accessKeyId: R2_ACCESS_KEY,
      secretAccessKey: R2_SECRET_KEY,
    },
  });

  await r2.send(new PutObjectCommand({
    Bucket: R2_BUCKET,
    Key: r2Key,
    Body: buf,
    ContentType: 'application/octet-stream',
    Metadata: {
      'sha256': sha256,
      'version': version,
    },
  }));
  console.log(`[+] Uploaded ${r2Key}`);
  console.log(`    Size:   ${size.toLocaleString()} bytes`);
  console.log(`    SHA256: ${sha256}`);

  // D1'e release kaydı
  await d1Query(`
    INSERT INTO releases (version, build_hash, r2_key, file_size, sha256, changelog, status, is_latest, min_client_version, created_at, released_at)
    VALUES (?, ?, ?, ?, ?, 'Uploaded via admin.mjs', 'active', 1, '1.0.0', ?, ?)
    ON CONFLICT(version) DO UPDATE SET r2_key=excluded.r2_key, file_size=excluded.file_size, sha256=excluded.sha256, is_latest=1, released_at=excluded.released_at
  `, [version, sha256.slice(0,12), r2Key, size, sha256, Date.now(), Date.now()]);

  // Önceki latest'i deprecated işaretle
  await d1Query(`UPDATE releases SET is_latest=0 WHERE version != ?`, [version]);
  console.log(`[+] D1 marked v${version} as latest (previous deprecated)`);
}

async function stats() {
  const ts = Date.now();
  const totals = await d1Query(`SELECT
    COUNT(*) as total,
    SUM(CASE WHEN status='active' AND expires_at > ? THEN 1 ELSE 0 END) as active,
    SUM(CASE WHEN status='banned' THEN 1 ELSE 0 END) as banned,
    SUM(CASE WHEN status='expired' THEN 1 ELSE 0 END) as expired
    FROM licenses`, [ts]);
  const last24hAuth = await d1Query(`SELECT COUNT(*) as cnt FROM auth_logs WHERE timestamp > ? AND success = 1`, [ts - 86400000]);
  const last24hPing = await d1Query(`SELECT COUNT(*) as cnt FROM ping_logs WHERE timestamp > ?`, [ts - 86400000]);

  const t = totals?.[0]?.results?.[0] || {};
  console.log('=== Moon Auth Stats ===');
  console.log(`Total licenses:  ${t.total || 0}`);
  console.log(`  active:        ${t.active || 0}`);
  console.log(`  banned:        ${t.banned || 0}`);
  console.log(`  expired:       ${t.expired || 0}`);
  console.log(`Auth (24h):      ${last24hAuth?.[0]?.results?.[0]?.cnt || 0}`);
  console.log(`Ping (24h):      ${last24hPing?.[0]?.results?.[0]?.cnt || 0}`);
}

const cmd = process.argv[2];
const args = process.argv.slice(3);

(async () => {
  try {
    switch (cmd) {
      case 'add-license':
        await addLicense(args[0], args[1], args[2], parseInt(args[3] || '30', 10));
        break;
      case 'list-licenses':
        await listLicenses(parseInt(args[0] || '50', 10));
        break;
      case 'ban':
        await ban(args[0]);
        break;
      case 'unban':
        await unban(args[0]);
        break;
      case 'extend':
        await extend(args[0], parseInt(args[1] || '30', 10));
        break;
      case 'upload-release':
        await uploadRelease(args[0], args[1]);
        break;
      case 'stats':
        await stats();
        break;
      default:
        console.log('Usage: npm run admin -- <command> [args]');
        console.log('       node scripts/admin.mjs <command>');
        console.log('');
        console.log('Commands:');
        console.log('  add-license <hwid> [key] [name] [days]   Lisans ekler (varsayılan 30 gün)');
        console.log('  upload-release <version> <dll-path>     R2\'ye DLL yükler + D1 günceller');
        console.log('  list-licenses [limit]                   Son N lisansı gösterir');
        console.log('  ban <hwid>                              Lisansı banlar');
        console.log('  unban <hwid>                            Ban\'ı kaldırır');
        console.log('  extend <hwid> <days>                    Expire süresini uzatır');
        console.log('  stats                                   Veritabanı istatistikleri');
    }
  } catch (e) {
    console.error('[X]', e.message);
    process.exit(1);
  }
})();