// Test /download/payload
const url = 'https://moon-auth-service.moonsal.workers.dev';
(async () => {
  console.log('== Test /download/payload ==');
  const r = await fetch(url + '/download/payload?key=releases/v1.0.0/SpCrashReport.dll', { redirect: 'manual' });
  console.log('   status:', r.status);
  console.log('   location:', r.headers.get('location') || '(none)');
  console.log('   body:', (await r.text()).slice(0, 300));

  console.log('');
  console.log('== Follow /download/payload with redirect =');
  const r2 = await fetch(url + '/download/payload?key=releases/v1.0.0/SpCrashReport.dll', { redirect: 'follow' });
  console.log('   final status:', r2.status);
  console.log('   redirected:', r2.redirected);
  console.log('   url:', r2.url);
  const buf = await r2.arrayBuffer();
  console.log('   bytes received:', buf.byteLength);
  const text = new TextDecoder('utf-8', { fatal: false }).decode(buf);
  console.log('   preview:', text.slice(0, 100));
})();
