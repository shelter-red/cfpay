<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { ShieldAlertIcon, ShieldCheckIcon } from "@lucide/vue";
import { useRouter } from "vue-router";
import { toast } from "vue-sonner";
import AdminPageHeader from "@/components/admin/AdminPageHeader.vue";
import SettingSection from "@/components/admin/SettingSection.vue";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { markLoggedOut } from "@/composables/useSession";
import { api, errorMessage } from "@/lib/api";
import type { AdminSettings, TurnstileStatus } from "@/lib/types";

const router = useRouter();
const settings = ref<AdminSettings | null>(null);
const adminPath = ref("");
const saving = ref(false);
const failure = ref("");
const failureTitle = ref("安全配置读取失败");
const turnstile = ref<TurnstileStatus | null>(null);
const turnstileState = computed<"loading" | "disabled" | "incomplete" | "enabled">(() => {
  if (!turnstile.value) return "loading";
  if (turnstile.value.enabled) return "enabled";
  return turnstile.value.siteKeyConfigured || turnstile.value.secretKeyConfigured ? "incomplete" : "disabled";
});
const turnstileTitle = computed(() => turnstileState.value === "loading"
  ? "状态读取中"
  : turnstileState.value === "enabled"
    ? "Turnstile 已启用"
    : turnstileState.value === "disabled"
      ? "Turnstile 未启用"
      : "Turnstile 配置不完整");
const turnstileDescription = computed(() => turnstileState.value === "loading"
  ? "正在读取配置…"
  : turnstileState.value === "enabled"
    ? "后台登录已启用人机验证。"
    : turnstileState.value === "disabled"
      ? "配置站点密钥和私密密钥后启用。"
      : "站点密钥和私密密钥必须同时配置。");

/** 读取后台入口和 Turnstile 状态。 */
async function load(): Promise<void> {
  failure.value = "";
  try {
    const [adminSettings, turnstileStatus] = await Promise.all([
      api<AdminSettings>("/admin-api/admin-settings"),
      api<TurnstileStatus>("/admin-api/security/turnstile"),
    ]);
    settings.value = adminSettings;
    adminPath.value = adminSettings.adminPath;
    turnstile.value = turnstileStatus;
  } catch (error) {
    failureTitle.value = "安全配置读取失败";
    failure.value = errorMessage(error);
  }
}

/** 修改后台入口并跳转到新入口重新登录。 */
async function savePath(): Promise<void> {
  if (!settings.value) return;
  saving.value = true;
  failure.value = "";
  try {
    const data = await api<{ adminPath: string }>("/admin-api/admin-settings", {
      method: "PUT",
      body: JSON.stringify({ username: settings.value.username, adminPath: adminPath.value }),
    });
    markLoggedOut();
    toast.success("后台入口已保存，请重新登录");
    window.setTimeout(() => void router.replace({ path: `/${data.adminPath}` }), 500);
  } catch (error) {
    failureTitle.value = "后台入口保存失败";
    failure.value = errorMessage(error);
  } finally {
    saving.value = false;
  }
}

onMounted(load);
</script>

<template>
  <section class="flex w-full min-w-0 flex-col gap-6">
    <AdminPageHeader />
    <Alert v-if="failure" variant="destructive"><AlertTitle>{{ failureTitle }}</AlertTitle><AlertDescription class="break-words">{{ failure }}</AlertDescription></Alert>

    <SettingSection title="后台入口" description="入口路径用于访问管理后台；修改后当前会话立即失效。">
      <form class="grid min-w-0 gap-3 lg:grid-cols-[minmax(0,1fr)_auto] lg:items-end" @submit.prevent="savePath">
        <div class="grid min-w-0 gap-2"><Label for="admin-path">后台入口路径</Label><div class="flex min-w-0 items-center gap-2"><span class="text-sm text-muted-foreground">/</span><Input id="admin-path" v-model="adminPath" class="min-w-0 flex-1" required /></div></div>
        <Button type="submit" :disabled="saving || !adminPath">{{ saving ? "保存中…" : "保存并重新登录" }}</Button>
      </form>
    </SettingSection>

    <SettingSection title="Cloudflare Turnstile" description="为后台登录增加人机验证，降低自动化撞库风险。">
      <div class="flex min-w-0 items-start gap-3 rounded-lg border p-4" :class="turnstileState === 'incomplete' ? 'border-orange-500/30 bg-orange-500/10' : undefined">
        <ShieldCheckIcon v-if="turnstileState === 'enabled'" class="mt-0.5 size-5 shrink-0" />
        <ShieldAlertIcon v-else class="mt-0.5 size-5 shrink-0" />
        <div class="min-w-0 flex-1">
          <p class="font-medium">{{ turnstileTitle }}</p>
          <p class="mt-1 text-sm text-muted-foreground">{{ turnstileDescription }}</p>
          <div v-if="turnstileState === 'incomplete'" class="mt-2 flex flex-wrap gap-2">
            <Badge variant="outline">站点密钥 {{ turnstile?.siteKeyConfigured ? "已配置" : "缺失" }}</Badge>
            <Badge variant="outline">私密密钥 {{ turnstile?.secretKeyConfigured ? "已配置" : "缺失" }}</Badge>
          </div>
        </div>
      </div>
      <details v-if="turnstileState === 'disabled' || turnstileState === 'incomplete'" class="rounded-lg border bg-muted/30 p-4 text-sm">
        <summary class="cursor-pointer font-medium">查看配置命令</summary>
        <pre class="mt-3 overflow-x-auto rounded bg-background p-3 font-mono text-xs">wrangler secret put TURNSTILE_SITE_KEY
wrangler secret put TURNSTILE_SECRET_KEY</pre>
        <p class="mt-3 text-muted-foreground">私密密钥保存后不显示。</p>
      </details>
    </SettingSection>
  </section>
</template>
