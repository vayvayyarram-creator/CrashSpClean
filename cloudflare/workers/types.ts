// Moon Auth Service - TypeScript Types
// Shared types for Worker and client

export interface Env {
  DB: D1Database;
  // R2 binding is optional — disabled until Cloudflare R2 is activated for this account
  R2_BUCKET?: R2Bucket;
  R2_ACCOUNT_ID: string;
  R2_BUCKET_NAME: string;
  ADMIN_SECRET: string;
  ENVIRONMENT: string;
  // GitHub Releases fallback — when R2 is not available, /download/payload
  // redirects to public GitHub releases CDN (no auth, free, cloudflare backed)
  GITHUB_OWNER?: string;
  GITHUB_REPO?: string;
}

export interface LicenseRecord {
  id: number;
  hwid: string;
  key: string;
  name: string;
  email?: string;
  status: 'active' | 'expired' | 'banned' | 'revoked';
  max_devices: number;
  current_devices: number;
  expires_at: number;
  created_at: number;
  last_seen: number;
  last_ip: string;
  notes?: string;
}

export interface ReleaseRecord {
  id: number;
  version: string;
  build_hash: string;
  r2_key: string;
  file_size: number;
  sha256: string;
  changelog: string;
  status: 'active' | 'deprecated' | 'archived';
  is_latest: number;
  min_client_version: string;
  created_at: number;
  released_at: number;
}

export interface AuthRequest {
  hwid: string;
  key?: string;
  version: string;
  build_hash?: string;
}

export interface AuthResponse {
  success: boolean;
  message: string;
  data?: {
    name: string;
    expires_at: number;
    download_url: string;
    version: string;
    build_hash: string;
    config: string;
  };
}

export interface PingRequest {
  hwid: string;
  version: string;
  build_hash: string;
  status: 'running' | 'injected' | 'error';
  game_pid?: number;
  game_name?: string;
  features?: string[];
}

export interface PingResponse {
  success: boolean;
  message: string;
  data?: {
    update_available: boolean;
    latest_version: string;
    download_url: string;
    build_hash: string;
    message_of_day: string;
  };
}