CREATE TABLE alipay_credential (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  app_id TEXT NOT NULL,
  app_cert_sn TEXT NOT NULL,
  alipay_root_cert_sn TEXT NOT NULL,
  alipay_cert_sn TEXT NOT NULL,
  encrypted_payload TEXT NOT NULL,
  encryption_iv TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
