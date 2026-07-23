// Set ADMIN_SECRET on the Worker using Cloudflare API directly
// Environment variables (set before running):
//   CF_API_TOKEN         — your Cloudflare API token
//   CF_ACCOUNT_ID        — your Cloudflare account ID
//   ADMIN_SECRET_VALUE   — desired ADMIN_SECRET value
//   WORKER_NAME          — (optional) worker script name, default "moon-auth-service"

const secretValue = process.env.ADMIN_SECRET_VALUE || 'PLACEHOLDER_ADMIN_SECRET';
const acc        = process.env.CF_ACCOUNT_ID  || 'PLACEHOLDER_ACCOUNT_ID';
const worker     = process.env.WORKER_NAME     || 'moon-auth-service';
const token      = process.env.CF_API_TOKEN   || 'PLACEHOLDER_CF_API_TOKEN';

async function setSecret() {
  // Try PATCH first (update)
  const url = `https://api.cloudflare.com/client/v4/accounts/${acc}/workers/scripts/${worker}`;
  const r = await fetch(url, {
    method: 'PATCH',
    headers: {
      'Authorization': `Bearer ${token}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({
      metadata: { main_module: 'dist/index.js' },
      bindings: [
        { type: 'secret_text', name: 'ADMIN_SECRET', value: secretValue },
      ],
    }),
  });
  console.log('PATCH bindings status:', r.status);
  console.log('   body:', (await r.text()).slice(0, 500));
}

async function listSecrets() {
  const url = `https://api.cloudflare.com/client/v4/accounts/${acc}/workers/scripts/${worker}/secrets`;
  const r = await fetch(url, {
    headers: { 'Authorization': `Bearer ${token}` },
  });
  console.log('GET /secrets status:', r.status);
  console.log('   body:', await r.text());
}

async function testAdminStats() {
  const url = 'https://moon-auth-service.moonsal.workers.dev/admin/stats';
  const r = await fetch(url, {
    headers: { 'X-Admin-Key': secretValue },
  });
  console.log('Test admin/stats status:', r.status);
  console.log('   body:', (await r.text()).slice(0, 500));
}

(async () => {
  console.log('Set required env vars before running:');
  console.log('  set CF_API_TOKEN=...');
  console.log('  set CF_ACCOUNT_ID=...');
  console.log('  set ADMIN_SECRET_VALUE=...');
  console.log('');

  await listSecrets();
  await setSecret();
  await new Promise(r => setTimeout(r, 2000));
  await testAdminStats();
})();
