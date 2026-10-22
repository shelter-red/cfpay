<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { useRoute, useRouter } from "vue-router";
import { toast } from "vue-sonner";
import { CheckCheckIcon, CircleCheckIcon, DownloadIcon, RefreshCwIcon, RotateCcwIcon, SearchIcon, Trash2Icon } from "@lucide/vue";
import AdminDataTable, { type AdminTableColumn } from "@/components/admin/AdminDataTable.vue";
import AdminPageHeader from "@/components/admin/AdminPageHeader.vue";
import DetailDialog from "@/components/admin/DetailDialog.vue";
import StatusBadge from "@/components/admin/StatusBadge.vue";
import Pagination from "@/components/ui/pagination/Pagination.vue";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import { Badge } from "@/components/ui/badge";
import { Checkbox } from "@/components/ui/checkbox";
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Textarea } from "@/components/ui/textarea";
import { api, downloadFile, errorMessage, notifyError } from "@/lib/api";
import { orderDetailSections, type DetailSection } from "@/lib/detail";
import {
  formatDateTime, formatFen, NOTIFY_STATUS_LABELS, ORDER_STATUS_LABELS, PROVIDER_LABELS,
} from "@/lib/format";
import type { OrderDetail, OrderRow } from "@/lib/types";

type OrderStatusFilter = "" | "PENDING" | "PAID" | "EXPIRED" | "CLOSED";
type NotifyStatusFilter = "" | "NONE" | "ACTIVE" | "PENDING" | "RETRYING" | "SUCCESS" | "FAILED" | "HANDLED";
type ProviderFilter = "" | "wxpay" | "alipay";

const route = useRoute();
const router = useRouter();
const rows = ref<OrderRow[]>([]);
const total = ref(0);
const page = ref(1);
const pageSize = ref(20);
const query = ref(typeof route.query.q === "string" ? route.query.q : "");
const provider = ref<ProviderFilter>(route.query.provider === "wxpay" || route.query.provider === "alipay" ? route.query.provider : "");
const status = ref<OrderStatusFilter>(["PENDING", "PAID", "EXPIRED", "CLOSED"].includes(String(route.query.status)) ? route.query.status as OrderStatusFilter : "");
const notifyStatus = ref<NotifyStatusFilter>(["NONE", "ACTIVE", "PENDING", "RETRYING", "SUCCESS", "FAILED", "HANDLED"].includes(String(route.query.notifyStatus)) ? route.query.notifyStatus as NotifyStatusFilter : "");
const loading = ref(true);
const refreshing = ref(false);
const failure = ref("");
const detailOpen = ref(false);
const detailTitle = ref("");
const detailSections = ref<DetailSection[]>([]);
const markingPaid = ref<string | null>(null);
const retryingCallback = ref<string | null>(null);
const handleOpen = ref(false);
const handlingOrder = ref<OrderRow | null>(null);
const handleReason = ref("");
const handlingCallback = ref(false);
const selectedIds = ref<string[]>([]);
const deleteOpen = ref(false);
const deleteRows = ref<OrderRow[]>([]);
const deleteConfirm = ref("");
const deleting = ref(false);
let loadSequence = 0;

const deleteExpected = computed(() => deleteRows.value.length === 1
  ? deleteRows.value[0]?.merchant_order_no ?? ""
  : `删除 ${deleteRows.value.length} 笔订单`);

const columns: AdminTableColumn<OrderRow>[] = [
  { key: "_selected", label: "选择", class: "w-14", headerClass: "w-14" },
  { key: "merchant_order_no", label: "商户订单号", class: "w-56 max-w-56 break-all font-mono text-xs", headerClass: "w-56 max-w-56" },
  { key: "provider", label: "渠道", class: "w-20" },
  { key: "amount_fen", label: "订单金额", class: "w-24 text-right" },
  { key: "payable_amount_fen", label: "实付金额", class: "w-24 text-right" },
  { key: "status", label: "订单状态", class: "w-28" },
  { key: "notify_status", label: "回调状态", class: "w-28" },
  { key: "created_at", label: "创建时间", class: "w-44 text-xs text-muted-foreground" },
];

/** 将当前筛选条件写入 URL，首页异常卡片可以直接复用这些条件。 */
async function syncQuery(): Promise<void> {
  await router.replace({
    query: {
      ...(query.value.trim() ? { q: query.value.trim() } : {}),
      ...(provider.value ? { provider: provider.value } : {}),
      ...(status.value ? { status: status.value } : {}),
      ...(notifyStatus.value ? { notifyStatus: notifyStatus.value } : {}),
    },
  });
}

/** 分页读取订单列表，静默刷新时保留当前表格。 */
async function load(options: { silent?: boolean; fresh?: boolean } = {}): Promise<void> {
  const sequence = ++loadSequence;
  if (options.silent) refreshing.value = true;
  else loading.value = true;
  try {
    const params = new URLSearchParams({ page: String(page.value), pageSize: String(pageSize.value) });
    if (query.value.trim()) params.set("q", query.value.trim());
    if (provider.value) params.set("provider", provider.value);
    if (status.value) params.set("status", status.value);
    if (notifyStatus.value) params.set("notifyStatus", notifyStatus.value);
    if (options.fresh) params.set("refresh", "1");
    const data = await api<{ items: OrderRow[]; total: number }>(`/admin-api/orders?${params}`);
    if (sequence !== loadSequence) return;
    rows.value = data.items;
    selectedIds.value = selectedIds.value.filter((id) => data.items.some((row) => row.id === id));
    total.value = data.total;
    failure.value = "";
  } catch (error) {
    if (sequence !== loadSequence) return;
    if (!options.silent) failure.value = errorMessage(error);
  } finally {
    if (sequence !== loadSequence) return;
    loading.value = false;
    refreshing.value = false;
  }
}

/** 切换当前页一笔订单的选择状态。 */
function toggleSelected(orderId: string, checked: boolean): void {
  selectedIds.value = checked
    ? [...new Set([...selectedIds.value, orderId])]
    : selectedIds.value.filter((id) => id !== orderId);
}

/** 选择或取消选择当前页全部订单。 */
function toggleCurrentPage(): void {
  selectedIds.value = selectedIds.value.length === rows.value.length ? [] : rows.value.map((row) => row.id);
}

/** 导出选中订单的完整 JSON 或电子表格安全 CSV。 */
async function exportSelected(format: "json" | "csv", ids = selectedIds.value): Promise<void> {
  try {
    await downloadFile("/admin-api/orders/export", { method: "POST", body: JSON.stringify({ ids, format }) });
    toast.success(`订单${format.toUpperCase()}导出已开始下载`);
  } catch (error) {
    notifyError(error);
  }
}

/** 打开单条或批量订单永久删除确认弹窗。 */
function openDelete(targets: OrderRow[]): void {
  deleteRows.value = targets;
  deleteConfirm.value = "";
  deleteOpen.value = true;
}

/** 永久删除确认中的订单及全部关联数据。 */
async function confirmDelete(): Promise<void> {
  if (!deleteRows.value.length || deleteConfirm.value !== deleteExpected.value) return;
  deleting.value = true;
  const ids = deleteRows.value.map((row) => row.id);
  try {
    const result = ids.length === 1
      ? await api<{ deleted: number }>(`/admin-api/orders/${encodeURIComponent(ids[0]!)}`, { method: "DELETE" })
      : await api<{ deleted: number }>("/admin-api/orders/bulk-delete", { method: "POST", body: JSON.stringify({ ids }) });
    deleteOpen.value = false;
    selectedIds.value = [];
    await load();
    toast.success(`已永久删除 ${result.deleted} 笔订单`);
  } catch (error) {
    notifyError(error);
  } finally {
    deleting.value = false;
  }
}

/** 应用搜索与筛选并回到第一页。 */
async function applyFilters(): Promise<void> {
  page.value = 1;
  await syncQuery();
  await load();
}

/** 清空所有筛选条件。 */
async function clearFilters(): Promise<void> {
  query.value = "";
  provider.value = "";
  status.value = "";
  notifyStatus.value = "";
  await applyFilters();
}

/** 手动刷新当前分页。 */
async function onRefresh(): Promise<void> {
  await load({ silent: true, fresh: true });
}

/** 切换页码并只触发一次订单读取。 */
function changePage(value: number): void {
  if (page.value === value) return;
  page.value = value;
  void load();
}

/** 修改每页条数后回到第一页并只触发一次订单读取。 */
function changePageSize(value: number): void {
  if (pageSize.value === value && page.value === 1) return;
  pageSize.value = value;
  page.value = 1;
  void load();
}

/** 打开订单完整详情。 */
async function showDetail(order: OrderRow): Promise<void> {
  try {
    const data = await api<OrderDetail>(`/admin-api/orders/${encodeURIComponent(order.id)}`);
    detailTitle.value = `订单 ${data.order?.merchant_order_no ?? order.id}`;
    detailSections.value = orderDetailSections(data);
    detailOpen.value = true;
  } catch (error) {
    notifyError(error);
  }
}

/** 将普通待支付或已过期订单人工补记为已支付。 */
async function markPaid(order: OrderRow): Promise<void> {
  if (!window.confirm(`确认将订单 ${order.merchant_order_no} 标记为已支付？`)) return;
  markingPaid.value = order.id;
  try {
    await api(`/admin-api/orders/${encodeURIComponent(order.id)}/mark-paid`, { method: "POST" });
    await load({ silent: true });
    toast.success("订单已标记为已支付");
  } catch (error) {
    notifyError(error);
  } finally {
    markingPaid.value = null;
  }
}

/** 为回调失败订单追加一次立即重试。 */
async function retryCallback(order: OrderRow): Promise<void> {
  if (!window.confirm(`确认立即重试订单 ${order.merchant_order_no} 的商户回调？本次只尝试一次。`)) return;
  retryingCallback.value = order.id;
  try {
    await api(`/admin-api/orders/${encodeURIComponent(order.id)}/retry-callback`, { method: "POST" });
    await load({ silent: true });
    toast.success("已执行一次商户回调重试");
  } catch (error) {
    notifyError(error);
  } finally {
    retryingCallback.value = null;
  }
}

/** 打开失败回调处理弹窗并清空上一次输入。 */
function openHandleCallback(order: OrderRow): void {
  handlingOrder.value = order;
  handleReason.value = "";
  handleOpen.value = true;
}

/** 保存失败回调处理说明，处理后该回调不再允许重试。 */
async function handleCallback(): Promise<void> {
  if (!handlingOrder.value || !handleReason.value.trim()) return;
  handlingCallback.value = true;
  try {
    await api(`/admin-api/orders/${encodeURIComponent(handlingOrder.value.id)}/handle-callback`, {
      method: "POST",
      body: JSON.stringify({ reason: handleReason.value }),
    });
    handleOpen.value = false;
    await load({ silent: true });
    toast.success("失败回调已标记为处理完成");
  } catch (error) {
    notifyError(error);
  } finally {
    handlingCallback.value = false;
  }
}

onMounted(() => void load());
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
      <AlertTitle>订单读取失败</AlertTitle>
      <AlertDescription class="flex flex-wrap items-center gap-3"><span>{{ failure }}</span><Button size="sm" variant="outline" @click="load()">重试</Button></AlertDescription>
    </Alert>

    <form class="grid min-w-0 gap-3 rounded-lg border bg-card p-4 sm:grid-cols-2 xl:grid-cols-[minmax(14rem,1fr)_10rem_10rem_11rem_auto]" @submit.prevent="applyFilters">
      <Input v-model="query" placeholder="搜索商户订单号或系统订单号" />
      <Select :model-value="provider || 'all'" @update:model-value="provider = $event === 'all' ? '' : $event as ProviderFilter">
        <SelectTrigger class="w-full"><SelectValue placeholder="全部渠道" /></SelectTrigger>
        <SelectContent><SelectItem value="all">全部渠道</SelectItem><SelectItem value="wxpay">微信</SelectItem><SelectItem value="alipay">支付宝</SelectItem></SelectContent>
      </Select>
      <Select :model-value="status || 'all'" @update:model-value="status = $event === 'all' ? '' : $event as OrderStatusFilter">
        <SelectTrigger class="w-full"><SelectValue placeholder="全部订单状态" /></SelectTrigger>
        <SelectContent><SelectItem value="all">全部订单状态</SelectItem><SelectItem value="PENDING">待支付</SelectItem><SelectItem value="PAID">已支付</SelectItem><SelectItem value="EXPIRED">已过期</SelectItem><SelectItem value="CLOSED">已关闭</SelectItem></SelectContent>
      </Select>
      <Select :model-value="notifyStatus || 'all'" @update:model-value="notifyStatus = $event === 'all' ? '' : $event as NotifyStatusFilter">
        <SelectTrigger class="w-full"><SelectValue placeholder="全部回调状态" /></SelectTrigger>
        <SelectContent><SelectItem value="all">全部回调状态</SelectItem><SelectItem value="ACTIVE">待完成回调</SelectItem><SelectItem value="FAILED">回调失败</SelectItem><SelectItem value="HANDLED">回调已处理</SelectItem><SelectItem value="SUCCESS">回调成功</SelectItem><SelectItem value="NONE">无需回调</SelectItem></SelectContent>
      </Select>
      <div class="flex flex-wrap gap-2"><Button type="submit"><SearchIcon />查询</Button><Button type="button" variant="outline" @click="clearFilters">清空</Button></div>
    </form>

    <AdminDataTable :columns="columns" :rows="rows" row-key="id" :empty-text="loading ? '读取中…' : '暂无符合条件的订单'">
      <template #toolbar>
        <div class="flex min-w-0 flex-wrap items-center gap-2">
          <Button size="sm" variant="outline" :disabled="!rows.length" @click="toggleCurrentPage">{{ selectedIds.length === rows.length && rows.length ? "取消全选" : "全选本页" }}</Button>
          <span class="text-sm text-muted-foreground">已选 {{ selectedIds.length }} 笔</span>
        </div>
        <div class="flex min-w-0 flex-wrap gap-2">
          <Button size="sm" variant="outline" :disabled="!selectedIds.length" @click="exportSelected('json')"><DownloadIcon />导出 JSON</Button>
          <Button size="sm" variant="outline" :disabled="!selectedIds.length" @click="exportSelected('csv')"><DownloadIcon />导出 CSV</Button>
          <Button size="sm" variant="destructive" :disabled="!selectedIds.length" @click="openDelete(rows.filter((row) => selectedIds.includes(row.id)))"><Trash2Icon />删除已选</Button>
        </div>
      </template>
      <template #cell-_selected="{ row }"><Checkbox :model-value="selectedIds.includes(row.id)" :aria-label="`选择订单 ${row.merchant_order_no}`" @update:model-value="toggleSelected(row.id, Boolean($event))" /></template>
      <template #cell-merchant_order_no="{ row }"><span class="break-all font-mono text-xs">{{ row.merchant_order_no }}</span></template>
      <template #cell-provider="{ value }">{{ PROVIDER_LABELS[String(value)] ?? value }}</template>
      <template #cell-amount_fen="{ row }">{{ formatFen(row.amount_fen) }}</template>
      <template #cell-payable_amount_fen="{ row }"><span class="font-medium">{{ formatFen(row.payable_amount_fen) }}</span></template>
      <template #cell-status="{ row, value }"><div class="flex flex-wrap gap-1"><StatusBadge :value="String(value)" :label="ORDER_STATUS_LABELS[String(value)] ?? String(value)" /><Badge v-if="row.paid_source === 'MANUAL'" variant="secondary">人工补单</Badge></div></template>
      <template #cell-notify_status="{ row }"><StatusBadge :value="row.callback_handled_at ? 'HANDLED' : row.notify_status" :label="row.callback_handled_at ? '回调已处理' : NOTIFY_STATUS_LABELS[row.notify_status] ?? row.notify_status" /></template>
      <template #cell-created_at="{ value }">{{ formatDateTime(Number(value)) }}</template>
      <template #actions="{ row }">
        <div class="flex min-w-0 flex-wrap justify-end gap-2">
          <Button size="sm" variant="outline" @click="showDetail(row)">详情</Button>
          <Button v-if="row.notify_status === 'FAILED' && !row.callback_handled_at" size="sm" variant="outline" :disabled="retryingCallback === row.id" @click="retryCallback(row)"><RotateCcwIcon />{{ retryingCallback === row.id ? "重试中…" : "重试回调" }}</Button>
          <Button v-if="row.notify_status === 'FAILED' && !row.callback_handled_at" size="sm" variant="outline" @click="openHandleCallback(row)"><CheckCheckIcon />标记已处理</Button>
          <Button v-if="row.status === 'PENDING' || row.status === 'EXPIRED'" size="sm" :disabled="markingPaid === row.id" @click="markPaid(row)"><CircleCheckIcon />{{ markingPaid === row.id ? "处理中…" : "补单" }}</Button>
          <Button size="sm" variant="destructive" @click="openDelete([row])"><Trash2Icon />删除</Button>
        </div>
      </template>
      <template #pagination><Pagination :page="page" :page-size="pageSize" :total="total" @update:page="changePage" @update:page-size="changePageSize" /></template>
    </AdminDataTable>

    <DetailDialog v-model:open="detailOpen" :title="detailTitle" :sections="detailSections" />

    <Dialog v-model:open="handleOpen">
      <DialogContent>
        <DialogHeader>
          <DialogTitle>处理失败回调</DialogTitle>
          <DialogDescription>处理后保留失败结果和全部尝试记录，但不再允许重试。</DialogDescription>
        </DialogHeader>
        <div class="grid min-w-0 gap-2">
          <Label for="callback-handle-reason">处理说明</Label>
          <Textarea id="callback-handle-reason" v-model="handleReason" maxlength="200" placeholder="请输入处理原因或线下处理结果" />
          <p class="text-right text-xs text-muted-foreground">{{ handleReason.trim().length }} / 200</p>
        </div>
        <DialogFooter>
          <Button variant="outline" :disabled="handlingCallback" @click="handleOpen = false">取消</Button>
          <Button :disabled="handlingCallback || !handleReason.trim()" @click="handleCallback"><CheckCheckIcon />{{ handlingCallback ? "处理中…" : "确认已处理" }}</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>

    <Dialog v-model:open="deleteOpen">
      <DialogContent class="sm:max-w-xl">
        <DialogHeader>
          <DialogTitle>永久删除订单</DialogTitle>
          <DialogDescription>将删除订单、关联收款、处理记录、回调任务和全部回调尝试，此操作不可恢复。</DialogDescription>
        </DialogHeader>
        <Alert v-if="deleteRows.some((row) => row.status === 'PENDING')" variant="destructive"><AlertTitle>包含待支付订单</AlertTitle><AlertDescription>删除后若付款人继续支付，到账会进入未匹配收款。</AlertDescription></Alert>
        <Alert v-if="deleteRows.some((row) => row.status === 'PAID')" variant="destructive"><AlertTitle>包含已支付订单</AlertTitle><AlertDescription>真实到账和回调审计数据也会被永久删除。</AlertDescription></Alert>
        <div class="flex min-w-0 flex-wrap gap-2">
          <Button variant="outline" @click="exportSelected('json', deleteRows.map((row) => row.id))"><DownloadIcon />删除前导出 JSON</Button>
          <Button variant="outline" @click="exportSelected('csv', deleteRows.map((row) => row.id))"><DownloadIcon />删除前导出 CSV</Button>
        </div>
        <div class="grid gap-2">
          <Label for="delete-order-confirm">请输入“{{ deleteExpected }}”确认</Label>
          <Input id="delete-order-confirm" v-model="deleteConfirm" autocomplete="off" />
        </div>
        <DialogFooter>
          <Button variant="outline" :disabled="deleting" @click="deleteOpen = false">取消</Button>
          <Button variant="destructive" :disabled="deleting || deleteConfirm !== deleteExpected" @click="confirmDelete"><Trash2Icon />{{ deleting ? "删除中…" : "永久删除" }}</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  </section>
</template>
