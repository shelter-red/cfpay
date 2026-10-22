CREATE TABLE payment_event_v2 (
  id TEXT PRIMARY KEY,
  device_id TEXT NOT NULL,
  provider TEXT NOT NULL CHECK (provider IN ('wxpay', 'alipay')),
  amount_fen INTEGER NOT NULL CHECK (amount_fen > 0),
  paid_at INTEGER NOT NULL,
  status TEXT NOT NULL DEFAULT 'UNMATCHED' CHECK (status IN ('UNMATCHED', 'MATCHED', 'IGNORED')),
  matched_order_id TEXT REFERENCES payment_order(id),
  raw_excerpt TEXT,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);

INSERT INTO payment_event_v2 (
  id, device_id, provider, amount_fen, paid_at, status, matched_order_id, raw_excerpt, created_at, updated_at
)
SELECT id, device_id, provider, amount_fen, paid_at, status, matched_order_id, raw_excerpt, created_at, updated_at
FROM payment_event;

DROP TABLE payment_event;
ALTER TABLE payment_event_v2 RENAME TO payment_event;

CREATE INDEX payment_event_match_idx ON payment_event(provider, amount_fen, paid_at);
CREATE INDEX payment_event_status_created_idx ON payment_event(status, created_at DESC);

CREATE TABLE payment_event_action (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  event_id TEXT NOT NULL REFERENCES payment_event(id) ON DELETE CASCADE,
  action TEXT NOT NULL CHECK (action IN ('AUTO_MATCH', 'MANUAL_MATCH', 'IGNORE', 'RESTORE')),
  order_id TEXT REFERENCES payment_order(id),
  reason TEXT,
  actor TEXT NOT NULL,
  created_at INTEGER NOT NULL
);

CREATE INDEX payment_event_action_event_idx ON payment_event_action(event_id, id DESC);
CREATE INDEX payment_event_action_created_idx ON payment_event_action(created_at DESC);

INSERT INTO payment_event_action (event_id, action, order_id, reason, actor, created_at)
SELECT id, 'AUTO_MATCH', matched_order_id, NULL, '系统', updated_at
FROM payment_event
WHERE status = 'MATCHED' AND matched_order_id IS NOT NULL;
