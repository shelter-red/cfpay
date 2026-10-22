#!/usr/bin/env node

import { createHash, createHmac, timingSafeEqual } from "node:crypto";
import { appendFile } from "node:fs/promises";
import http from "node:http";

const options = parseArgs(process.argv.slice(2));
const port = Number(options.port ?? 8799);
const host = options.host ?? "0.0.0.0";
const secret = options.secret;
const output = options.output;
const expectedPayments = Number(options["expected-payments"] ?? 3);
const failFirstPayment = options["fail-first-payment"] === "true";
if (!secret || !output || !Number.isInteger(port) || port <= 0 || !Number.isInteger(expectedPayments)) {
  console.error("用法: node tools/mock-monitor-server.mjs --secret <密钥> --output <JSONL> [--host 0.0.0.0] [--port 8799]");
  process.exit(2);
}

let paymentCount = 0;
let paymentFailureInjected = false;
const nonces = new Set();
const server = http.createServer(async (request, response) => {
  try {
    const body = await readBody(request);
    verifyRequest(request, body);
    const payload = body ? JSON.parse(body) : {};
    const shouldFail = request.url === "/api/monitor/payment" && failFirstPayment && !paymentFailureInjected;
    if (shouldFail) paymentFailureInjected = true;
    const record = { path: request.url, payload, responseStatus: shouldFail ? 503 : 200, receivedAt: Date.now() };
    await appendFile(output, `${JSON.stringify(record)}\n`, "utf8");
    if (shouldFail) {
      response.writeHead(503, { "content-type": "application/json" });
      response.end(JSON.stringify({ ok: false, error: "INJECTED_TEMPORARY_FAILURE" }));
      return;
    }
    if (request.url === "/api/monitor/payment") paymentCount += 1;
    response.writeHead(200, { "content-type": "application/json" });
    response.end(JSON.stringify({ ok: true, duplicate: false, matched: false, orderId: null, serverTime: Date.now() }));
    if (paymentCount >= expectedPayments) setTimeout(() => server.close(), 500).unref();
  } catch (error) {
    response.writeHead(401, { "content-type": "application/json" });
    response.end(JSON.stringify({ ok: false, error: error instanceof Error ? error.message : String(error) }));
  }
});

server.listen(port, host, () => console.log(`LISTENING=http://${host}:${port}`));
await new Promise((resolve) => server.once("close", resolve));
console.log(`COMPLETED_PAYMENTS=${paymentCount}`);

function verifyRequest(request, body) {
  const monitorId = request.headers["x-monitor-id"];
  const timestamp = request.headers["x-timestamp"];
  const nonce = request.headers["x-nonce"];
  const signature = request.headers["x-signature"];
  if (monitorId !== "windows-e2e" || typeof timestamp !== "string" || typeof nonce !== "string" || typeof signature !== "string") {
    throw new Error("AUTH_HEADERS_INVALID");
  }
  if (Math.abs(Math.floor(Date.now() / 1000) - Number(timestamp)) > 300) throw new Error("TIMESTAMP_INVALID");
  if (nonces.has(nonce)) throw new Error("NONCE_REPLAY");
  nonces.add(nonce);
  const bodyHash = createHash("sha256").update(body).digest("hex");
  const expected = createHmac("sha256", secret).update(`${timestamp}\n${nonce}\n${bodyHash}`).digest("hex");
  const actualBytes = Buffer.from(signature, "hex");
  const expectedBytes = Buffer.from(expected, "hex");
  if (actualBytes.length !== expectedBytes.length || !timingSafeEqual(actualBytes, expectedBytes)) throw new Error("SIGNATURE_INVALID");
}

function readBody(request) {
  return new Promise((resolve, reject) => {
    let body = "";
    request.setEncoding("utf8");
    request.on("data", (chunk) => {
      body += chunk;
      if (body.length > 1024 * 1024) reject(new Error("BODY_TOO_LARGE"));
    });
    request.on("end", () => resolve(body));
    request.on("error", reject);
  });
}

function parseArgs(args) {
  const result = {};
  for (let index = 0; index < args.length; index += 2) result[args[index].replace(/^--/, "")] = args[index + 1];
  return result;
}
