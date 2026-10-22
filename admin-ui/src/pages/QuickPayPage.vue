<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import { useRoute, useRouter } from "vue-router";
import { toast } from "vue-sonner";
import { CheckCircle2Icon, CircleIcon, ExternalLinkIcon, LoaderCircleIcon, RefreshCwIcon, XCircleIcon } from "@lucide/vue";
import AdminPageHeader from "@/components/admin/AdminPageHeader.vue";
import CopyButton from "@/components/admin/CopyButton.vue";
import SettingSection from "@/components/admin/SettingSection.vue";
import StatusBadge from "@/components/admin/StatusBadge.vue";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { NativeSelect, NativeSelectOption } from "@/components/ui/native-select";
import { api, errorMessage } from "@/lib/api";
import { formatDateTime, formatFen, NOTIFY_STATUS_LABELS, ORDER_STATUS_LABELS, prettyJson, PROVIDER_LABELS } from "@/lib/format";
import type { Provider, QuickPayRun } from "@/lib/types";

const POLL_INTERVAL_MS = 2_000;
const route = useRoute();
const router = useRouter();
const subject = ref("CFPay 快捷支付");
const provider = ref<Provider>("wxpay");
const amount = ref("0.01");
const creating = ref(false);
const refreshing = ref(false);
const failure = ref("");
const failureTitle = ref("快捷支付状态读取失败");
const run = ref<QuickPayRun | null>(null);
let pollTimer: ReturnType<typeof setTimeout> | null = null;
let loadSequence = 0;

const isTerminal = computed(() => {
  const orderStatus = run.value?.order.status;
  const callbackStatus = run.value?.callback?.status;
  return Boolean(run.value?.completed || orderStatus === "CLOSED" || orderStatus === "EXPIRED" || callbackStatus === "FAILED");
});

/** 清除当前诊断轮询，避免切换订单后旧请求继续运行。 */
function clearPoll(): void {
  if (pollTimer) clearTimeout(pollTimer);
  pollTimer = null;
}

/** 根据订单是否进入终态安排下一次诊断查询。 */
function schedulePoll(): void {
  clearPoll();
  if (!run.value || isTerminal.value || document.visibilityState !== "visible") return;
  pollTimer = setTimeout(() => void loadRun(run.value!.order.id, true), POLL_INTERVAL_MS);
}

/** 读取普通订单的六阶段诊断数据。 */
async function loadRun(orderId: string, silent = false): Promise<void> {
  const sequence = ++loadSequence;
  if (!silent) refreshing.value = true;
  try {
    const data = await api<QuickPayRun>(`/admin-api/quick-pay/orders/${encodeURIComponent(orderId)}/diagnostics`);
    if (sequence !== loadSequence) return;
    run.value = data;
    failure.value = "";
  } catch (error) {
    if (sequence !== loadSequence) return;
    failureTitle.value = "快捷支付状态读取失败";
    failure.value = errorMessage(error);
  } finally {
    if (sequence !== loadSequence) return;
    refreshing.value = false;
    schedulePoll();
  }
}

/** 使用当前临时商品信息创建一笔普通支付订单。 */
async function createOrder(): Promise<void> {
  creating.value = true;
  failure.value = "";
  clearPoll();
  loadSequence += 1;
  try {
    const data = await api<{ order: QuickPayRun["order"]; payUrl: string }>("/admin-api/quick-pay/orders", {
      method: "POST",
      body: JSON.stringify({ provider: provider.value, amount: amount.value, subject: subject.value }),
    });
    await router.replace({ name: "quick-pay", query: { order: data.order.id } });
    await loadRun(data.order.id, true);
    toast.success("普通商品订单已创建，请打开收银台完成真实支付");
  } catch (error) {
    failureTitle.value = "快捷支付订单创建失败";
    failure.value = errorMessage(error);
  } finally {
    creating.value = false;
  }
}

/** 在新标签页打开当前订单的正式收银台。 */
function openCashier(): void {
  if (run.value?.payUrl) window.open(run.value.payUrl, "_blank", "noopener,noreferrer");
}

/** 返回默认商品表单，不删除已经创建的普通订单。 */
async function startAnother(): Promise<void> {
  clearPoll();
  loadSequence += 1;
  run.value = null;
  subject.value = "CFPay 快捷支付";
  provider.value = "wxpay";
  amount.value = "0.01";
  failure.value = "";
  await router.replace({ name: "quick-pay" });
}

/** 手动刷新当前订单的全部诊断阶段。 */
async function refreshRun(): Promise<void> {
  if (!run.value) return;
  await loadRun(run.value.order.id);
}

/** 页面不可见时暂停诊断轮询，重新显示后立即补查一次。 */
function handleVisibilityChange(): void {
  if (document.visibilityState !== "visible") {
    clearPoll();
    return;
  }
  if (run.value && !isTerminal.value) void loadRun(run.value.order.id, true);
}

onMounted(() => {
  document.addEventListener("visibilitychange", handleVisibilityChange);
  const orderId = typeof route.query.order === "string" ? route.query.order : "";
  if (orderId) void loadRun(orderId);
});

onUnmounted(() => {
  clearPoll();
  loadSequence += 1;
  document.removeEventListener("visibilitychange", handleVisibilityChange);
});
</script>

<template>
  <section class="flex w-full min-w-0 flex-col gap-6">
    <AdminPageHeader>
      <template #actions>
        <Button v-if="run" variant="outline" size="sm" :disabled="refreshing" @click="refreshRun">
          <RefreshCwIcon :class="refreshing ? 'animate-spin' : undefined" />刷新
        </Button>
      </template>
    </AdminPageHeader>

    <Alert v-if="failure" variant="destructive">
      <AlertTitle>{{ failureTitle }}</AlertTitle>
      <AlertDescription class="break-words">{{ failure }}</AlertDescription>
    </Alert>

    <SettingSection v-if="!run" title="商品信息" description="仅用于本次下单。">
      <form class="grid min-w-0 gap-4 sm:grid-cols-2" @submit.prevent="createOrder">
        <div class="grid min-w-0 gap-2 sm:col-span-2">
          <Label for="quick-pay-subject">商品名称</Label>
          <Input id="quick-pay-subject" v-model="subject" maxlength="200" required />
        </div>
        <div class="grid min-w-0 gap-2">
          <Label for="quick-pay-provider">支付渠道</Label>
          <NativeSelect id="quick-pay-provider" v-model="provider" class="w-full">
            <NativeSelectOption value="wxpay">微信支付</NativeSelectOption>
            <NativeSelectOption value="alipay">支付宝</NativeSelectOption>
          </NativeSelect>
        </div>
        <div class="grid min-w-0 gap-2">
          <Label for="quick-pay-amount">商品金额</Label>
          <Input id="quick-pay-amount" v-model="amount" inputmode="decimal" placeholder="0.01" required />
        </div>
        <div class="sm:col-span-2">
          <Button type="submit" :disabled="creating || !subject.trim()">
            <LoaderCircleIcon v-if="creating" class="animate-spin" />{{ creating ? "正在创建…" : "创建订单" }}
          </Button>
        </div>
      </form>
    </SettingSection>

    <template v-else>
      <SettingSection title="商品订单" :status="run.completed ? '完整流程已完成' : isTerminal ? '流程已停止' : '流程进行中'">
        <div class="grid min-w-0 gap-4 sm:grid-cols-2 xl:grid-cols-4">
          <div class="min-w-0 rounded-lg border p-4 sm:col-span-2"><p class="text-xs text-muted-foreground">商品名称</p><p class="mt-1 break-words font-medium">{{ run.order.subject }}</p></div>
          <div class="min-w-0 rounded-lg border p-4"><p class="text-xs text-muted-foreground">支付渠道</p><p class="mt-1 font-medium">{{ PROVIDER_LABELS[run.order.provider] }}</p></div>
          <div class="min-w-0 rounded-lg border p-4"><p class="text-xs text-muted-foreground">实付金额</p><p class="mt-1 text-lg font-semibold">{{ formatFen(run.order.payable_amount_fen) }}</p></div>
        </div>
        <div class="grid min-w-0 gap-3 rounded-lg border p-4 text-sm">
          <div class="grid min-w-0 gap-1 sm:grid-cols-[8rem_minmax(0,1fr)]"><span class="text-muted-foreground">系统订单号</span><code class="break-all">{{ run.order.id }}</code></div>
          <div class="grid min-w-0 gap-1 sm:grid-cols-[8rem_minmax(0,1fr)]"><span class="text-muted-foreground">商户订单号</span><code class="break-all">{{ run.order.merchant_order_no }}</code></div>
          <div class="grid min-w-0 gap-1 sm:grid-cols-[8rem_minmax(0,1fr)]"><span class="text-muted-foreground">订单状态</span><span>{{ ORDER_STATUS_LABELS[run.order.status] ?? run.order.status }}</span></div>
          <div class="grid min-w-0 gap-1 sm:grid-cols-[8rem_minmax(0,1fr)]"><span class="text-muted-foreground">回调状态</span><span>{{ NOTIFY_STATUS_LABELS[run.order.notify_status] ?? run.order.notify_status }}</span></div>
          <div class="grid min-w-0 gap-1 sm:grid-cols-[8rem_minmax(0,1fr)]"><span class="text-muted-foreground">创建时间</span><span>{{ formatDateTime(run.order.created_at) }}</span></div>
        </div>
        <template #footer>
          <div class="flex min-w-0 flex-wrap gap-2">
            <Button v-if="run.order.status === 'PENDING'" @click="openCashier"><ExternalLinkIcon />打开收银台</Button>
            <CopyButton :value="run.payUrl" label="复制收银台地址" size="default" />
          </div>
          <Button variant="outline" @click="startAnother">创建另一笔</Button>
        </template>
      </SettingSection>

      <SettingSection title="流程诊断" description="查看支付、到账和回调进度。">
        <ol class="grid min-w-0 gap-3">
          <li v-for="(stage, index) in run.stages" :key="stage.key" class="min-w-0 rounded-lg border p-4">
            <details class="group min-w-0" :open="stage.status === 'failed'">
              <summary class="flex min-w-0 cursor-pointer list-none items-start gap-3">
                <CheckCircle2Icon v-if="stage.status === 'complete'" class="mt-0.5 size-5 shrink-0 text-primary" />
                <XCircleIcon v-else-if="stage.status === 'failed'" class="mt-0.5 size-5 shrink-0 text-destructive" />
                <LoaderCircleIcon v-else-if="index === run.stages.findIndex((item) => item.status === 'waiting') && !isTerminal" class="mt-0.5 size-5 shrink-0 animate-spin text-muted-foreground" />
                <CircleIcon v-else class="mt-0.5 size-5 shrink-0 text-muted-foreground" />
                <div class="min-w-0 flex-1"><p class="font-medium">{{ index + 1 }}. {{ stage.title }}</p><p class="mt-1 break-words text-sm text-muted-foreground">{{ stage.summary }}</p></div>
                <StatusBadge :value="stage.status" :label="stage.status === 'complete' ? '完成' : stage.status === 'failed' ? '失败' : '等待'" />
              </summary>
              <pre class="mt-4 max-h-96 min-w-0 overflow-auto whitespace-pre-wrap break-all rounded-md bg-muted p-4 text-xs leading-5">{{ prettyJson(JSON.stringify(stage.data)) }}</pre>
            </details>
          </li>
        </ol>
      </SettingSection>

      <Alert v-if="run.completed">
        <AlertTitle>完整支付流程已完成</AlertTitle>
        <AlertDescription>订单已收款，商户回调已完成。</AlertDescription>
      </Alert>
      <Alert v-else-if="isTerminal" variant="destructive">
        <AlertTitle>流程未完整完成</AlertTitle>
        <AlertDescription>请展开失败或等待阶段查看实际数据，再到订单管理处理回调或删除记录。</AlertDescription>
      </Alert>
    </template>
  </section>
</template>
