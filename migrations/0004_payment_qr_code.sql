CREATE TABLE payment_qr_code (
  provider TEXT PRIMARY KEY CHECK (provider IN ('wxpay', 'alipay')),
  payload TEXT NOT NULL CHECK (length(payload) BETWEEN 3 AND 2048),
  payload_sha256 TEXT NOT NULL CHECK (length(payload_sha256) = 64),
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
