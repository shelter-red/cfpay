import { readFileSync } from "node:fs";
import path from "node:path";
import { describe, expect, it } from "vitest";
import { APP_VERSION } from "../src/version";

describe("shared release version", () => {
  it("keeps package, Worker and Windows monitor versions identical", () => {
    const packageInfo = JSON.parse(
      readFileSync(path.resolve(import.meta.dirname, "../package.json"), "utf8"),
    ) as { version?: string };
    const header = readFileSync(
      path.resolve(import.meta.dirname, "../native/wechat-observer/wechat_monitor_version.h"),
      "utf8",
    );
    const nativeVersion = header.match(/kVersion\[\][^\"]*\"([^\"]+)\"/)?.[1];
    expect(packageInfo.version).toBe(APP_VERSION);
    expect(nativeVersion).toBe(APP_VERSION);
  });
});
