export type AdminPageMeta = {
  title: string;
  path: string;
  pageTitle: string;
  description: string;
};

export type AdminNavigationGroup = {
  title: string;
  items: readonly AdminPageMeta[];
};

export type AdminNavigationItem = AdminPageMeta | AdminNavigationGroup;

/** 判断侧栏条目是否为 cffk 风格的可折叠分组。 */
export function isNavigationModule(item: AdminNavigationItem): item is AdminNavigationGroup {
  return "items" in item;
}

export const adminPages = {
  dashboard: {
    title: "面板",
    path: "/dash",
    pageTitle: "面板",
    description: "查看订单、收款、回调与系统用量。",
  },
  wechat: {
    title: "微信渠道",
    path: "/channels/wechat",
    pageTitle: "微信渠道",
    description: "配置微信监控端、登录状态和收款码。",
  },
  alipay: {
    title: "支付宝渠道",
    path: "/channels/alipay",
    pageTitle: "支付宝渠道",
    description: "配置支付宝账务证书、账务查询与支付宝收款码。",
  },
  orders: {
    title: "订单管理",
    path: "/orders",
    pageTitle: "订单管理",
    description: "查看订单支付状态、回调状态与完整详情。",
  },
  unmatched: {
    title: "未匹配收款",
    path: "/unmatched",
    pageTitle: "未匹配收款",
    description: "处理尚未匹配订单的收款记录。",
  },
  quickPay: {
    title: "快捷支付",
    path: "/quick-pay",
    pageTitle: "快捷支付",
    description: "创建订单并检查支付、到账和回调流程。",
  },
  gateway: {
    title: "支付配置",
    path: "/system/gateway",
    pageTitle: "支付配置",
    description: "配置易支付兼容网关、商户凭据与订单金额规则。",
  },
  security: {
    title: "安全配置",
    path: "/system/security",
    pageTitle: "安全配置",
    description: "配置后台入口路径与 Cloudflare Turnstile。",
  },
  site: {
    title: "站点设置",
    path: "/system/site",
    pageTitle: "站点设置",
    description: "配置收银台品牌信息与公开文案。",
  },
  account: {
    title: "账户设置",
    path: "/account",
    pageTitle: "账户设置",
    description: "修改管理员账号与登录密码。",
  },
} satisfies Record<string, AdminPageMeta>;

export const adminNavigation = {
  title: "管理",
  items: [
    adminPages.dashboard,
    { title: "渠道管理", items: [adminPages.wechat, adminPages.alipay] },
    { title: "交易管理", items: [adminPages.orders, adminPages.unmatched, adminPages.quickPay] },
    { title: "系统配置", items: [adminPages.gateway, adminPages.site, adminPages.security] },
  ] satisfies readonly AdminNavigationItem[],
};

const allPages: readonly AdminPageMeta[] = Object.values(adminPages);

/** 规范化 vue-router 路径，便于统一处理尾部斜杠。 */
function normalize(path: string): string {
  const trimmed = path.replace(/\/+$/g, "");
  return trimmed.startsWith("/") ? trimmed || "/" : `/${trimmed}`;
}

/** 根据当前路由返回页面元数据。 */
export function getAdminPageMeta(routePath: string): AdminPageMeta | undefined {
  const target = normalize(routePath);
  return allPages.find((page) => page.path === target);
}

/** 生成与 cffk 一致的“分组 / 页面”面包屑。 */
export function getAdminBreadcrumb(routePath: string): { titles: string[] } {
  const page = getAdminPageMeta(routePath);
  if (!page) return { titles: ["管理后台"] };
  const group = adminNavigation.items.find((item) => isNavigationModule(item) && item.items.some((child) => child.path === page.path));
  return { titles: group ? [group.title, page.title] : [page.title] };
}

/** 判断页面条目是否命中当前路由。 */
export function isAdminNavigationItemActive(item: AdminPageMeta, routePath: string): boolean {
  return normalize(routePath) === item.path;
}

/** 判断分组下是否存在当前路由。 */
export function isAdminNavigationGroupActive(group: AdminNavigationGroup, routePath: string): boolean {
  return group.items.some((item) => isAdminNavigationItemActive(item, routePath));
}
