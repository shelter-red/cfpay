<script setup lang="ts">
import { onMounted, ref } from "vue";
import { toast } from "vue-sonner";
import { DownloadIcon, InfoIcon, QrCodeIcon, RefreshCwIcon } from "@lucide/vue";
import AdminDataTable, { type AdminTableColumn } from "@/components/admin/AdminDataTable.vue";
import AdminPageHeader from "@/components/admin/AdminPageHeader.vue";
import CopyButton from "@/components/admin/CopyButton.vue";
import QrCodeCard from "@/components/admin/QrCodeCard.vue";
import SecretDialog from "@/components/admin/SecretDialog.vue";
import SettingSection from "@/components/admin/SettingSection.vue";
import StatusBadge from "@/components/admin/StatusBadge.vue";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle } from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Separator } from "@/components/ui/separator";
import { Skeleton } from "@/components/ui/skeleton";
import { useMonitors } from "@/composables/useMonitors";
import { api, notifyError } from "@/lib/api";
import {
  COMPONENT_NAMES, COMPONENT_STATUS_LABELS, formatCountdown, formatDateTime, formatRelative, WECHAT_LOGIN_LABELS,
} from "@/lib/format";
import { randomSecret } from "@/lib/secrets";
import type { MonitorComponent } from "@/lib/types";

const {
  devices, components, onlineWindowMs, loaded, refreshing, failure, nowMs, realtimeConnected,
  qrDialogOpen, qrSession, componentsOf, sessionOf, isStale, openQr, reload, start,
} = useMonitors();

const monitorSecret = ref("");
const monitorSecretConfigured = ref(false);
const savingSecret = ref(false);
const secretDialogOpen = ref(false);
const secretValue = ref("");
const versionDialogOpen = ref(false);
const monitorDownload = __WECHAT_MONITOR_DOWNLOAD__;
const monitorDownloadUrl = `${import.meta.env.BASE_URL}downloads/${encodeURIComponent(monitorDownload.filename)}`;

const componentColumns: AdminTableColumn<MonitorComponent>[] = [
  { key: "component", label: "组件", class: "w-28" },
  { key: "status", label: "状态", class: "w-24" },
  { key: "detail_code", label: "诊断码", class: "font-mono text-xs" },
  { key: "detail_message", label: "状态说明", class: "text-xs text-muted-foreground" },
  { key: "target_version", label: "目标版本", class: "font-mono text-xs" },
  { key: "hook_offset", label: "Hook 偏移", class: "font-mono text-xs" },
  { key: "process_id", label: "微信进程 ID", class: "w-28" },
  { key: "last_event_at", label: "最近收款时间", class: "w-40 text-xs" },
  { key: "reported_at", label: "最近上报时间", class: "w-40 text-xs" },
];

async function loadSecretState() {
  try {
    const data = await api<{ monitorSecretConfigured: boolean }>("/admin-api/settings");
    monitorSecretConfigured.value = data.monitorSecretConfigured;
  } catch {
    // 密钥状态读取失败不影响设备列表展示
  }
}

function generateSecret() {
  secretValue.value = randomSecret();
  monitorSecret.value = secretValue.value;
  secretDialogOpen.value = true;
}

async function saveSecret() {
  savingSecret.value = true;
  try {
    await api("/admin-api/settings", { method: "PUT", body: JSON.stringify({ monitorSecret: monitorSecret.value }) });
    monitorSecret.value = "";
    monitorSecretConfigured.value = true;
    toast.success("监控端密钥已保存");
  } catch (error) {
    notifyError(error);
  } finally {
    savingSecret.value = false;
  }
}

async function onRefresh() {
  await reload(true);
}

/** 下载当前支持版本的 Windows 监控端。 */
function downloadMonitor() {
  const anchor = document.createElement("a");
  anchor.href = monitorDownloadUrl;
  anchor.download = monitorDownload.filename;
  anchor.rel = "noopener";
  anchor.click();
}

/** 把监控端字节大小格式化成便于管理员确认的 MiB。 */
function formatMonitorSize(bytes: number): string {
  return `${(bytes / 1024 / 1024).toFixed(2)} MiB`;
}

onMounted(() => {
  start();
  void loadSecretState();
});
</script>

<template>
  <section class="flex w-full min-w-0 flex-col gap-6">
    <AdminPageHeader>
      <template #actions>
        <Button variant="outline" size="sm" :disabled="refreshing" @click="onRefresh">
          <RefreshCwIcon :class="refreshing ? 'animate-spin' : undefined" />刷新
        </Button>
      </template>
    </AdminPageHeader>

    <Alert v-if="failure" variant="destructive">
      <AlertTitle>监控端状态读取失败</AlertTitle>
      <AlertDescription class="flex min-w-0 flex-wrap items-center gap-3">
        <span class="break-all">{{ failure }}</span>
        <Button size="sm" variant="outline" @click="reload()">重试</Button>
      </AlertDescription>
    </Alert>

    <SettingSection title="监控端接入" description="下载监控端并配置接入密钥。">
      <div class="flex min-w-0 flex-col gap-3 rounded-lg border p-3 sm:flex-row sm:items-center sm:justify-between">
        <div class="min-w-0">
          <p class="font-medium">Windows 监控端</p>
          <p class="mt-1 break-words text-xs text-muted-foreground">
            v{{ monitorDownload.monitorVersion }} · 微信 {{ monitorDownload.weixinVersion }} · {{ formatMonitorSize(monitorDownload.size) }}
          </p>
        </div>
        <div class="flex shrink-0 flex-wrap gap-2">
          <Button type="button" variant="outline" size="sm" @click="versionDialogOpen = true"><InfoIcon />版本信息</Button>
          <Button type="button" size="sm" @click="downloadMonitor"><DownloadIcon />下载</Button>
        </div>
      </div>

      <Separator />

      <form class="grid min-w-0 gap-3 lg:grid-cols-[minmax(0,48rem)_auto] lg:items-end" @submit.prevent="saveSecret">
        <div class="grid min-w-0 gap-2">
          <Label for="monitor-secret">PC 监控端密钥</Label>
          <div class="flex min-w-0 gap-2">
            <Input
              id="monitor-secret"
              v-model="monitorSecret"
              type="password"
              autocomplete="off"
              class="min-w-0 flex-1"
              :placeholder="monitorSecretConfigured ? '已配置，留空不修改（至少 32 位）' : '至少 32 位'"
            />
            <Button type="button" variant="outline" class="shrink-0" @click="generateSecret">生成</Button>
          </div>
        </div>
        <Button type="submit" :disabled="savingSecret || !monitorSecret">
          {{ savingSecret ? "保存中…" : "保存密钥" }}
        </Button>
      </form>
    </SettingSection>

    <div class="grid min-w-0 gap-3">
      <div class="flex min-w-0 flex-wrap items-end justify-between gap-3">
        <div class="min-w-0">
          <h2 class="text-xl font-semibold tracking-normal">在线监控端</h2>
          <p class="mt-1 text-sm text-muted-foreground">{{ Math.round(onlineWindowMs / 1000) }} 秒未上报即离线。</p>
        </div>
        <div class="flex flex-wrap items-center gap-2">
          <Badge variant="outline">{{ realtimeConnected ? "实时同步" : "轮询同步" }}</Badge>
          <Badge variant="outline">{{ devices.length }} 台在线</Badge>
        </div>
      </div>

      <Skeleton v-if="!loaded" class="h-40 w-full" />
      <p v-else-if="!devices.length" class="rounded-lg border border-dashed p-8 text-center text-sm text-muted-foreground">
        暂无在线监控端，收到上报后自动显示
      </p>

      <template v-else>
        <div v-for="device in devices" :key="device.id" class="min-w-0 rounded-xl border bg-card p-4 shadow-xs">
          <div class="flex min-w-0 flex-wrap items-start justify-between gap-3">
            <div class="min-w-0">
              <div class="flex min-w-0 flex-wrap items-center gap-2">
                <span class="min-w-0 break-words font-medium">{{ device.name || device.id }}</span>
                <code class="min-w-0 max-w-full break-all rounded bg-muted px-1.5 py-0.5 font-mono text-xs">{{ device.id }}</code>
              </div>
              <p class="mt-1 text-xs text-muted-foreground">上报于 {{ formatRelative(device.last_heartbeat_at, nowMs) }}</p>
            </div>
            <StatusBadge value="online" label="在线" />
          </div>

          <dl class="mt-4 grid min-w-0 gap-3 text-sm sm:grid-cols-2 lg:grid-cols-4">
            <div class="min-w-0"><dt class="text-xs text-muted-foreground">平台</dt><dd class="break-all">{{ device.platform || "-" }}</dd></div>
            <div class="min-w-0"><dt class="text-xs text-muted-foreground">监控端版本</dt><dd class="break-all font-mono text-xs">{{ device.version || "-" }}</dd></div>
            <div class="min-w-0"><dt class="text-xs text-muted-foreground">客户端地址</dt><dd class="break-all font-mono text-xs">{{ device.last_ip || "-" }}</dd></div>
            <div class="min-w-0">
              <dt class="text-xs text-muted-foreground">微信登录</dt>
              <dd class="mt-1 flex min-w-0 flex-wrap items-center gap-2">
                <template v-if="sessionOf(device.id)">
                  <StatusBadge
                    :value="sessionOf(device.id)!.status"
                    :label="WECHAT_LOGIN_LABELS[sessionOf(device.id)!.status] ?? sessionOf(device.id)!.status"
                  />
                  <span class="text-xs text-muted-foreground">{{ formatDateTime(sessionOf(device.id)!.observedAt) }}</span>
                </template>
                <span v-else class="text-xs text-muted-foreground">尚未收到微信登录状态</span>
                <Button
                  v-if="sessionOf(device.id)?.qrAvailable && sessionOf(device.id)?.qrUrl && Number(sessionOf(device.id)?.expiresAt) > nowMs"
                  type="button"
                  size="sm"
                  variant="outline"
                  class="h-auto max-w-full whitespace-normal text-left"
                  @click="openQr(sessionOf(device.id)!)"
                >
                  <QrCodeIcon />显示登录二维码（剩余 {{ formatCountdown(Number(sessionOf(device.id)?.expiresAt), nowMs) }}）
                </Button>
              </dd>
            </div>
          </dl>

          <Separator class="my-4" />

          <AdminDataTable
            :columns="componentColumns"
            :rows="componentsOf(device.id)"
            :row-key="(row: MonitorComponent) => `${row.device_id}-${row.component}`"
            empty-text="尚未收到 Hook 状态"
          >
            <template #cell-component="{ value }">{{ COMPONENT_NAMES[String(value)] ?? value }}</template>
            <template #cell-status="{ row }">
              <StatusBadge
                :value="isStale(row as MonitorComponent) ? 'stale' : String((row as MonitorComponent).status)"
                :label="isStale(row as MonitorComponent) ? '上报超时' : (COMPONENT_STATUS_LABELS[String((row as MonitorComponent).status)] ?? String((row as MonitorComponent).status))"
              />
            </template>
            <template #cell-detail_code="{ value }"><span class="font-mono text-xs">{{ value || "-" }}</span></template>
            <template #cell-detail_message="{ value }">
              <!-- 长说明整行换行显示，不用省略号：截断会让管理员看不到完整的诊断原因 -->
              <span class="block break-words">{{ value || "-" }}</span>
            </template>
            <template #cell-last_event_at="{ value }">{{ value ? formatDateTime(Number(value)) : "-" }}</template>
            <template #cell-reported_at="{ value }">{{ formatDateTime(Number(value)) }}</template>
          </AdminDataTable>
        </div>
      </template>
    </div>

    <QrCodeCard provider="wxpay" />

    <Dialog v-model:open="qrDialogOpen">
      <DialogContent class="w-[calc(100vw-2rem)] max-w-[calc(100vw-2rem)] sm:max-w-sm">
        <DialogHeader>
          <DialogTitle>微信登录二维码</DialogTitle>
          <DialogDescription>
            {{ qrSession?.qrUrl && Number(qrSession.expiresAt) > nowMs
              ? `剩余有效时间 ${formatCountdown(Number(qrSession.expiresAt), nowMs)}，登录成功后自动关闭。`
              : "二维码已失效，等待监控端上报新的二维码。" }}
          </DialogDescription>
        </DialogHeader>
        <div class="grid place-items-center rounded-lg border bg-white p-4">
          <img v-if="qrSession?.qrUrl" :src="qrSession.qrUrl" alt="微信登录二维码" class="size-56 max-w-full object-contain" />
          <span v-else class="py-16 text-sm text-muted-foreground">等待二维码</span>
        </div>
      </DialogContent>
    </Dialog>

    <Dialog v-model:open="versionDialogOpen">
      <DialogContent class="sm:max-w-xl">
        <DialogHeader>
          <DialogTitle>Windows 监控端版本</DialogTitle>
          <DialogDescription>核对版本或文件完整性。</DialogDescription>
        </DialogHeader>
        <dl class="grid min-w-0 gap-3 text-sm sm:grid-cols-[8rem_minmax(0,1fr)] sm:items-center">
          <dt class="text-muted-foreground">监控端版本</dt><dd class="font-mono">{{ monitorDownload.monitorVersion }}</dd>
          <dt class="text-muted-foreground">支持微信版本</dt><dd class="font-mono">{{ monitorDownload.weixinVersion }}</dd>
          <dt class="text-muted-foreground">文件大小</dt><dd>{{ formatMonitorSize(monitorDownload.size) }}</dd>
          <dt class="text-muted-foreground">文件名</dt>
          <dd class="flex min-w-0 items-center gap-2"><code class="min-w-0 flex-1 break-all text-xs">{{ monitorDownload.filename }}</code><CopyButton :value="monitorDownload.filename" size="icon" label="" /></dd>
          <dt class="text-muted-foreground">SHA-256</dt>
          <dd class="flex min-w-0 items-center gap-2"><code class="min-w-0 flex-1 break-all text-xs">{{ monitorDownload.sha256 }}</code><CopyButton :value="monitorDownload.sha256" size="icon" label="" /></dd>
        </dl>
      </DialogContent>
    </Dialog>

    <SecretDialog
      v-model:open="secretDialogOpen"
      title="保存 PC 监控端密钥"
      label="新 PC 监控端密钥"
      :value="secretValue"
      saveHint="再点击「保存监控端密钥」。"
    />
  </section>
</template>
