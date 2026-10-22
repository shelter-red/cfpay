import { describe, expect, it } from "vitest";
import {
  hashAdminPassword,
  matchesAdminEntry,
  validateAdminPath,
  validateAdminUsername,
  verifyAdminPassword,
} from "../src/auth";

describe("dynamic admin authentication", () => {
  it("hashes passwords with PBKDF2 and verifies without storing plaintext", async () => {
    const salt = Uint8Array.from({ length: 16 }, (_, index) => index + 1);
    const result = await hashAdminPassword("correct horse battery staple", salt, 100_000);
    expect(result.hash).not.toContain("correct horse");
    await expect(verifyAdminPassword("correct horse battery staple", result.hash, result.salt, result.iterations)).resolves.toBe(
      true,
    );
    await expect(verifyAdminPassword("wrong password", result.hash, result.salt, result.iterations)).resolves.toBe(false);
  });

  it("chains PBKDF2 rounds to reach the target work factor inside the Workers cap", async () => {
    // Workers 单次 PBKDF2 最多 100000 次迭代，600000 次靠 6 轮链式派生达到。
    const salt = Uint8Array.from({ length: 16 }, (_, index) => index + 1);
    const single = await hashAdminPassword("correct horse battery staple", salt, 100_000);
    const chained = await hashAdminPassword("correct horse battery staple", salt, 600_000);

    expect(chained.iterations).toBe(600_000);
    expect(chained.salt).toBe(single.salt);
    expect(chained.hash).not.toBe(single.hash);
    await expect(
      verifyAdminPassword("correct horse battery staple", chained.hash, chained.salt, chained.iterations),
    ).resolves.toBe(true);
    await expect(verifyAdminPassword("wrong password", chained.hash, chained.salt, chained.iterations)).resolves.toBe(
      false,
    );
    // 迭代次数参与派生：用其它次数校验同一份哈希必须失败。
    await expect(
      verifyAdminPassword("correct horse battery staple", chained.hash, chained.salt, 100_000),
    ).resolves.toBe(false);
  });

  it("rejects iteration counts the Workers runtime cannot express", async () => {
    const salt = Uint8Array.from({ length: 16 }, (_, index) => index + 1);

    for (const iterations of [0, 50_000, 150_000, 310_000, 1_000_001, 2_000_000]) {
      await expect(hashAdminPassword("correct horse battery staple", salt, iterations)).rejects.toThrow(
        "INVALID_ADMIN_PASSWORD_ITERATIONS",
      );
    }
  });

  it("accepts any non-empty administrator username", () => {
    expect(validateAdminUsername("owner.primary")).toBe("owner.primary");
    expect(validateAdminUsername("ab")).toBe("ab");
    expect(validateAdminUsername("a")).toBe("a");
    expect(validateAdminUsername("管理员@example.com")).toBe("管理员@example.com");
    expect(validateAdminUsername("  带 空 格  ")).toBe("带 空 格");
    expect(validateAdminUsername("x".repeat(300))).toBe("x".repeat(300));

    expect(() => validateAdminUsername("")).toThrow("ADMIN_USERNAME_REQUIRED");
    expect(() => validateAdminUsername("   ")).toThrow("ADMIN_USERNAME_REQUIRED");
    expect(() => validateAdminUsername(123)).toThrow("ADMIN_USERNAME_REQUIRED");
  });

  it("accepts unrestricted entry paths of any charset, length and depth", () => {
    expect(validateAdminPath("/admin-a1b2c3d4/")).toBe("admin-a1b2c3d4");
    expect(validateAdminPath("a")).toBe("a");
    expect(validateAdminPath("后台/入口")).toBe("后台/入口");
    expect(validateAdminPath("console.entry-v2/deep/path")).toBe("console.entry-v2/deep/path");
    expect(validateAdminPath("y".repeat(300))).toBe("y".repeat(300));
  });

  it("still rejects an empty entry path and paths that collide with system routes", () => {
    expect(() => validateAdminPath("")).toThrow("ADMIN_PATH_REQUIRED");
    expect(() => validateAdminPath("   /  ")).toThrow("ADMIN_PATH_REQUIRED");
    expect(() => validateAdminPath(null)).toThrow("ADMIN_PATH_REQUIRED");

    const reserved = [
      "admin",
      "admin-app",
      "admin.html",
      "admin.js",
      "admin-api",
      "api",
      "api.php",
      "index.html",
      "mapi.php",
      "pay",
      "payment-assets",
      "query.php",
      "styles.css",
      "submit.php",
    ];
    for (const path of reserved) {
      expect(() => validateAdminPath(path)).toThrow("ADMIN_PATH_RESERVED");
      expect(() => validateAdminPath(path.toUpperCase())).toThrow("ADMIN_PATH_RESERVED");
      expect(() => validateAdminPath(`${path}/deeper`)).toThrow("ADMIN_PATH_RESERVED");
    }
  });

  it("matches the configured entry path exactly, including non-ASCII and trailing slashes", () => {
    expect(matchesAdminEntry("/admin-a1b2c3d4", "admin-a1b2c3d4")).toBe(true);
    expect(matchesAdminEntry("/admin-a1b2c3d4/", "admin-a1b2c3d4")).toBe(true);
    expect(matchesAdminEntry("/console/deep/path", "console/deep/path")).toBe(true);
    expect(matchesAdminEntry("/%E5%90%8E%E5%8F%B0/%E5%85%A5%E5%8F%A3", "后台/入口")).toBe(true);

    expect(matchesAdminEntry("/admin-a1b2c3d5", "admin-a1b2c3d4")).toBe(false);
    expect(matchesAdminEntry("/", "")).toBe(false);
    expect(matchesAdminEntry("/anything", "")).toBe(false);
    // 固定 404 的默认入口即使被配置成同名也不放行。
    expect(matchesAdminEntry("/admin", "admin")).toBe(false);
    expect(matchesAdminEntry("/admin.html", "admin.html")).toBe(false);
  });

  it("serves the SPA index for every sub route under the entry path", () => {
    // 管理后台是 Vue SPA，入口下的子路由必须由 Worker 返回同一份 index.html，
    // 否则在子页面刷新会 404。
    expect(matchesAdminEntry("/cfpay", "cfpay")).toBe(true);
    expect(matchesAdminEntry("/cfpay/orders", "cfpay")).toBe(true);
    expect(matchesAdminEntry("/cfpay/orders/", "cfpay")).toBe(true);
    expect(matchesAdminEntry("/cfpay/monitors/deep", "cfpay")).toBe(true);

    // 前缀相似但不是入口子路径的请求不能命中。
    expect(matchesAdminEntry("/cfpay2", "cfpay")).toBe(false);
    expect(matchesAdminEntry("/cfpay-other/orders", "cfpay")).toBe(false);
  });

  it("never exposes the SPA bundle entry outside the configured path", () => {
    expect(matchesAdminEntry("/admin-app", "admin-app")).toBe(false);
    expect(matchesAdminEntry("/admin-app/index.html", "admin-app/index.html")).toBe(false);
    expect(matchesAdminEntry("/admin.js", "admin.js")).toBe(false);
  });

  it("falls back to the raw pathname when percent decoding fails", () => {
    expect(matchesAdminEntry("/100%-entry", "100%-entry")).toBe(true);
    expect(matchesAdminEntry("/100%-entry", "other")).toBe(false);
  });
});
