// Quick test
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

const url = 'https://moon-auth-service.moonsal.workers.dev';

// Read dev vars file
const envFile = fs.readFileSync(path.join(__dirname, '..', '.dev.vars'), 'utf8');
const adminSecret = envFile.split('\n')
  .find(l => l.startsWith('ADMIN_SECRET='))
  .replace('ADMIN_SECRET=', '');

console.log('Admin secret:', adminSecret);

async function test() {
  // /ping
  console.log('\n[1] /ping');  const r1 = await fetch(url + '/ping').then(r => r.json());  console.log(' ', r1.message);

  // /release/latest
  console.log('\n[2] /release/latest');
  const r2 = await fetch(url + '/release/latest').then(r => r.json());
  console.log(' ', r2.data?.version);

  // /auth with TESTUSER01
  console.log('\n[3] /auth hwid=TESTUSER01');
  const r3 = await fetch(url + '/auth', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ hwid: 'TESTUSER01', version: '1.0.0', build_hash: 'init1234567' }),
  }).then(r => r.json());
  console.log(' ', r3.message, '→', r3.data?.name);

  // /admin/stats
  console.log('\n[4] /admin/stats (with admin secret)');
  const r4 = await fetch(url + '/admin/stats', { headers: { 'X-Admin-Key': adminSecret.trim() } });
  console.log('   status:', r4.status);
  const data4 = await r4.json();
  if (r4.ok) console.log('   data:', JSON.stringify(data4.data));
  else console.log('   err:', data4);

  // /admin/licenses
  console.log('\n[5] /admin/licenses');
  const r5 = await fetch(url + '/admin/licenses?limit=10', { headers: { 'X-Admin-Key': adminSecret.trim() } });
  console.log('   status:', r5.status);
  const data5 = await r5.json();
  if (r5.ok) {
    console.log('   licenses:');
    data5.data.forEach(l => {
      console.log('    -', l.hwid, l.name, l.status, new Date(l.expires_at).toISOString().slice(0,10));
    });
  }

  // /download/payload
  console.log('\n[6] /download/payload (R2 not active yet)');
  const r6 = await fetch(url + '/download/payload');
  console.log('   status:', r6.status);
  console.log('   body:', (await r6.text()).slice(0, 200));

  // rate limit test
  console.log('\n[7] Rate limit test (rapid 70 calls)');
  let limited = 0;
  for (let i = 0; i < 70; i++) {
    const r = await fetch(url + '/ping');
    if (r.status === 429) limited++;
  }
  console.log('   rate-limited responses:', limited);

  console.log('\n=== DONE ===');
}

test().catch(console.error);
