DROP TABLE IF EXISTS monitor_component_status;
DROP TABLE IF EXISTS monitor_device;
DROP TABLE IF EXISTS monitor_nonce;
DROP TABLE IF EXISTS wechat_login_session;
DROP TABLE IF EXISTS callback_sink;

CREATE TABLE system_setting_v2 (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  merchant_id TEXT NOT NULL DEFAULT '1000',
  merchant_key TEXT,
  monitor_secret TEXT,
  order_ttl_minutes INTEGER NOT NULL DEFAULT 5 CHECK (order_ttl_minutes BETWEEN 1 AND 60),
  amount_direction TEXT NOT NULL DEFAULT 'UP' CHECK (amount_direction IN ('UP', 'DOWN')),
  max_amount_adjustments INTEGER NOT NULL DEFAULT 99 CHECK (max_amount_adjustments BETWEEN 0 AND 999),
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  site_name TEXT NOT NULL DEFAULT 'CFPay',
  site_subtitle TEXT NOT NULL DEFAULT '个人收款控制台',
  site_notice TEXT NOT NULL DEFAULT '',
  footer_text TEXT NOT NULL DEFAULT '',
  favicon_data_url TEXT NOT NULL DEFAULT ''
);

INSERT INTO system_setting_v2 (
  id, merchant_id, merchant_key, monitor_secret, order_ttl_minutes, amount_direction,
  max_amount_adjustments, created_at, updated_at, site_name, site_subtitle,
  site_notice, footer_text, favicon_data_url
)
SELECT
  id, merchant_id, merchant_key, monitor_secret, order_ttl_minutes, amount_direction,
  max_amount_adjustments, created_at, updated_at, site_name, site_subtitle,
  site_notice, footer_text, favicon_data_url
FROM system_setting;

DROP TABLE system_setting;
ALTER TABLE system_setting_v2 RENAME TO system_setting;

CREATE INDEX IF NOT EXISTS payment_event_status_created_idx
ON payment_event(status, created_at DESC);

CREATE INDEX IF NOT EXISTS payment_order_provider_status_expires_idx
ON payment_order(provider, status, expires_at);

CREATE INDEX IF NOT EXISTS callback_job_status_lease_idx
ON callback_job(status, lease_until);
