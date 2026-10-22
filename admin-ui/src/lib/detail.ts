import {
  CALLBACK_STATUS_LABELS, NOTIFY_STATUS_LABELS, ORDER_STATUS_LABELS, PROVIDER_LABELS,
  formatDateTime, formatFen, prettyJson,
} from "@/lib/format";
import type { OrderDetail, PaymentEventDetail } from "@/lib/types";

export type DetailRow = [string, string | number | null | undefined];

export type DetailSection =
  | { kind: "table"; title: string; rows: DetailRow[] }
  | { kind: "raw"; title: string; raw: string }
  | { kind: "attempts"; title: string; attempts: OrderDetail["callbackAttempts"] }
  | { kind: "text"; title: string; text: string };

/**
 * 结构化字段名 → 中文标签。两条渠道的上报结构不同：微信是监控端解析出的 paymsg 协议字段，
 * 支付宝是账务接口返回的明细字段。未登记的键也会按原始键名展示，绝不静默丢弃。
 */
const PAYMENT_FIELD_LABELS: Record<string, string> = {
  // 微信 paymsg
  source: "来源",
  transid: "交易号（transid）",
  outtradeno: "商户交易号（outtradeno）",
  payMsgType: "PayMsgType",
  fee: "金额（分）",
  feetype: "币种（feetype）",
  status: "状态（status）",
  scene: "场景（scene）",
  type: "收款类型（type）",
  timestamp: "支付时间戳",
  msgType: "消息类型",
  createTime: "消息创建时间",
  serverMsgId: "服务端消息号",
  toUsername: "收款账号",
  fromUsername: "消息发送方",
  payerUsername: "付款方账号",
  payerDisplayName: "付款方昵称",
  contentBytes: "原文字节数",
  // 支付宝账务明细（alipay.data.bill.accountlog.query）
  accountLogId: "账务流水号（accountLogId）",
  alipayOrderNo: "支付宝订单号",
  merchantOrderNo: "支付宝侧商户单号",
  transAmount: "账务金额（元）",
  transDt: "账务时间",
  direction: "收支方向",
  memo: "账务备注",
};

const PAYMENT_EVENT_ACTION_LABELS: Record<string, string> = {
  AUTO_MATCH: "系统自动匹配",
  MANUAL_MATCH: "管理员人工匹配",
  IGNORE: "管理员忽略",
  RESTORE: "恢复为待处理",
};

// 把订单自身的人工支付确认信息组装成独立区块，避免伪装成真实收款事件。
function manualPaymentConfirmationSection(order: OrderDetail["order"]): DetailSection {
  return {
    kind: "table",
    title: "支付确认",
    rows: [
      ["确认方式", "管理员人工补单"],
      ["操作管理员", order.manual_paid_by || "历史管理员"],
      ["确认时间", formatDateTime(order.paid_at)],
    ],
  };
}

// 格式化单个字段值：时间戳类字段额外附上可读时间，空值统一显示 “-”。
function paymentValue(key: string, value: unknown): string {
  if (key === "timestamp" || key === "createTime") {
    const seconds = Number(value);
    return Number.isFinite(seconds) ? `${value}（${formatDateTime(seconds * 1000)}）` : String(value ?? "-");
  }
  return value === null || value === undefined || value === "" ? "-" : String(value);
}

// 把上报的结构化字段与原始 JSON 组装成详情区块。
// 未登记标签的键也会按原始键名展示，避免后端加了字段而后台看不见。
export function paymentSections(detail: Record<string, unknown> | null, raw: string | null): DetailSection[] {
  const sections: DetailSection[] = [];
  if (detail && typeof detail === "object") {
    const known = Object.keys(PAYMENT_FIELD_LABELS).filter((key) => key in detail);
    // 未登记的新字段照原样展示，避免以后后端加字段而后台看不见。
    const unknown = Object.keys(detail).filter((key) => !(key in PAYMENT_FIELD_LABELS));
    const rows: DetailRow[] = [
      ...known.map((key) => [PAYMENT_FIELD_LABELS[key], paymentValue(key, detail[key])] as DetailRow),
      ...unknown.map((key) => [key, paymentValue(key, detail[key])] as DetailRow),
    ];
    if (rows.length) sections.push({ kind: "table", title: "解析字段", rows });
  }
  if (raw) sections.push({ kind: "raw", title: "完整上报 JSON", raw: prettyJson(raw) });
  return sections;
}

// 组装订单详情的全部区块：订单、收款事件、解析字段、完整上报 JSON、回调任务与每次尝试记录。
export function orderDetailSections(data: OrderDetail): DetailSection[] {
  // 后端异常或响应结构变化时不要整页崩掉：缺失字段一律按空对象渲染成 "-"。
  const order = (data.order ?? {}) as OrderDetail["order"];
  const sections: DetailSection[] = [
    {
      kind: "table",
      title: "订单",
      rows: [
        ["商户订单号", order.merchant_order_no],
        ["系统订单号", order.id],
        ["渠道", PROVIDER_LABELS[order.provider] ?? order.provider],
        ["订单金额", formatFen(order.amount_fen)],
        ["实付金额", formatFen(order.payable_amount_fen)],
        ["订单状态", `${order.status}（${ORDER_STATUS_LABELS[order.status] ?? "-"}）`],
        ["回调状态", `${order.notify_status}（${NOTIFY_STATUS_LABELS[order.notify_status] ?? "-"}）`],
        ["支付时间", formatDateTime(order.paid_at)],
        ["过期时间", formatDateTime(order.expires_at)],
        ["创建时间", formatDateTime(order.created_at)],
        ["回调地址", (order.notify_url as string | null) || "未设置"],
        ["附加数据", (order.attach as string) || "-"],
      ],
    },
  ];

  if (order.paid_source === "MANUAL") {
    sections.push(manualPaymentConfirmationSection(order));
  } else if (data.paymentEvent) {
    const event = data.paymentEvent as Record<string, unknown>;
    sections.push({
      kind: "table",
      title: "收款事件",
      rows: [
        ["事件标识", String(event.id ?? "-")],
        ["监控端", String(event.device_id ?? "-")],
        ["渠道", PROVIDER_LABELS[String(event.provider)] ?? String(event.provider ?? "-")],
        ["金额（分）", String(event.amount_fen ?? "-")],
        ["支付时间", formatDateTime(event.paid_at as number)],
        ["事件状态", String(event.status ?? "-")],
        ["匹配订单", String(event.matched_order_id ?? "-")],
        ["接收时间", formatDateTime(event.created_at as number)],
      ],
    });
  } else {
    sections.push({ kind: "text", title: "收款事件", text: "该订单还没有关联的收款事件。" });
  }

  if (order.paid_source !== "MANUAL") {
    sections.push(...paymentSections(data.paymentDetail, data.paymentRaw));
  }

  if (data.callback) {
    sections.push({
      kind: "table",
      title: "商户回调",
      rows: [
        ["任务状态", `${data.callback.status}（${CALLBACK_STATUS_LABELS[data.callback.status] ?? "-"}）`],
        ["已尝试", data.callback.attempt_count],
        ["最大次数", data.callback.max_attempts],
        ["下次尝试", formatDateTime(data.callback.next_attempt_at)],
        ["最后错误", data.callback.last_error || "-"],
        ["处理状态", data.callback.handled_at ? "管理员已处理" : "未处理"],
        ["处理说明", data.callback.handled_reason || "-"],
        ["处理管理员", data.callback.handled_by || "-"],
        ["处理时间", formatDateTime(data.callback.handled_at)],
        ["更新时间", formatDateTime(data.callback.updated_at)],
      ],
    });
  }
  if (data.callbackAttempts?.length) {
    sections.push({ kind: "attempts", title: "回调尝试记录", attempts: data.callbackAttempts });
  }
  return sections;
}

// 组装未匹配收款事件的详情区块：事件本身、匹配到的订单（若有）、解析字段与完整上报 JSON。
export function eventDetailSections(data: PaymentEventDetail): DetailSection[] {
  const event = (data.event ?? {}) as Record<string, unknown>;
  const sections: DetailSection[] = [
    {
      kind: "table",
      title: "收款事件",
      rows: [
        ["事件标识", String(event.id ?? "-")],
        ["监控端", String(event.device_id ?? "-")],
        ["渠道", PROVIDER_LABELS[String(event.provider)] ?? String(event.provider ?? "-")],
        ["金额（分）", String(event.amount_fen ?? "-")],
        ["支付时间", formatDateTime(event.paid_at as number)],
        ["事件状态", String(event.status ?? "-")],
        ["匹配订单", String(event.matched_order_id ?? "未匹配")],
        ["接收时间", formatDateTime(event.created_at as number)],
      ],
    },
  ];

  if (data.order) {
    const matched = data.order as Record<string, unknown>;
    sections.push({
      kind: "table",
      title: "匹配订单",
      rows: [
        ["商户订单号", String(matched.merchant_order_no ?? "-")],
        ["订单状态", String(matched.status ?? "-")],
        ["实付金额", formatFen(Number(matched.payable_amount_fen ?? 0))],
      ],
    });
  }
  sections.push(...paymentSections(data.detail, data.raw));
  if (data.actions?.length) {
    sections.push({
      kind: "table",
      title: "处理记录",
      rows: data.actions.flatMap((action, index) => [
        [`第 ${data.actions.length - index} 次操作`, PAYMENT_EVENT_ACTION_LABELS[action.action] ?? action.action],
        ["操作者", action.actor],
        ["操作时间", formatDateTime(action.created_at)],
        ...(action.order_id ? [["关联订单", action.order_id] as DetailRow] : []),
        ...(action.reason ? [["处理原因", action.reason] as DetailRow] : []),
      ] as DetailRow[]),
    });
  }
  return sections;
}
