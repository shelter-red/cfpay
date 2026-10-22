<script setup lang="ts">
import { onMounted, ref } from "vue";
import { toast } from "vue-sonner";
import { CircleAlertIcon, LandmarkIcon, Trash2Icon, UploadIcon } from "@lucide/vue";
import AdminPageHeader from "@/components/admin/AdminPageHeader.vue";
import QrCodeCard from "@/components/admin/QrCodeCard.vue";
import SettingSection from "@/components/admin/SettingSection.vue";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Skeleton } from "@/components/ui/skeleton";
import { api, errorMessage, notifyError } from "@/lib/api";
import { formatDateTime } from "@/lib/format";
import type { AlipayStatus } from "@/lib/types";

const status = ref<AlipayStatus | null>(null);
const appId = ref("");
const form = ref<HTMLFormElement | null>(null);
const busy = ref<"" | "save" | "query" | "delete">("");
const failure = ref("");
const failureTitle = ref("支付宝配置读取失败");

const sourceLabel = (value: AlipayStatus["source"]) =>
  value === "database" ? "后台上传" : value === "environment" ? "部署配置" : "未配置";

const fileFields = [
  { key: "appPrivateKey", label: "应用私钥", accept: ".txt,.pem,.key", hint: "应用私钥RSA2048.txt" },
  { key: "appCert", label: "应用公钥证书", accept: ".crt,.pem", hint: "appCertPublicKey_*.crt，App ID 会从文件名自动识别" },
  { key: "alipayPublicCert", label: "支付宝公钥证书", accept: ".crt,.pem", hint: "alipayCertPublicKey_RSA2.crt" },
  { key: "alipayRootCert", label: "支付宝根证书", accept: ".crt,.pem", hint: "alipayRootCert.crt" },
] as const;

async function load() {
  try {
    status.value = await api<AlipayStatus>("/admin-api/alipay/status");
    if (status.value.appId) appId.value = status.value.appId;
    failure.value = "";
  } catch (error) {
    failureTitle.value = "支付宝配置读取失败";
    failure.value = errorMessage(error);
  }
}

function onCertPicked(event: Event) {
  const file = (event.target as HTMLInputElement).files?.[0];
  const match = file?.name.match(/appCertPublicKey_(\d{16,32})\.crt$/i);
  if (match) appId.value = match[1];
}

async function onSubmit() {
  const element = form.value;
  if (!element) return;
  // 先收齐文件再提交：私钥和三份证书缺一不可，否则服务端会报 ALIPAY_CREDENTIAL_FILE_REQUIRED。
  const picked: Array<[string, File]> = [];
  const missing: string[] = [];
  for (const field of fileFields) {
    const file = element.querySelector<HTMLInputElement>(`[data-field="${field.key}"]`)?.files?.[0];
    if (file) picked.push([field.key, file]);
    else missing.push(field.label);
  }
  if (missing.length) {
    toast.error(`请上传完整的支付宝私钥和证书：缺少${missing.join("、")}`);
    return;
  }

  busy.value = "save";
  failure.value = "";
  try {
    const body = new FormData();
    body.set("appId", appId.value);
    for (const [key, file] of picked) body.set(key, file);
    await api("/admin-api/alipay/config", { method: "POST", body });
    element.reset();
    await load();
    toast.success("支付宝证书已保存");
  } catch (error) {
    failureTitle.value = "支付宝证书保存失败";
    failure.value = errorMessage(error);
  } finally {
    busy.value = "";
  }
}

async function onQuery() {
  busy.value = "query";
  try {
    const data = await api<{ totalSize: number; traceId: string; traceSource: "alipay" | "cfpay_request" }>(
      "/admin-api/alipay/query",
      { method: "POST" },
    );
    const traceLabel = data.traceSource === "alipay" ? "支付宝追踪编号" : "CFPay 请求编号";
    toast.success(`账务查询完成，共 ${data.totalSize} 条`, {
      description: `${traceLabel}：${data.traceId}`,
      action: {
        label: "复制编号",
        // 追踪编号只在本次即时通知中展示，按需复制后可到 Worker 日志或支付宝侧排查。
        onClick: () => {
          void navigator.clipboard.writeText(data.traceId).catch(() => toast.error("追踪编号复制失败，请检查浏览器权限"));
        },
      },
    });
  } catch (error) {
    notifyError(error);
  } finally {
    busy.value = "";
  }
}

async function onDelete() {
  if (!window.confirm("确认删除后台上传的支付宝证书和私钥？部署配置不会被修改。")) return;
  busy.value = "delete";
  failure.value = "";
  try {
    await api("/admin-api/alipay/config", { method: "DELETE" });
    form.value?.reset();
    await load();
    toast.success("支付宝上传配置已删除");
  } catch (error) {
    failureTitle.value = "支付宝配置删除失败";
    failure.value = errorMessage(error);
  } finally {
    busy.value = "";
  }
}

onMounted(load);
</script>

<template>
  <section class="flex w-full min-w-0 flex-col gap-6">
    <AdminPageHeader />

    <Alert v-if="status?.poller?.lastRun?.error" variant="destructive">
      <CircleAlertIcon />
      <AlertTitle>账务轮询异常</AlertTitle>
      <AlertDescription>{{ status.poller.lastRun.error }}</AlertDescription>
    </Alert>
    <Alert v-else-if="status?.configured">
      <LandmarkIcon />
      <AlertTitle>支付宝证书已配置</AlertTitle>
      <AlertDescription>
        {{ sourceLabel(status.source) }} · App ID {{ status.appId || "-" }}
        <template v-if="status.updatedAt"> · {{ formatDateTime(status.updatedAt) }}</template>
      </AlertDescription>
    </Alert>
    <Alert v-else>
      <AlertTitle>支付宝证书未配置</AlertTitle>
      <AlertDescription>上传应用私钥与三份证书后，支付宝到账才能通过官方账务查询自动确认。</AlertDescription>
    </Alert>

    <Alert v-if="failure" variant="destructive">
      <AlertTitle>{{ failureTitle }}</AlertTitle>
      <AlertDescription>{{ failure }}</AlertDescription>
    </Alert>

    <SettingSection title="证书与私钥" description="上传应用私钥和三份证书。">
      <form ref="form" class="grid gap-4" @submit.prevent="onSubmit">
        <template v-if="!status">
          <Skeleton class="h-9 w-full max-w-sm" />
          <Skeleton class="h-9 w-full" />
        </template>
        <template v-else>
          <div class="grid w-full min-w-0 max-w-sm gap-2">
            <Label for="alipay-app-id">支付宝 App ID</Label>
            <Input id="alipay-app-id" v-model="appId" inputmode="numeric" required placeholder="16 至 32 位数字" />
          </div>

          <div class="grid min-w-0 gap-4 md:grid-cols-2">
            <div v-for="field in fileFields" :key="field.key" class="grid gap-2">
              <Label :for="`alipay-${field.key}`">{{ field.label }}</Label>
              <Input
                :id="`alipay-${field.key}`"
                :data-field="field.key"
                type="file"
                :accept="field.accept"
                @change="field.key === 'appCert' ? onCertPicked($event) : undefined"
              />
              <p class="text-xs text-muted-foreground">{{ field.hint }}</p>
            </div>
          </div>

          <div class="flex min-w-0 flex-wrap items-center justify-between gap-3 border-t pt-4">
            <Button type="button" variant="destructive" :disabled="busy !== '' || !status.configured || status.source !== 'database'" @click="onDelete">
              <Trash2Icon />{{ busy === "delete" ? "删除中…" : "删除上传配置" }}
            </Button>
            <div class="flex min-w-0 flex-wrap gap-3">
              <Button type="button" variant="outline" :disabled="busy !== ''" @click="onQuery">
                {{ busy === "query" ? "查询中…" : "测试账务查询" }}
              </Button>
              <Button type="submit" :disabled="busy !== ''">
                <UploadIcon />{{ busy === "save" ? "上传校验中…" : "上传并保存" }}
              </Button>
            </div>
          </div>
        </template>
      </form>
    </SettingSection>

    <QrCodeCard provider="alipay" />
  </section>
</template>
