CREATE TABLE admin_setting (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  username TEXT NOT NULL CHECK (length(username) BETWEEN 3 AND 64),
  admin_path TEXT NOT NULL CHECK (length(admin_path) BETWEEN 8 AND 96),
  password_hash TEXT,
  password_salt TEXT,
  password_iterations INTEGER CHECK (password_iterations BETWEEN 100000 AND 1000000),
  session_version INTEGER NOT NULL DEFAULT 1 CHECK (session_version > 0),
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  CHECK (
    (password_hash IS NULL AND password_salt IS NULL AND password_iterations IS NULL)
    OR
    (password_hash IS NOT NULL AND password_salt IS NOT NULL AND password_iterations IS NOT NULL)
  )
);
