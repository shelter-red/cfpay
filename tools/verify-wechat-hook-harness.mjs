import { readFile } from "node:fs/promises";

const outputPath = process.argv[2];
if (!outputPath) {
  console.error("用法: node tools/verify-wechat-hook-harness.mjs <observer-output.jsonl>");
  process.exit(1);
}

const lines = (await readFile(outputPath, "utf8"))
  .split(/\r?\n/g)
  .filter(Boolean)
  .map((line) => JSON.parse(line));

const events = lines.filter((item) => item.type === "event");
const accounts = lines.filter((item) => item.type === "account");
const nicknames = lines.filter((item) => item.type === "nickname");
const statuses = lines.filter((item) => item.type === "status");
if (!statuses.some((item) => item.status === "HEALTHY" && item.detailCode === "HOOK_ACTIVE")) {
  throw new Error("HOOK_ACTIVE status was not emitted");
}
if (!statuses.some((item) => item.status === "DEGRADED" && item.detailCode === "WEIXIN_LOGIN_REQUIRED")) {
  throw new Error("WEIXIN_LOGIN_REQUIRED status was not emitted");
}
if (!statuses.some((item) => item.status === "STOPPED" && item.detailCode === "HOOK_STOPPED")) {
  throw new Error("HOOK_STOPPED status was not emitted");
}
if (events.length !== 3) throw new Error(`expected 3 events, received ${events.length}`);
if (accounts.length !== 4) throw new Error(`expected 4 account records, received ${accounts.length}`);
if (events[0].fields.transactionId !== "transaction-test-000001" || events[0].fields.paySubtype !== 1) {
  throw new Error("first event fields did not match");
}
if (events[0].fields.feeDescription !== "CNY 0.01" || events[1].fields.feeDescription !== "CNY 0.02") {
  throw new Error("event amount fields did not match");
}
if (!events.some((item) => item.fields.transactionId === "transaction-thread-000003")) {
  throw new Error("new thread event was not observed");
}
if (lines.some((item) => item.fields?.transactionId === "transaction-after-stop")) {
  throw new Error("observer emitted an event after stop");
}
if (accounts[0].fields.loggedIn !== false || accounts[0].fields.username !== "" || accounts[0].fields.uin !== 0) {
  throw new Error("logged-out account state did not match");
}
if (accounts.filter((item) => item.fields.loggedIn === false).length !== 2) {
  throw new Error("logged-out confirmation was not debounced");
}
if (!accounts.some((item) => item.fields.username === "wxid_receiver_long_name" && item.fields.uin === 10001)) {
  throw new Error("first account fields did not match");
}
if (!accounts.some((item) => item.fields.username === "wxid_thread_account")) {
  throw new Error("new thread account was not observed");
}
if (!nicknames.some((item) => item.fields.nickname === "Receiver Nickname")) {
  throw new Error("nickname hook record was not observed");
}
if (lines.some((item) => item.fields?.username === "wxid_after_stop")) {
  throw new Error("observer emitted an account after stop");
}

console.log(JSON.stringify({ ok: true, statuses: statuses.length, events: events.length, accounts: accounts.length, nicknames: nicknames.length }));
