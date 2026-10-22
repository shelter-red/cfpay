CREATE TABLE monitor_component_status (
  device_id TEXT NOT NULL,
  component TEXT NOT NULL,
  status TEXT NOT NULL CHECK (status IN ('STARTING', 'HEALTHY', 'DEGRADED', 'ERROR', 'STOPPED', 'UNSUPPORTED')),
  component_version TEXT,
  target_version TEXT,
  process_id INTEGER,
  hook_offset TEXT,
  detail_code TEXT,
  detail_message TEXT,
  last_event_at INTEGER,
  reported_at INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  PRIMARY KEY (device_id, component)
);

CREATE INDEX monitor_component_status_updated_idx
ON monitor_component_status(updated_at DESC);
