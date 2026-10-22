-- 放开管理员账号与后台入口路径的数据库长度约束。
-- 0005 建表时写了 length(username) BETWEEN 3 AND 64、length(admin_path) BETWEEN 8 AND 96，
-- 应用层校验放开后，这两条 CHECK 仍会让写入失败并报 SQLITE_CONSTRAINT_CHECK，
-- 前端只能看到兜底的“请求处理失败”。这里只保留“非空”这一下限。
CREATE TABLE admin_setting_v2 (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  username TEXT NOT NULL CHECK (length(username) > 0),
  admin_path TEXT NOT NULL CHECK (length(admin_path) > 0),
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

INSERT INTO admin_setting_v2 (
  id, username, admin_path, password_hash, password_salt, password_iterations,
  session_version, created_at, updated_at
)
SELECT
  id, username, admin_path, password_hash, password_salt, password_iterations,
  session_version, created_at, updated_at
FROM admin_setting;

DROP TABLE admin_setting;
ALTER TABLE admin_setting_v2 RENAME TO admin_setting;
