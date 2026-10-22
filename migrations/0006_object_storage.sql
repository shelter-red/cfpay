CREATE TABLE object_storage_setting (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  endpoint TEXT NOT NULL,
  region TEXT NOT NULL,
  bucket TEXT NOT NULL,
  path_prefix TEXT NOT NULL,
  force_path_style INTEGER NOT NULL CHECK (force_path_style IN (0, 1)),
  encrypted_credentials TEXT NOT NULL,
  encryption_iv TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);

CREATE TABLE alipay_credential_v2 (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  app_id TEXT NOT NULL,
  app_cert_sn TEXT NOT NULL,
  alipay_root_cert_sn TEXT NOT NULL,
  alipay_cert_sn TEXT NOT NULL,
  storage_backend TEXT NOT NULL DEFAULT 'database' CHECK (storage_backend IN ('database', 'object_storage')),
  encrypted_payload TEXT,
  encryption_iv TEXT NOT NULL,
  object_key TEXT,
  object_sha256 TEXT,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  CHECK (
    (storage_backend = 'database' AND encrypted_payload IS NOT NULL AND object_key IS NULL AND object_sha256 IS NULL)
    OR
    (storage_backend = 'object_storage' AND encrypted_payload IS NULL AND object_key IS NOT NULL AND length(object_sha256) = 64)
  )
);

INSERT INTO alipay_credential_v2 (
  id, app_id, app_cert_sn, alipay_root_cert_sn, alipay_cert_sn,
  storage_backend, encrypted_payload, encryption_iv, created_at, updated_at
)
SELECT
  id, app_id, app_cert_sn, alipay_root_cert_sn, alipay_cert_sn,
  'database', encrypted_payload, encryption_iv, created_at, updated_at
FROM alipay_credential;

DROP TABLE alipay_credential;
ALTER TABLE alipay_credential_v2 RENAME TO alipay_credential;
