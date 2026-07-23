// debug test
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

const url = 'https://moon-auth-service.moonsal.workers.dev';

(async () => {
  // /admin/ping (no auth)
  console.log('\n[1] /admin/ping (no auth):');
  const r1 = await (await fetch(url + '/admin/ping')).json();
  console.log(JSON.stringify(r1.data, null, 2));

  // /admin/stats with real secret from .dev.vars
  const envFile = fs.readFileSync(path.join(__dirname, '..', '.dev.vars'), 'utf8');
  const realSecret = envFile.split('\n')
    .find(l => l.startsWith('ADMIN_SECRET='))
    .replace('ADMIN_SECRET=', '')
    .trim();
  console.log('\n[2] /admin/stats with real secret:');
  const r2 = await fetch(url + '/admin/stats', {
    headers: { 'X-Admin-Key': realSecret },
  });
  console.log('   status:', r2.status);
  console.log('   body:', await r2.text());

  // /admin/stats with debug key
  console.log('\n[3] /admin/stats with debug key moon-debug-2024:');
  const r3 = await fetch(url + '/admin/stats', {
    headers: { 'X-Admin-Key': 'moon-debug-2024' },
  });
  console.log('   status:', r3.status);
  console.log('   body:', await r3.text());

  // /admin/stats with empty
  console.log('\n[4] /admin/stats with empty:');
  const r4 = await fetch(url + '/admin/stats', {
    headers: { 'X-Admin-Key': '' },
  });
  console.log('   status:', r4.status);
  console.log('   body:', await r4.text());

  // /admin/stats with random
  console.log('\n[5] /admin/stats with WRONG secret:');
  const r5 = await fetch(url + '/admin/stats', {
    headers: { 'X-Admin-Key': 'wrong-key-' + 'x'.repeat(20) },
  });
  console.log('   status:', r5.status);
  console.log('   body:', await r5.text());
})();
