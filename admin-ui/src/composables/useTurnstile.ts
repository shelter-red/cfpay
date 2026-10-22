import { nextTick, onBeforeUnmount, ref, type Ref } from "vue";

declare global {
  interface Window {
    turnstile?: {
      render: (element: HTMLElement, options: Record<string, unknown>) => string;
      reset: (widgetId?: string) => void;
      remove: (widgetId?: string) => void;
    };
  }
}

const API_SCRIPT_URL = "https://challenges.cloudflare.com/turnstile/v0/api.js?render=explicit";

/**
 * 登录页人机验证。与 cffk 的做法一致：先问后端是否启用，启用才加载官方脚本并显式渲染，
 * 未配置时完全不引入外部脚本，登录页保持零第三方请求。
 */
export function useTurnstile(element: Ref<HTMLElement | null>) {
  const enabled = ref(false);
  const siteKey = ref<string | null>(null);
  const token = ref("");
  const rendered = ref(false);
  const loadError = ref<string | null>(null);
  let widgetId: string | null = null;

  async function ensureScript(): Promise<void> {
    if (window.turnstile) return;
    await new Promise<void>((resolve, reject) => {
      const script = document.createElement("script");
      script.src = API_SCRIPT_URL;
      script.async = true;
      script.onload = () => resolve();
      script.onerror = () => reject(new Error("TURNSTILE_LOAD_FAILED"));
      document.head.append(script);
    });
  }

  function render(): void {
    if (!element.value || !siteKey.value || !window.turnstile) return;
    widgetId =
      window.turnstile.render(element.value, {
        sitekey: siteKey.value,
        callback: (value: unknown) => {
          token.value = typeof value === "string" ? value : "";
        },
        "expired-callback": () => {
          token.value = "";
        },
        "error-callback": () => {
          token.value = "";
        },
      }) ?? null;
    rendered.value = true;
  }

  async function init(): Promise<void> {
    try {
      const response = await fetch("/api/security/turnstile", { cache: "no-store" });
      if (!response.ok) return;
      const config = (await response.json()) as { enabled?: boolean; siteKey?: string | null };
      if (!config.enabled || !config.siteKey) return;
      enabled.value = true;
      siteKey.value = config.siteKey;
      await nextTick();
      await ensureScript();
      render();
    } catch {
      // 组件加载失败不能把管理员彻底锁在门外：后端仍会强制校验，
      // 这里只提示刷新，提交时由服务端给出明确原因。
      loadError.value = "人机验证组件加载失败，请检查网络后刷新页面重试";
    }
  }

  /** 令牌是一次性的，登录失败后必须重置，否则重试会一直拿到已消费的令牌。 */
  function reset(): void {
    token.value = "";
    if (widgetId) window.turnstile?.reset(widgetId);
  }

  onBeforeUnmount(() => {
    if (!widgetId) return;
    try {
      window.turnstile?.remove(widgetId);
    } catch {
      // 脚本未加载完成时 remove 会抛错，卸载阶段无需处理
    }
    widgetId = null;
  });

  return { enabled, siteKey, token, rendered, loadError, init, reset };
}
