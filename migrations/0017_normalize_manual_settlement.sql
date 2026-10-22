INSERT INTO payment_event_action (event_id, action, order_id, reason, actor, created_at)
SELECT
  event.id,
  'MANUAL_MATCH',
  event.matched_order_id,
  '管理员人工补单',
  '历史管理员',
  event.created_at
FROM payment_event AS event
WHERE event.id LIKE 'manual:%'
  AND NOT EXISTS (
    SELECT 1
    FROM payment_event_action AS action
    WHERE action.event_id = event.id
      AND action.action = 'MANUAL_MATCH'
      AND action.reason = '管理员人工补单'
  );

UPDATE payment_event
SET raw_excerpt = NULL,
    updated_at = CAST(strftime('%s', 'now') AS INTEGER) * 1000
WHERE id LIKE 'manual:%'
  AND raw_excerpt = 'manual settlement';
