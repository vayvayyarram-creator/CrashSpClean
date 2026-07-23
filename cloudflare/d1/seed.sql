-- Moon Auth Service - Seed Data
-- Test/dev ortamı için başlangıç lisansları ve release'ler.
-- Production'dan ÖNCE değiştir!

-- Test lisansları — admin bunu kullanarak client testleri yapabilir
INSERT OR IGNORE INTO licenses (hwid, key, name, status, max_devices, current_devices, expires_at, created_at, last_seen, notes) VALUES
    ('TESTUSER01',  'MOON-TEST-0001-AAAA-0000', 'Test Hero',    'active', 1, 0, strftime('%s','now','+365 days')*1000, strftime('%s','now')*1000, NULL, 'Demo hesap'),
    ('TESTUSER02',  'MOON-TEST-0002-BBBB-1111', 'Test Buddy',   'active', 1, 0, strftime('%s','now','+30 days')*1000,  strftime('%s','now')*1000, NULL, '30 günlük trial'),
    ('TESTUSER03',  'MOON-TEST-0003-CCCC-2222', 'Test Lifetime','active', 5, 0, strftime('%s','now','+3650 days')*1000, strftime('%s','now')*1000, NULL, '5 cihaz, 10 yıl'),
    ('TESTEXP01',   'MOON-TEST-EXPI-XXXX-3333', 'Test Expired', 'expired', 1, 0, strftime('%s','now','-7 days')*1000, strftime('%s','now','-37 days')*1000, NULL, 'Geçmiş expire test'),
    ('TESTBAN01',   'MOON-TEST-BANN-YYYY-4444', 'Banned User',  'banned',  1, 0, strftime('%s','now','+90 days')*1000, strftime('%s','now')*1000, NULL, 'Ban test hesabı');

-- Webhook URL'leri — Discord embed bildirimler
INSERT OR IGNORE INTO webhooks (name, url, events, enabled, created_at) VALUES
    ('discord-public', 'https://discord.com/api/webhooks/YOUR_WEBHOOK_URL', '["auth.success","auth.failure","license.expired","license.banned","release.deployed"]', 1, strftime('%s','now')*1000),
    ('internal-logs',  'https://logs.internal.example/moon', '["*"]', 1, strftime('%s','now')*1000);

-- Feature flags — Aşamalı rollout için
INSERT OR IGNORE INTO feature_flags (name, description, enabled, rollout_percentage, target_versions, created_at, updated_at) VALUES
    ('magic_bullet_v2',  'Geliştirilmiş magic bullet algoritması', 1, 100, '["v1.0.0","v1.0.1"]', strftime('%s','now')*1000, strftime('%s','now')*1000),
    ('esp_lag_fix',     'Yeni ESP distance culling',                1, 100, '[]',          strftime('%s','now')*1000, strftime('%s','now')*1000),
    ('teleporter_v2',   'PlayerList > Teleport',                    1, 100, '[]',          strftime('%s','now')*1000, strftime('%s','now')*1000),
    ('web_menu_v3',     'OneUI menü tasarımı',                      0,   0, '[]',          strftime('%s','now')*1000, strftime('%s','now')*1000);

-- Settings override — bakım modu, varsayılanlar
INSERT OR IGNORE INTO settings (key, value, description, updated_at) VALUES
    ('app_version',           '1.0.0',           'Uygulama versiyonu',                               strftime('%s','now')*1000),
    ('notices_json',          '[]',               'Aktif bilgilendirme mesajları (JSON array)',       strftime('%s','now')*1000),
    ('discord_community_url', 'https://discord.gg/example', 'Topluluk Discord invite',            strftime('%s','now')*1000),
    ('telegram_channel_url',  'https://t.me/example',       'Telegram channel',                       strftime('%s','now')*1000),
    ('tg_bot_token',          '',                'Telegram bot token (boş = disabled)',              strftime('%s','now')*1000),
    ('tg_admin_chat_id',      '',                'Telegram admin chat ID',                            strftime('%s','now')*1000);

-- v1.0.0 placeholder release row (gerçek build_hash + SHA upload sırasında admin.js veya release.yml tarafından değiştirilir)
INSERT OR IGNORE INTO releases (version, build_hash, r2_key, file_size, sha256, changelog, status, is_latest, min_client_version, created_at, released_at) VALUES
    ('v1.0.0', 'initialbuild', 'releases/v1.0.0/SpCrashReport.dll', 0, 'pending', 'İlk kararlı release', 'active', 1, 'v1.0.0', strftime('%s','now')*1000, strftime('%s','now')*1000);
