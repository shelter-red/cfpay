ALTER TABLE system_setting ADD COLUMN site_name TEXT NOT NULL DEFAULT 'CFPay';
ALTER TABLE system_setting ADD COLUMN site_subtitle TEXT NOT NULL DEFAULT '个人收款控制台';
ALTER TABLE system_setting ADD COLUMN site_notice TEXT NOT NULL DEFAULT '';
ALTER TABLE system_setting ADD COLUMN footer_text TEXT NOT NULL DEFAULT '';
