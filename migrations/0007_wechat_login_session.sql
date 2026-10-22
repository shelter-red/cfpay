CREATE TABLE wechat_login_session (
  device_id TEXT PRIMARY KEY,
  status TEXT NOT NULL CHECK (status IN ('LOGIN_REQUIRED', 'LOGIN_CLICKED', 'QR_READY', 'LOGGED_IN', 'RESTARTING')),
  encrypted_qr_payload TEXT,
  encryption_iv TEXT,
  observed_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  CHECK (
    (status = 'QR_READY' AND encrypted_qr_payload IS NOT NULL AND encryption_iv IS NOT NULL)
    OR
    (status <> 'QR_READY' AND encrypted_qr_payload IS NULL AND encryption_iv IS NULL)
  )
);

CREATE INDEX wechat_login_session_expires_idx ON wechat_login_session(expires_at);
