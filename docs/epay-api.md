# 易支付 V1 兼容接口

## 网关配置

- 生产网关基础地址：`https://pay.example.com`
- 易支付插件只要求填写“接口网关”时，填写基础地址，不要附加 `submit.php` 或 `mapi.php`。
- 商户 ID：管理后台“系统设置”中的“易支付商户 ID”。
- 商户密钥：管理后台“系统设置”中的“易支付商户密钥”。
- 请求编码：`application/x-www-form-urlencoded`，字符集 UTF-8。
- 支付方式：支付宝填写 `alipay`，微信填写 `wxpay`。

完整接口：

| 用途 | 方法 | 地址 |
| --- | --- | --- |
| 页面支付 | `POST` 或 `GET` | `https://pay.example.com/submit.php` |
| API 支付 | `POST` 或 `GET` | `https://pay.example.com/mapi.php` |
| 标准订单查询 | `GET` 或 `POST` | `https://pay.example.com/api.php?act=order` |
| 兼容订单查询 | `GET` 或 `POST` | `https://pay.example.com/query.php` |

## 签名规则

1. 删除值为空字符串的参数。
2. 删除 `sign` 和 `sign_type`。
3. 参数名按 ASCII 升序排列。
4. 拼接为 `key=value&key2=value2`，不在末尾增加 `&`。
5. 在拼接结果末尾直接追加商户密钥。
6. 对完整字符串计算 MD5，输出 32 位小写十六进制字符串。

示例伪代码：

```text
sign = md5("money=1.00&name=test&out_trade_no=ORDER1001&pid=1000&type=alipay" + merchant_key)
```

签名按服务端收到的原始参数值计算。生成签名后再由 HTTP 客户端执行表单 URL 编码。

## 创建订单参数

`submit.php` 和 `mapi.php` 使用相同参数：

| 参数 | 必填 | 说明 |
| --- | --- | --- |
| `pid` | 是 | 后台配置的商户 ID |
| `type` | 是 | `alipay` 或 `wxpay` |
| `out_trade_no` | 是 | 商户订单号；同一订单号重复请求时，渠道和金额必须一致 |
| `name` | 是 | 商品名称，最多保留 200 个字符 |
| `money` | 是 | 订单金额，单位元，最多两位小数 |
| `notify_url` | 否 | 支付成功后的服务器异步通知地址，只允许公网 HTTP/HTTPS |
| `return_url` | 否 | 支付完成后的页面返回地址，只允许公网 HTTP/HTTPS |
| `param` | 否 | 商户扩展参数，回调时原样返回，最多保留 500 个字符 |
| `clientip` | 否 | 用户 IP；未填写时使用请求来源 IP |
| `device` | 否 | 设备标识，例如 `pc` 或 `mobile` |
| `sign_type` | 否 | 省略或填写 `MD5`；其他值会被拒绝 |
| `sign` | 是 | 按上述规则生成的 MD5 签名 |

`submit.php` 成功后返回 HTTP 302，浏览器跳转到本站收银台。

`mapi.php` 成功返回：

```json
{
  "code": 1,
  "msg": "success",
  "trade_no": "CFPay系统订单号",
  "out_trade_no": "ORDER1001",
  "payurl": "https://pay.example.com/pay/CFPay系统订单号",
  "qrcode": "https://pay.example.com/pay/CFPay系统订单号",
  "money": "1.00",
  "really_money": "1.01"
}
```

`money` 是商户订单金额，`really_money` 是应用尾差后的实际付款金额。二维码内容应使用 `qrcode` 返回值，页面跳转使用 `payurl`。

## 查询订单

推荐使用易支付常见的密钥查询方式：

```text
GET /api.php?act=order&pid=商户ID&key=商户密钥&out_trade_no=商户订单号
```

也可以传 `trade_no` 查询 CFPay 系统订单号。`query.php` 支持相同参数。查询接口还支持不传 `key`、改用 `sign` 和 `sign_type=MD5` 的签名方式。

成功返回：

```json
{
  "code": 1,
  "msg": "查询订单号成功",
  "trade_no": "CFPay系统订单号",
  "out_trade_no": "ORDER1001",
  "type": "alipay",
  "pid": "1000",
  "money": "1.00",
  "status": 1
}
```

`status=1` 表示已支付，`status=0` 表示未支付、已过期或未完成。

## 支付通知

订单支付成功后，系统使用 HTTP GET 请求 `notify_url`，附带以下参数：

| 参数 | 说明 |
| --- | --- |
| `pid` | 商户 ID |
| `trade_no` | CFPay 系统订单号 |
| `out_trade_no` | 商户订单号 |
| `type` | `alipay` 或 `wxpay` |
| `name` | 商品名称 |
| `money` | 商户订单金额，不是尾差金额 |
| `trade_status` | 固定为 `TRADE_SUCCESS` |
| `param` | 创建订单时传入的扩展参数 |
| `sign_type` | 固定为 `MD5` |
| `sign` | 按相同规则生成的签名 |

商户服务器必须返回纯文本 `success`，忽略大小写并允许首尾空白。其他响应会按 1、5、15、30、60 分钟间隔重试，最多请求 6 次。

## 注意事项

- `notify_url` 和 `return_url` 拒绝 localhost、回环地址和常见内网地址。
- 相同 `out_trade_no`、渠道和金额的重复下单会返回原订单；参数冲突会返回错误。
- 当前网关只支持表单参数，不接受 JSON 请求体。
- MD5 仅用于兼容易支付 V1，管理端和 PC 监控端不使用 MD5。
