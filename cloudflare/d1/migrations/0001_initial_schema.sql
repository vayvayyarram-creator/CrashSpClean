-- Moon Auth Service - D1 Database Schema
-- Run with: wrangler d1 execute moon-auth --local --file=./migrations/0001_initial_schema.sql

-- Licenses table
CREATE TABLE IF NOT EXISTS licenses (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    hwid TEXT UNIQUE NOT NULL,           -- Hardware ID (bound on first auth)
    key TEXT UNIQUE NOT NULL,            -- License key (for activation)
    name TEXT NOT NULL,                  -- User display name
    email TEXT,                          -- User email (optional)
    status TEXT NOT NULL DEFAULT 'active', -- active, expired, banned, revoked
    max_devices INTEGER NOT NULL DEFAULT 1,
    current_devices INTEGER NOT NULL DEFAULT 0,
    expires_at INTEGER NOT NULL,         -- Unix timestamp (ms)
    created_at INTEGER NOT NULL,         -- Unix timestamp (ms)
    last_seen INTEGER,                   -- Unix timestamp (ms)
    last_ip TEXT,                        -- Last seen IP
    notes TEXT                           -- Admin notes
);

CREATE INDEX IF NOT EXISTS idx_licenses_hwid ON licenses(hwid);
CREATE INDEX IF NOT EXISTS idx_licenses_key ON licenses(key);
CREATE INDEX IF NOT EXISTS idx_licenses_status ON licenses(status);
CREATE INDEX IF NOT EXISTS idx_licenses_expires ON licenses(expires_at);

-- User configs table
CREATE TABLE IF NOT EXISTS user_configs (
    hwid TEXT PRIMARY KEY,
    config TEXT NOT NULL DEFAULT '{}',   -- JSON config
    updated_at INTEGER NOT NULL,         -- Unix timestamp (ms)
    FOREIGN KEY (hwid) REFERENCES licenses(hwid) ON DELETE CASCADE
);

-- Authentication logs
CREATE TABLE IF NOT EXISTS auth_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    hwid TEXT NOT NULL,
    ip TEXT NOT NULL,
    success INTEGER NOT NULL,            -- 1 = success, 0 = failure
    reason TEXT NOT NULL,                -- Failure reason or 'success'
    timestamp INTEGER NOT NULL,          -- Unix timestamp (ms)
    user_agent TEXT
);

CREATE INDEX IF NOT EXISTS idx_auth_logs_hwid ON auth_logs(hwid);
CREATE INDEX IF NOT EXISTS idx_auth_logs_timestamp ON auth_logs(timestamp);
CREATE INDEX IF NOT EXISTS idx_auth_logs_success ON auth_logs(success);

-- Ping/heartbeat logs
CREATE TABLE IF NOT EXISTS ping_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    hwid TEXT NOT NULL,
    ip TEXT NOT NULL,
    version TEXT NOT NULL,
    build_hash TEXT NOT NULL,
    status TEXT NOT NULL,                -- running, injected, error, etc.
    game_pid INTEGER,
    game_name TEXT,
    features TEXT,                       -- JSON array of active features
    timestamp INTEGER NOT NULL,
    FOREIGN KEY (hwid) REFERENCES licenses(hwid) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_ping_logs_hwid ON ping_logs(hwid);
CREATE INDEX IF NOT EXISTS idx_ping_logs_timestamp ON ping_logs(timestamp);

-- Releases table
CREATE TABLE IF NOT EXISTS releases (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    version TEXT NOT NULL UNIQUE,
    build_hash TEXT NOT NULL,
    r2_key TEXT NOT NULL,                -- R2 object key (e.g., "releases/v1.0.0/SpCrashReport.dll")
    file_size INTEGER NOT NULL,          -- Bytes
    sha256 TEXT NOT NULL,                -- File checksum
    changelog TEXT,
    status TEXT NOT NULL DEFAULT 'active', -- active, deprecated, archived
    is_latest INTEGER NOT NULL DEFAULT 0, -- 1 = latest release
    min_client_version TEXT,             -- Minimum client version required
    created_at INTEGER NOT NULL,
    released_at INTEGER
);

CREATE INDEX IF NOT EXISTS idx_releases_version ON releases(version);
CREATE INDEX IF NOT EXISTS idx_releases_status ON releases(status);
CREATE INDEX IF NOT EXISTS idx_releases_latest ON releases(is_latest);

-- Admin users table
CREATE TABLE IF NOT EXISTS admin_users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,         -- Argon2id hash
    role TEXT NOT NULL DEFAULT 'admin',  -- admin, superadmin
    created_at INTEGER NOT NULL,
    last_login INTEGER
);

-- Admin sessions
CREATE TABLE IF NOT EXISTS admin_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    admin_id INTEGER NOT NULL,
    token TEXT UNIQUE NOT NULL,
    expires_at INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    ip TEXT,
    user_agent TEXT,
    FOREIGN KEY (admin_id) REFERENCES admin_users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_admin_sessions_token ON admin_sessions(token);
CREATE INDEX IF NOT EXISTS idx_admin_sessions_expires ON admin_sessions(expires_at);

-- Feature flags (for gradual rollouts)
CREATE TABLE IF NOT EXISTS feature_flags (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT UNIQUE NOT NULL,
    description TEXT,
    enabled INTEGER NOT NULL DEFAULT 0,  -- 1 = enabled, 0 = disabled
    rollout_percentage INTEGER NOT NULL DEFAULT 0, -- 0-100
    target_versions TEXT,                -- JSON array of versions, empty = all
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

-- Webhooks (for Discord notifications, etc.)
CREATE TABLE IF NOT EXISTS webhooks (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    url TEXT NOT NULL,
    events TEXT NOT NULL,                -- JSON array: ["auth.success", "auth.failure", "license.expired", "license.banned"]
    secret TEXT,                         -- HMAC secret for verification
    enabled INTEGER NOT NULL DEFAULT 1,
    created_at INTEGER NOT NULL
);

-- Settings table (key-value for runtime config)
CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    description TEXT,
    updated_at INTEGER NOT NULL
);

-- Insert default settings
INSERT OR IGNORE INTO settings (key, value, description, updated_at) VALUES
    ('maintenance_mode', '0', 'Enable maintenance mode (0=off, 1=on)', strftime('%s', 'now') * 1000),
    ('maintenance_message', 'Service is under maintenance. Please try again later.', 'Message shown during maintenance', strftime('%s', 'now') * 1000),
    ('max_devices_default', '1', 'Default max devices per license', strftime('%s', 'now') * 1000),
    ('license_duration_days', '30', 'Default license duration in days', strftime('%s', 'now') * 1000),
    ('ping_interval_seconds', '30', 'Expected ping interval from clients', strftime('%s', 'now') * 1000),
    ('rate_limit_auth_per_minute', '10', 'Auth requests per minute per IP', strftime('%s', 'now') * 1000),
    ('rate_limit_ping_per_minute', '60', 'Ping requests per minute per HWID', strftime('%s', 'now') * 1000);

-- Insert a sample admin user (password: changeme123 - CHANGE IN PRODUCTION!)
-- Argon2id hash for "changeme123"
INSERT OR IGNORE INTO admin_users (username, password_hash, role, created_at) VALUES
    ('admin', '$argon2id$v=19$m=65536,t=3,p=4$c29tZXNhbHQ$V9X8Y7Z6A5B4C3D2E1F0', 'superadmin', strftime('%s', 'now') * 1000);

-- Insert sample feature flags
INSERT OR IGNORE INTO feature_flags (name, description, enabled, rollout_percentage, target_versions, created_at, updated_at) VALUES
    ('new_esp_renderer', 'New ESP rendering pipeline', 1, 100, '[]', strftime('%s', 'now') * 1000, strftime('%s', 'now') * 1000),
    ('magic_bullet_fix', 'Magic bullet accuracy fix', 1, 50, '["1.0.0", "1.0.1"]', strftime('%s', 'now') * 1000, strftime('%s', 'now') * 1000),
    ('web_menu_v2', 'New web-based menu UI', 0, 0, '[]', strftime('%s', 'now') * 1000, strftime('%s', 'now') * 1000);