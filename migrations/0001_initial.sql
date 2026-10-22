PRAGMA foreign_keys = ON;

CREATE TABLE system_setting (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  merchant_id TEXT NOT NULL DEFAULT '1000',
  merchant_key TEXT,
  monitor_secret TEXT,
  wechat_qr_image_url TEXT,
  alipay_qr_image_url TEXT,
  order_ttl_minutes INTEGER NOT NULL DEFAULT 5 CHECK (order_ttl_minutes BETWEEN 1 AND 60),
  amount_direction TEXT NOT NULL DEFAULT 'UP' CHECK (amount_direction IN ('UP', 'DOWN')),
  max_amount_adjustments INTEGER NOT NULL DEFAULT 99 CHECK (max_amount_adjustments BETWEEN 0 AND 999),
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);

INSERT INTO system_setting (id, created_at, updated_at)
VALUES (
  1,
  CAST(strftime('%s', 'now') AS INTEGER) * 1000,
  CAST(strftime('%s', 'now') AS INTEGER) * 1000
);

CREATE TABLE payment_order (
  id TEXT PRIMARY KEY,
  merchant_order_no TEXT NOT NULL UNIQUE,
  merchant_id TEXT NOT NULL,
  provider TEXT NOT NULL CHECK (provider IN ('wxpay', 'alipay')),
  subject TEXT NOT NULL,
  amount_fen INTEGER NOT NULL CHECK (amount_fen > 0),
  payable_amount_fen INTEGER NOT NULL CHECK (payable_amount_fen > 0),
  status TEXT NOT NULL DEFAULT 'PENDING' CHECK (status IN ('PENDING', 'PAID', 'EXPIRED', 'CLOSED')),
  notify_status TEXT NOT NULL DEFAULT 'NONE' CHECK (notify_status IN ('NONE', 'PENDING', 'RETRYING', 'SUCCESS', 'FAILED')),
  notify_url TEXT,
  return_url TEXT,
  attach TEXT NOT NULL DEFAULT '',
  client_ip TEXT,
  device TEXT,
  payment_event_id TEXT,
  paid_at INTEGER,
  expires_at INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);

CREATE UNIQUE INDEX payment_order_active_amount_unique
ON payment_order(provider, payable_amount_fen)
WHERE status = 'PENDING';

CREATE INDEX payment_order_status_expires_idx
ON payment_order(status, expires_at);

CREATE INDEX payment_order_created_idx
ON payment_order(created_at DESC);

CREATE TABLE monitor_device (
  id TEXT PRIMARY KEY,
  name TEXT,
  version TEXT,
  platform TEXT,
  last_ip TEXT,
  last_heartbeat_at INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);

CREATE TABLE monitor_nonce (
  nonce TEXT PRIMARY KEY,
  device_id TEXT NOT NULL,
  expires_at INTEGER NOT NULL,
  created_at INTEGER NOT NULL
);

CREATE INDEX monitor_nonce_expires_idx ON monitor_nonce(expires_at);

CREATE TABLE payment_event (
  id TEXT PRIMARY KEY,
  device_id TEXT NOT NULL,
  provider TEXT NOT NULL CHECK (provider IN ('wxpay', 'alipay')),
  amount_fen INTEGER NOT NULL CHECK (amount_fen > 0),
  paid_at INTEGER NOT NULL,
  status TEXT NOT NULL DEFAULT 'UNMATCHED' CHECK (status IN ('UNMATCHED', 'MATCHED')),
  matched_order_id TEXT REFERENCES payment_order(id),
  raw_excerpt TEXT,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);

CREATE INDEX payment_event_match_idx
ON payment_event(provider, amount_fen, paid_at);

CREATE TABLE callback_job (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  order_id TEXT NOT NULL UNIQUE REFERENCES payment_order(id) ON DELETE CASCADE,
  status TEXT NOT NULL DEFAULT 'PENDING' CHECK (status IN ('PENDING', 'PROCESSING', 'SUCCESS', 'FAILED')),
  attempt_count INTEGER NOT NULL DEFAULT 0,
  max_attempts INTEGER NOT NULL DEFAULT 6,
  next_attempt_at INTEGER NOT NULL,
  lease_until INTEGER,
  last_error TEXT,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);

CREATE INDEX callback_job_due_idx
ON callback_job(status, next_attempt_at);

CREATE TABLE callback_attempt (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  job_id INTEGER NOT NULL REFERENCES callback_job(id) ON DELETE CASCADE,
  attempt_no INTEGER NOT NULL,
  response_status INTEGER,
  response_excerpt TEXT,
  error TEXT,
  created_at INTEGER NOT NULL
);

CREATE INDEX callback_attempt_job_idx
ON callback_attempt(job_id, id DESC);
