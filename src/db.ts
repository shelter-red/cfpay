import { runtimeFirst } from "./runtime-data";
import type { Env, SystemSetting } from "./types";

// 读取单商户系统设置（第 1 行）。这是全系统的唯一配置行，缺失说明迁移没跑，直接抛 SYSTEM_SETTING_NOT_FOUND 而不是返回默认值。
export async function getSettings(env: Env): Promise<SystemSetting> {
  const setting = await runtimeFirst<SystemSetting>(
    env,
    "config:system",
    "SELECT * FROM system_setting WHERE id = 1",
    [],
    { ttlMs: null, pinned: true },
  );
  if (!setting) throw new Error("SYSTEM_SETTING_NOT_FOUND");
  return setting;
}
