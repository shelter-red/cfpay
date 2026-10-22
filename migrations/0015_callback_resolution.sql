ALTER TABLE callback_job ADD COLUMN handled_at INTEGER;
ALTER TABLE callback_job ADD COLUMN handled_reason TEXT;
ALTER TABLE callback_job ADD COLUMN handled_by TEXT;

CREATE INDEX callback_job_handled_idx ON callback_job(status, handled_at);
