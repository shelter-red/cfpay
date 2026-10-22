-- 移除 S3 兼容私密对象存储：支付宝凭据固定只存 D1（AES-GCM 密文）。
-- object_storage_setting 里加密保存的 AccessKey/SecretKey 一并删除，需要时重新配置。
DROP TABLE IF EXISTS object_storage_setting;

-- alipay_credential 收回 database-only 形态。
-- 这里刻意不加 WHERE 过滤：若仍有行的 encrypted_payload 为 NULL（即凭据在 OSS 上），
-- NOT NULL 约束会让迁移直接失败，避免静默丢掉支付宝私钥。
CREATE TABLE alipay_credential_v3 (
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

INSERT INTO alipay_credential_v3 (
  id, app_id, app_cert_sn, alipay_root_cert_sn, alipay_cert_sn,
  encrypted_payload, encryption_iv, created_at, updated_at
)
SELECT
  id, app_id, app_cert_sn, alipay_root_cert_sn, alipay_cert_sn,
  encrypted_payload, encryption_iv, created_at, updated_at
FROM alipay_credential;

DROP TABLE alipay_credential;
ALTER TABLE alipay_credential_v3 RENAME TO alipay_credential;
