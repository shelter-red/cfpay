import { describe, expect, it } from "vitest";
import { formatBytes } from "../admin-ui/src/lib/format";

describe("admin byte formatting", () => {
  it.each([
    [0, "0 B"],
    [1023, "1023 B"],
    [1024, "1 KiB"],
    [74_400, "72.7 KiB"],
    [1024 * 1024, "1 MiB"],
    [48 * 1024 * 1024, "48 MiB"],
    [1024 * 1024 * 1024, "1 GiB"],
  ])("formats %i bytes as %s", (value, expected) => {
    expect(formatBytes(value)).toBe(expected);
  });
});
