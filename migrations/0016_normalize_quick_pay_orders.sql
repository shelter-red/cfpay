UPDATE payment_order
SET device = NULL,
    attach = CASE WHEN attach = 'cfpay-payment-test' THEN '' ELSE attach END,
    updated_at = CAST(strftime('%s', 'now') AS INTEGER) * 1000
WHERE device = 'cfpay-admin-payment-test';
