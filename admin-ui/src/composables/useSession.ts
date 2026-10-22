import { ref } from "vue";
import { api } from "@/lib/api";
import type { SessionInfo } from "@/lib/types";

export const authenticated = ref(false);
export const sessionUsername = ref<string | null>(null);
export const sessionReady = ref(false);
let sessionRevision = 0;

/** 原子更新本地会话视图，避免账号和登录状态出现短暂不一致。 */
function applySessionState(loggedIn: boolean, username: string | null = null): void {
  authenticated.value = loggedIn;
  sessionUsername.value = loggedIn ? username : null;
}

/** 会话探测不抛错：401 是正常状态，表示需要登录。 */
export async function refreshSession(): Promise<boolean> {
  const revision = ++sessionRevision;
  try {
    const response = await fetch("/admin-api/session", { cache: "no-store", credentials: "same-origin" });
    if (!response.ok) {
      if (revision === sessionRevision) applySessionState(false);
      return false;
    }
    const data = (await response.json()) as SessionInfo;
    const loggedIn = data.ok === true;
    if (revision === sessionRevision) applySessionState(loggedIn, data.username ?? null);
    return revision === sessionRevision ? loggedIn : authenticated.value;
  } catch {
    if (revision === sessionRevision) applySessionState(false);
    return revision === sessionRevision ? false : authenticated.value;
  } finally {
    if (revision === sessionRevision) sessionReady.value = true;
  }
}

// 提交登录；服务端成功响应已经同时签发 Cookie，直接采用该响应建立本地状态，避免紧接着的二次探测偶发失败覆盖登录结果。
export async function login(username: string, password: string, turnstileToken?: string): Promise<void> {
  const headers: Record<string, string> = {};
  if (turnstileToken) headers["x-captcha-response"] = turnstileToken;
  const data = await api<SessionInfo>("/admin-api/login", { method: "POST", headers, body: JSON.stringify({ username, password }) });
  if (data.ok !== true) throw new Error("登录响应无效，请重试");
  sessionRevision += 1;
  applySessionState(true, data.username ?? username);
  sessionReady.value = true;
}

// 退出登录；即使接口失败也要清掉本地会话状态，避免界面停留在已登录视图。
export async function logout(): Promise<void> {
  try {
    await api("/admin-api/logout", { method: "POST" });
  } finally {
    sessionRevision += 1;
    applySessionState(false);
    sessionReady.value = true;
  }
}

// 只清本地会话状态，不发请求。用于修改管理员配置后：服务端已递增会话版本并清了 Cookie。
export function markLoggedOut(): void {
  sessionRevision += 1;
  applySessionState(false);
  sessionReady.value = true;
}
