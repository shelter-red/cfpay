import { AlipayRequestError } from "alipay-sdk";
import { createAlipayClient, mask, queryAlipayLedger, sanitizeLedgerDetail } from "./lib/alipay-client.mjs";

try {
  const client = await createAlipayClient();
  const result = await queryAlipayLedger(client);

  console.log("正在查询支付宝账务明细", {
    appId: mask(client.config.appId),
    startTime: result.query.start_time,
    endTime: result.query.end_time,
    pageSize: result.query.page_size,
  });
  console.log("查询成功", {
    responseHttpStatus: result.responseHttpStatus,
    traceId: result.traceId,
    pageNo: result.pageNo,
    pageSize: result.pageSize,
    totalSize: result.totalSize,
    returned: result.details.length,
  });
  console.log(JSON.stringify(result.details.map(sanitizeLedgerDetail), null, 2));
} catch (error) {
  if (error instanceof AlipayRequestError) {
    console.error("支付宝查询失败", {
      name: error.name,
      message: error.message,
      code: error.code,
      responseHttpStatus: error.responseHttpStatus,
      traceId: error.traceId,
      links: error.links,
    });
  } else {
    console.error("本地查询失败", error instanceof Error ? error.message : String(error));
  }
  process.exitCode = 1;
}
