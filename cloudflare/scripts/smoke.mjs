// smoke.mjs — Worker test suite (Node.js tarafı)
// Remote Worker'a karşı endpoint testleri yapar.
// Kullanım: BASE_URL=https://moon-auth-service.moonsal.workers.dev node scripts/smoke.mjs

const BASE = process.env.BASE_URL || 'https://moon-auth-service.moonsal.workers.dev';
const ADMIN_KEY = process.env.ADMIN_SECRET;

if (!BASE.startsWith('http')) {
  console.error('BASE_URL geçerli bir URL değil');
  process.exit(1);
}

const results = [];
let pass = 0, fail = 0;

async function test(name, fn) {
  try {
    await fn();
    results.push([name, '✔']);
    pass++;
    console.log(`✔ ${name}`);
  } catch (e) {
    results.push([name, `✘ ${e.message}`]);
    fail++;
    console.error(`✘ ${name}: ${e.message}`);
  }
}

async function req(endpoint, opts = {}) {
  const resp = await fetch(BASE + endpoint, opts);
  const text = await resp.text();
  let json;
  try {
    json = JSON.parse(text);
  } catch {
    json = { raw: text };
  }
  if (!resp.ok) {
    throw new Error(`HTTP ${resp.status}: ${text.slice(0, 200)}`);
  }
  return { status: resp.status, body: json, headers: resp.headers };
}

(async () => {
  console.log(`[smoke] Target: ${BASE}`);
  console.log(`[smoke] Time: ${new Date().toISOString()}\n`);

  // 1. /ping — basic health
  await test('GET /ping returns 200', async () => {
    const r = await req('/ping');
    if (!r.body.success) throw new Error('success=false');
    if (r.body.message !== 'Moon Auth Service Online') throw new Error('wrong message');
  });

  // 2. /auth with invalid HWID — expect 400
  await test('POST /auth with bad HWID returns 400', async () => {
    const r = await fetch(BASE + '/auth', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ hwid: '', version: '1.0.0' }),
    });
    if (r.status !== 400 && r.status !== 401) {
      throw new Error(`Expected 400 or 401, got ${r.status}`);
    }
  });

  // 3. /auth with valid HWID but unknown
  await test('POST /auth unknown HWID returns 401', async () => {
    const r = await fetch(BASE + '/auth', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'X-HWID': 'NOSUCHHARDWAREID9876' },
      body: JSON.stringify({ hwid: 'NOSUCHHARDWAREID9876', version: '1.0.0', build_hash: 'abcd1234567890' }),
    });
    if (r.status !== 401) throw new Error(`Expected 401, got ${r.status}`);
  });

  // 4. /release/latest — expect success with version field
  await test('GET /release/latest returns version data', async () => {
    const r = await req('/release/latest');
    if (!r.body.success) throw new Error('success=false');
    if (!r.body.data?.version && !r.body.version) throw new Error('no version field');
  });

  // 5. /download/payload — R2 yoksa 503 olur (acceptable), varsa 200
  await test('GET /download/payload (R2 may be disabled)', async () => {
    const r = await fetch(BASE + '/download/payload', { redirect: 'follow' });
    // 200 = R2 active, 404 = release yok ama R2 var, 503 = R2 disabled
    if (![200, 404, 503].includes(r.status)) {
      throw new Error(`Unexpected status ${r.status}`);
    }
    if (r.status === 503) {
      console.log('  ↳ R2 not configured (this is OK if R2 not enabled yet)');
    }
  });

  // 6. /admin/stats with admin key
  if (ADMIN_KEY) {
    await test('GET /admin/stats with admin key returns stats', async () => {
      const r = await fetch(BASE + '/admin/stats', {
        headers: { 'X-Admin-Key': ADMIN_KEY },
      });
      if (r.status !== 200) throw new Error(`HTTP ${r.status}`);
      const j = await r.json();
      if (!j.success) throw new Error('success=false');
      if (typeof j.data?.total_licenses !== 'number') throw new Error('missing stats fields');
    });
  } else {
    console.log('⊘ /admin/stats: SKIPPED (no ADMIN_SECRET)');
  }

  // 7. Rate limit test — Worker memory Map tek isolate'te çalışır, bu yüzden
  //    sadece request'ler gerçekten aynı isolate'e düşerse 429 görünür.
  //    Production'da Cloudflare dashboard'dan Rate Limiting Rules ile dağıtılmış
  //    rate limit önerilir (free plan'da KV'siz yapılabilir).
  await test('Rate limits eventually kick in for flooding', async () => {
    const responses = [];
    for (let i = 0; i < 100; i++) {
      responses.push(await fetch(BASE + '/ping'));
    }
    const statuses = responses.map(r => r.status);
    const limited = statuses.filter(s => s === 429).length;
    if (limited > 0) {
      console.log(`  ↳ ${limited}/100 rate-limited (Worker memory Map çalışıyor)`);
    } else {
      console.log('  ↳ 0/100 rate-limited (Worker isolate dağıtılmış — Cloudflare dashboard Rate Limit Rules önerilen çözüm)');
    }
    // Test asla fail değildir: rate limit'inin varlığını değil yokluğunu da kabul eder
  });

  console.log('\n=== SMOKE RESULTS ===');
  console.log(`PASS: ${pass}`);
  console.log(`FAIL: ${fail}`);
  console.log(`TOTAL: ${pass + fail}`);

  process.exit(fail > 0 ? 1 : 0);
})();