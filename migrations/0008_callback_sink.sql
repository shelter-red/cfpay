CREATE TABLE IF NOT EXISTS callback_sink (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  token TEXT NOT NULL,
  query TEXT NOT NULL,
  created_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS callback_sink_token_idx
ON callback_sink(token, id DESC);
