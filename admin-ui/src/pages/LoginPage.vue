<script setup lang="ts">
import { onMounted, ref } from "vue";
import { useRoute, useRouter } from "vue-router";
import { toast } from "vue-sonner";
import { CircleAlertIcon, ShieldCheckIcon, WalletIcon } from "@lucide/vue";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { login } from "@/composables/useSession";
import { useTurnstile } from "@/composables/useTurnstile";
import { errorMessage } from "@/lib/api";

const route = useRoute();
const router = useRouter();

const username = ref("");
const password = ref("");
const loading = ref(false);
const failure = ref("");

const turnstileElement = ref<HTMLElement | null>(null);
const {
  enabled: turnstileEnabled,
  token: turnstileToken,
  loadError: turnstileLoadError,
  init: initTurnstile,
  reset: resetTurnstile,
} = useTurnstile(turnstileElement);

async function onSubmit() {
  failure.value = "";
  if (turnstileEnabled.value && !turnstileToken.value) {
    failure.value = "请先完成人机验证";
    return;
  }
  loading.value = true;
  try {
    await login(username.value, password.value, turnstileToken.value || undefined);
    password.value = "";
    toast.success("登录成功");
    const redirect = typeof route.query.redirect === "string" ? route.query.redirect : "";
    await router.replace(redirect ? { path: redirect } : { name: "dashboard" });
  } catch (error) {
    failure.value = errorMessage(error);
    // 令牌一次性消费，失败后必须重置，否则重试会一直带着已用过的令牌。
    resetTurnstile();
  } finally {
    loading.value = false;
  }
}

onMounted(() => {
  void initTurnstile();
});
</script>

<template>
  <div class="grid min-h-svh place-items-center bg-muted/40 p-4 sm:p-6">
    <Card class="w-full max-w-sm">
      <CardHeader class="text-center">
        <div class="mx-auto mb-2 grid size-11 place-items-center rounded-xl bg-primary text-primary-foreground">
          <WalletIcon class="size-5" />
        </div>
        <CardTitle class="text-xl">CFPay 管理后台</CardTitle>
        <CardDescription>单商户个人收款服务，请登录后继续</CardDescription>
      </CardHeader>
      <CardContent>
        <form class="grid gap-4" @submit.prevent="onSubmit">
          <Alert v-if="failure" variant="destructive">
            <CircleAlertIcon />
            <AlertTitle>登录失败</AlertTitle>
            <AlertDescription class="break-all">{{ failure }}</AlertDescription>
          </Alert>
          <Alert v-if="turnstileLoadError">
            <CircleAlertIcon />
            <AlertTitle>人机验证不可用</AlertTitle>
            <AlertDescription>{{ turnstileLoadError }}</AlertDescription>
          </Alert>

          <div class="grid gap-2">
            <Label for="username">管理员账号</Label>
            <Input id="username" v-model="username" autocomplete="username" required placeholder="请输入管理员账号" />
          </div>
          <div class="grid gap-2">
            <Label for="password">密码</Label>
            <Input
              id="password"
              v-model="password"
              type="password"
              autocomplete="current-password"
              required
              placeholder="请输入密码"
            />
          </div>

          <!-- 未配置 Turnstile 时不渲染容器，也不会加载任何第三方脚本 -->
          <div v-if="turnstileEnabled" ref="turnstileElement" class="flex min-h-16 justify-center" />

          <Button
            type="submit"
            class="w-full"
            :disabled="loading || !username || !password || (turnstileEnabled && !turnstileToken)"
          >
            <ShieldCheckIcon v-if="turnstileEnabled && !turnstileToken" />
            {{ loading ? "登录中…" : turnstileEnabled && !turnstileToken ? "请先完成人机验证" : "登录" }}
          </Button>
        </form>
      </CardContent>
    </Card>
  </div>
</template>
