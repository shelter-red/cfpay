ALTER TABLE payment_order
ADD COLUMN paid_source TEXT CHECK (paid_source IN ('EVENT', 'MANUAL'));

ALTER TABLE payment_order
ADD COLUMN manual_paid_by TEXT;

UPDATE payment_order
SET paid_source = 'MANUAL',
    manual_paid_by = COALESCE(
      (
        SELECT action.actor
        FROM payment_event_action AS action
        WHERE action.event_id = payment_order.payment_event_id
          AND action.action = 'MANUAL_MATCH'
          AND action.reason = '管理员人工补单'
        ORDER BY action.id DESC
        LIMIT 1
      ),
      '历史管理员'
    ),
    payment_event_id = NULL
WHERE payment_event_id LIKE 'manual:%';

UPDATE payment_order
SET paid_source = 'EVENT'
WHERE status = 'PAID'
  AND payment_event_id IS NOT NULL
  AND paid_source IS NULL;

DELETE FROM payment_event
WHERE id LIKE 'manual:%';
