type ErrorHttpStatus = 400 | 401 | 403 | 404 | 409 | 429 | 500 | 502 | 503;

export interface PublicErrorDescriptor {
  code: string;
  message: string;
  status: ErrorHttpStatus;
  unknown: boolean;
}

const ERROR_MESSAGES: Record<string, string> = {
  INVALID_MONEY: "金额格式错误，最多保留两位小数",
  INVALID_FEN: "金额分值必须是非负整数",
  MERCHANT_NOT_CONFIGURED: "商户密钥尚未配置",
  PAYMENT_QR_NOT_CONFIGURED: "当前支付方式的收款码尚未配置",
  MERCHANT_ORDER_CONFLICT: "商户订单号已存在且参数不一致",
  AMOUNT_SLOTS_EXHAUSTED: "当前金额区间订单过多，请稍后重试",
  UNSUPPORTED_PROVIDER: "不支持的支付方式",
  INVALID_URL_PROTOCOL: "地址只允许 HTTP 或 HTTPS",
  PRIVATE_URL_NOT_ALLOWED: "不允许使用内网回调地址",
  MONITOR_NOT_CONFIGURED: "监控端密钥尚未配置",
  MONITOR_AUTH_REQUIRED: "缺少监控端认证参数",
  INVALID_MONITOR_HEADERS: "监控端认证头格式错误",
  INVALID_MONITOR_SIGNATURE: "监控端签名错误",
  MONITOR_TIMESTAMP_EXPIRED: "监控端时间戳已过期",
  MONITOR_REPLAY_DETECTED: "监控端请求重复",
  INVALID_MONITOR_COMPONENT: "监控组件名称无效",
  INVALID_MONITOR_COMPONENT_STATUS: "监控组件状态参数无效",
  MONITOR_COMPONENT_TIMESTAMP_EXPIRED: "监控组件状态时间已过期",
  INVALID_HOOK_OFFSET: "Hook 偏移格式无效",
  INVALID_MONITOR_DETAIL_CODE: "监控组件诊断代码无效",
  INVALID_WECHAT_LOGIN_PAYLOAD: "微信登录状态参数无效",
  INVALID_WECHAT_LOGIN_STATUS: "微信登录状态代码无效",
  WECHAT_LOGIN_TIMESTAMP_EXPIRED: "微信登录状态时间已过期",
  WECHAT_LOGIN_QR_REQUIRED: "微信登录二维码内容缺失",
  INVALID_WECHAT_LOGIN_QR: "微信登录二维码内容格式无效",
  WECHAT_LOGIN_QR_DECRYPT_FAILED: "微信登录二维码当前不可用，请等待监控端刷新",
  INVALID_EVENT_ID: "支付事件编号格式错误",
  INVALID_PAYMENT_EVENT: "支付事件参数不完整",
  INVALID_PAID_AT: "支付时间格式错误",
  PAID_AT_OUT_OF_RANGE: "支付时间超出允许范围",
  PAYMENT_EVENT_CREATE_FAILED: "收款事件写入失败，请检查 D1 状态后重试",
  PAYMENT_EVENT_NOT_FOUND: "收款事件不存在",
  PAYMENT_EVENT_NOT_PENDING: "该收款事件已经处理，请刷新列表后重试",
  PAYMENT_EVENT_NOT_IGNORED: "只有已忽略的收款事件可以恢复",
  PAYMENT_EVENT_IGNORE_REASON_INVALID: "忽略原因必须填写 1 至 200 个字符",
  PAYMENT_EVENT_ORDER_MISMATCH: "只能匹配同渠道、同实付金额的订单",
  PAYMENT_EVENT_MATCH_CONFLICT: "收款事件或订单已被其他操作处理，请刷新后重试",
  ORDER_NOT_FOUND: "订单不存在",
  ORDER_NOT_MATCHABLE: "订单已经支付、关闭或关联了其他收款事件",
  INVALID_ORDER_FILTER: "订单筛选参数无效",
  CALLBACK_NOT_FAILED: "只有回调失败的订单可以人工重试",
  CALLBACK_NOT_OPEN_FAILED: "回调不是未处理的失败状态，请刷新后重试",
  CALLBACK_HANDLE_REASON_INVALID: "处理说明必须为 1 至 200 个字符",
  QUICK_PAY_SUBJECT_INVALID: "商品名称不能超过 200 个字符",
  EXPORT_IDS_INVALID: "请选择 1 至 100 条有效记录",
  EXPORT_FORMAT_INVALID: "导出格式只支持 JSON 或 CSV",
  PAYMENT_EVENT_DELETE_FORBIDDEN: "只有已忽略的收款记录可以永久删除",
  MERCHANT_KEY_TOO_SHORT: "商户密钥至少 16 位",
  MONITOR_SECRET_TOO_SHORT: "监控端密钥至少 32 位",
  ALIPAY_API_NOT_CONFIGURED: "支付宝账务 API 尚未配置",
  ALIPAY_MULTIPART_REQUIRED: "支付宝配置必须使用文件上传",
  ALIPAY_CREDENTIAL_FILE_REQUIRED: "请上传完整的支付宝私钥和三份证书",
  ALIPAY_CREDENTIAL_FILE_TOO_LARGE: "支付宝凭据文件过大",
  INVALID_ALIPAY_APP_ID: "支付宝 App ID 格式错误",
  ALIPAY_APP_ID_FILENAME_MISMATCH: "App ID 与应用证书文件名不一致",
  INVALID_ALIPAY_PRIVATE_KEY: "支付宝应用私钥格式错误",
  INVALID_ALIPAY_CERTIFICATE: "支付宝证书格式错误",
  INVALID_ALIPAY_ROOT_CERTIFICATE: "支付宝根证书格式错误",
  ALIPAY_CERTIFICATE_EXPIRED: "支付宝证书尚未生效或已经过期",
  ALIPAY_PRIVATE_KEY_MISMATCH: "应用私钥与应用证书不匹配",
  ALIPAY_CREDENTIAL_DECRYPT_FAILED: "支付宝配置无法解密，请重新上传",
  ALIPAY_RESPONSE_SIGNATURE_MISSING: "支付宝响应缺少签名",
  ALIPAY_RESPONSE_CERT_MISMATCH: "支付宝响应证书与已配置证书不匹配",
  ALIPAY_RESPONSE_SIGNATURE_INVALID: "支付宝响应签名校验失败",
  QR_PAYLOAD_REQUIRED: "没有识别到二维码内容",
  INVALID_QR_PAYLOAD: "二维码内容格式无效",
  QR_PROVIDER_MISMATCH: "二维码内容与所选支付渠道不匹配",
  INVALID_QR_IMAGE_URL: "收款码图片地址与支付渠道不匹配",
  ADMIN_USERNAME_REQUIRED: "管理员账号不能为空",
  ADMIN_PATH_REQUIRED: "后台入口不能为空",
  ADMIN_PATH_RESERVED: "后台入口与系统接口或静态资源同名，请换一个",
  INVALID_ADMIN_PASSWORD: "管理员密码必须为 12 至 128 位",
  CURRENT_ADMIN_PASSWORD_REQUIRED: "请输入当前管理员密码",
  CURRENT_ADMIN_PASSWORD_MISMATCH: "当前管理员密码不正确",
  TURNSTILE_REQUIRED: "请先完成人机验证",
  TURNSTILE_REJECTED: "人机验证未通过，请刷新后重试",
  TURNSTILE_VERIFY_UNAVAILABLE: "人机验证服务暂时不可用，请稍后重试",
  INVALID_ADMIN_PASSWORD_ITERATIONS: "密码哈希迭代次数必须是 100,000 的整数倍且不超过 1,000,000",
  INVALID_MERCHANT_ID: "商户 ID 格式错误",
  INVALID_INTEGER: "数字配置超出允许范围",
  SITE_NAME_REQUIRED: "站点名称不能为空且不能超过 120 个字符",
  SITE_SUBTITLE_INVALID: "站点副标题不能超过 300 个字符",
  FOOTER_TEXT_INVALID: "页脚文案不能超过 1000 个字符",
  SITE_FAVICON_INVALID: "站点图标必须是 512 KB 以内的 PNG、JPEG、WebP 或 ICO 图片",
  INVALID_PAYLOAD: "请求数据格式无效",
  SESSION_SECRET_NOT_CONFIGURED: "会话加密密钥尚未配置",
  CALLBACK_URL_MISSING: "商户回调地址缺失",
  ORDER_CREATE_FAILED: "订单创建失败，数据库未返回订单数据，请稍后重试",
  SYSTEM_SETTING_NOT_FOUND: "系统初始化数据不存在，请执行数据库迁移",
  CLOUDFLARE_ANALYTICS_NOT_CONFIGURED: "D1 用量查询尚未配置，请设置账户 ID、数据库 ID 和 Analytics Token",
  CLOUDFLARE_ANALYTICS_FORBIDDEN: "Cloudflare Analytics Token 无权限，请授予 Account / Account Analytics / Read",
  CLOUDFLARE_ANALYTICS_UNAVAILABLE: "Cloudflare Analytics 服务暂时不可用，请稍后重试",
  CLOUDFLARE_ANALYTICS_QUERY_FAILED: "Cloudflare Analytics 查询失败，请检查账户 ID 和数据库 ID",
  CLOUDFLARE_ANALYTICS_RANGE_TOO_WIDE: "Cloudflare D1 Analytics 仅保留最近约 31 天数据，查询时间范围不能超过平台限制",
  CLOUDFLARE_ANALYTICS_ACCOUNT_NOT_FOUND: "Cloudflare Analytics 未找到指定账户或数据库",
  D1_WRITE_LIMIT_EXCEEDED: "Cloudflare D1 当前计费日写入额度已用尽，将于北京时间每日 08:00（UTC 00:00）重置；真实订单写入需等待额度恢复或升级套餐",
  D1_READ_LIMIT_EXCEEDED: "Cloudflare D1 当前计费日读取额度已用尽，将于北京时间每日 08:00（UTC 00:00）重置",
  D1_QUERY_PARAMETER_MISMATCH: "数据库查询参数数量不匹配，这是服务端 SQL 错误，请使用追踪编号联系维护人员",
  D1_SCHEMA_MISMATCH: "数据库结构与当前程序不一致，请执行缺失的 D1 迁移",
  D1_CONSTRAINT_FAILED: "保存内容不满足数据库约束，请检查字段长度、格式和唯一性",
  D1_UNAVAILABLE: "Cloudflare D1 暂时不可用，请稍后重试",
  REQUEST_TIMEOUT: "请求处理超时，请稍后重试",
  INVALID_JSON: "请求体不是有效的 JSON",
};

// 从运行时原始异常中识别稳定错误码，优先处理 D1 配额和 SQL 故障等动态文本。
function normalizeErrorCode(raw: string): string {
  if (ERROR_MESSAGES[raw]) return raw;
  if (raw.startsWith("CLOUDFLARE_ANALYTICS_QUERY_FAILED:")) return "CLOUDFLARE_ANALYTICS_QUERY_FAILED";
  if (/exceeded D1.*row write limit|daily row write limit/i.test(raw)) return "D1_WRITE_LIMIT_EXCEEDED";
  if (/exceeded D1.*row read limit|daily row read limit/i.test(raw)) return "D1_READ_LIMIT_EXCEEDED";
  if (/Wrong number of parameter bindings/i.test(raw)) return "D1_QUERY_PARAMETER_MISMATCH";
  if (/no such (table|column)|has no column named/i.test(raw)) return "D1_SCHEMA_MISMATCH";
  if (/CHECK constraint failed|UNIQUE constraint failed|SQLITE_CONSTRAINT/i.test(raw)) return "D1_CONSTRAINT_FAILED";
  if (/D1_ERROR|SQLITE_BUSY|database is locked|database unavailable/i.test(raw)) return "D1_UNAVAILABLE";
  if (/AbortError|operation was aborted|timed? out/i.test(raw)) return "REQUEST_TIMEOUT";
  if (/Unexpected token|Unexpected end of JSON input|JSON.*position|not valid JSON/i.test(raw)) return "INVALID_JSON";
  if (/^ALIPAY_HTTP_\d{3}:/.test(raw)) return "ALIPAY_UPSTREAM_ERROR";
  if (/^MONITOR_STATE_HTTP_\d{3}$/.test(raw)) return "MONITOR_STATE_UNAVAILABLE";
  if (/^[A-Z][A-Z0-9_]{2,80}$/.test(raw)) return raw;
  return "UNKNOWN_ERROR";
}

// 根据稳定错误码选择 HTTP 状态，调用方无需再从中文文案反推错误类型。
function statusFor(code: string): ErrorHttpStatus {
  if (["PAYMENT_EVENT_NOT_FOUND", "ORDER_NOT_FOUND"].includes(code)) return 404;
  if (["EXPORT_IDS_INVALID", "EXPORT_FORMAT_INVALID"].includes(code)) return 400;
  if (code === "CLOUDFLARE_ANALYTICS_FORBIDDEN") return 403;
  if (code.includes("SIGNATURE") || code.includes("AUTH_REQUIRED")) return 401;
  if (code.includes("CONFLICT") || code.includes("REPLAY") || ["PAYMENT_EVENT_NOT_PENDING", "PAYMENT_EVENT_NOT_IGNORED", "PAYMENT_EVENT_DELETE_FORBIDDEN", "ORDER_NOT_MATCHABLE", "CALLBACK_NOT_FAILED", "CALLBACK_NOT_OPEN_FAILED"].includes(code)) return 409;
  if (code === "D1_WRITE_LIMIT_EXCEEDED" || code === "D1_READ_LIMIT_EXCEEDED") return 429;
  if (code.endsWith("NOT_CONFIGURED") || code.endsWith("UNAVAILABLE") || code === "TURNSTILE_VERIFY_UNAVAILABLE") return 503;
  if (code === "ALIPAY_UPSTREAM_ERROR") return 502;
  if (["SYSTEM_SETTING_NOT_FOUND", "ORDER_CREATE_FAILED", "PAYMENT_EVENT_CREATE_FAILED", "D1_QUERY_PARAMETER_MISMATCH", "D1_SCHEMA_MISMATCH"].includes(code)) return 500;
  return 400;
}

// 把任意异常转换成可公开的中文原因、稳定错误码和状态码，不泄露 SQL、令牌或堆栈。
export function describePublicError(error: unknown): PublicErrorDescriptor {
  const raw = error instanceof Error ? error.message : String(error || "UNKNOWN_ERROR");
  const code = normalizeErrorCode(raw);
  if (code === "ALIPAY_UPSTREAM_ERROR") {
    const match = /^ALIPAY_HTTP_(\d{3}):(.*)$/.exec(raw);
    const detail = match?.[2]?.trim();
    return { code, message: `支付宝接口请求失败（HTTP ${match?.[1] ?? "未知"}）${detail ? `：${detail.slice(0, 160)}` : ""}`, status: 502, unknown: false };
  }
  if (code === "MONITOR_STATE_UNAVAILABLE") {
    return { code, message: "监控状态内存服务暂时不可用，请稍后重试", status: 503, unknown: false };
  }
  if (code === "CLOUDFLARE_ANALYTICS_QUERY_FAILED") {
    const detail = raw.slice("CLOUDFLARE_ANALYTICS_QUERY_FAILED:".length).trim();
    return { code, message: `Cloudflare Analytics 查询失败${detail ? `：${detail.slice(0, 300)}` : ""}`, status: 400, unknown: false };
  }
  const message = ERROR_MESSAGES[code];
  if (message) return { code, message, status: statusFor(code), unknown: false };
  if (code !== "UNKNOWN_ERROR") {
    return { code, message: `服务拒绝了该请求，错误代码：${code}`, status: statusFor(code), unknown: false };
  }
  return { code, message: "服务器发生未识别异常，请使用追踪编号联系维护人员", status: 500, unknown: true };
}
