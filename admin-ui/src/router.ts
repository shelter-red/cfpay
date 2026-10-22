import { createRouter, createWebHistory } from "vue-router";
import { authenticated, refreshSession, sessionReady } from "@/composables/useSession";

const injected = window.__CFPAY_ADMIN_BASE__ ?? "/";
/** 入口路径由 Worker 注入，history base 必须与之对齐，否则刷新子路由会 404。 */
export const adminBase = injected.endsWith("/") ? injected : `${injected}/`;

export const router = createRouter({
  history: createWebHistory(adminBase),
  routes: [
    {
      path: "/",
      name: "login",
      component: () => import("@/pages/LoginPage.vue"),
      meta: { public: true },
    },
    {
      path: "/",
      component: () => import("@/components/layout/AdminLayout.vue"),
      children: [
        { path: "dash", name: "dashboard", component: () => import("@/pages/DashboardPage.vue") },
        { path: "channels/wechat", name: "wechat", component: () => import("@/pages/WechatChannelPage.vue") },
        { path: "channels/alipay", name: "alipay", component: () => import("@/pages/AlipayChannelPage.vue") },
        { path: "orders", name: "orders", component: () => import("@/pages/OrdersPage.vue") },
        { path: "unmatched", name: "unmatched", component: () => import("@/pages/UnmatchedPage.vue") },
        { path: "quick-pay", name: "quick-pay", component: () => import("@/pages/QuickPayPage.vue") },
        { path: "system/gateway", name: "gateway", component: () => import("@/pages/GatewayPage.vue") },
        { path: "system/site", name: "site", component: () => import("@/pages/SiteSettingsPage.vue") },
        { path: "system/security", name: "security", component: () => import("@/pages/SecurityPage.vue") },
        { path: "account", name: "account", component: () => import("@/pages/AccountPage.vue") },
      ],
    },
    { path: "/:pathMatch(.*)*", redirect: { name: "dashboard" } },
  ],
});

router.beforeEach(async (to) => {
  if (!sessionReady.value) await refreshSession();
  if (to.meta.public) return authenticated.value ? { name: "dashboard" } : true;
  if (authenticated.value) return true;
  return { name: "login", query: to.fullPath === "/" ? {} : { redirect: to.fullPath } };
});
