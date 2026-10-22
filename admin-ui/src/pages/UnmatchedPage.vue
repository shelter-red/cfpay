<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { useRoute, useRouter } from "vue-router";
import { toast } from "vue-sonner";
import { CircleCheckIcon, DownloadIcon, RefreshCwIcon, RotateCcwIcon, SearchIcon, Trash2Icon, XCircleIcon } from "@lucide/vue";
import AdminDataTable, { type AdminTableColumn } from "@/components/admin/AdminDataTable.vue";
import AdminPageHeader from "@/components/admin/AdminPageHeader.vue";
import DetailDialog from "@/components/admin/DetailDialog.vue";
import Pagination from "@/components/ui/pagination/Pagination.vue";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Checkbox } from "@/components/ui/checkbox";
import {
  Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Textarea } from "@/components/ui/textarea";
import { api, downloadFile, errorMessage, notifyError } from "@/lib/api";
import { eventDetailSections, type DetailSection } from "@/lib/detail";
import { formatDateTime, formatFen, ORDER_STATUS_LABELS, PROVIDER_LABELS } from "@/lib/format";
import type { PaymentEventCandidate, PaymentEventDetail, PaymentEventListItem } from "@/lib/types";

type EventScope = "pending" | "resolved";
type PagedResponse<T> = { items: T[]; total: number; page: number; pageSize: number; now?: number };

const route = useRoute();
const router = useRouter();
const scope = ref<EventScope>(route.query.scope === "resolved" ? "resolved" : "pending");
const overdueOnly = ref(route.query.overdue === "1");
const rows = ref<PaymentEventListItem[]>([]);
const total = ref(0);
const page = ref(1);
const pageSize = ref(20);
const serverNow = ref(Date.now());
const loading = ref(true);
const refreshing = ref(false);
const failure = ref("");

const detailOpen = ref(false);
const detailTitle = ref("");
const detailSections = ref<DetailSection[]>([]);

const matchOpen = ref(false);
const matchEvent = ref<PaymentEventListItem | null>(null);
const candidates = ref<PaymentEventCandidate[]>([]);
const candidateTotal = ref(0);
const candidatePage = ref(1);
const candidateQuery = ref("");
const candidatesLoading = ref(false);
const matchingOrderId = ref<string | null>(null);

const ignoreOpen = ref(false);
const ignoreEvent = ref<PaymentEventListItem | null>(null);
const ignoreReason = ref("");
const ignoring = ref(false);
const restoringId = ref<string | null>(null);
const selectedIds = ref<string[]>([]);
const deleteOpen = ref(false);
const deleteRows = ref<PaymentEventListItem[]>([]);
const deleteConfirm = ref("");
const deleting = ref(false);
let loadSequence = 0;
let candidateLoadSequence = 0;

const deleteExpected = computed(() => deleteRows.value.length === 1
  ? deleteRows.value[0]?.id ?? ""
  : `删除 ${deleteRows.value.length} 条收款记录`);

const columns = computed<AdminTableColumn<PaymentEventListItem>[]>(() => scope.value === "pending"
  ? [
      { key: "provider", label: "渠道", class: "w-20" },
      { key: "amount_fen", label: "金额", class: "w-24 text-right" },
      { key: "paid_at", label: "支付时间", class: "w-40 text-xs" },
      { key: "created_at", label: "等待时间", class: "w-32 text-xs" },
    ]
  : [
      { key: "_selected", label: "选择", class: "w-14", headerClass: "w-14" },
      { key: "provider", label: "渠道", class: "w-20" },
      { key: "amount_fen", label: "金额", class: "w-24 text-right" },
      { key: "status", label: "处理结果", class: "w-28" },
      { key: "matched_merchant_order_no", label: "关联订单", class: "max-w-56 break-all font-mono text-xs" },
      { key: "latest_action_at", label: "处理时间", class: "w-40 text-xs" },
    ]);

/** 返回待处理事件已经等待的中文时长。 */
function waitingText(createdAt: number): string {
  const minutes = Math.max(0, Math.floor((serverNow.value - createdAt) / 60_000));
  if (minutes < 60) return `${minutes} 分钟`;
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return `${hours} 小时`;
  return `${Math.floor(hours / 24)} 天`;
}

/** 判断事件是否已经超过 24 小时仍未处理。 */
function isOverdue(row: PaymentEventListItem): boolean {
  return serverNow.value - row.created_at >= 24 * 60 * 60_000;
}

/** 把当前页签和超时筛选写进 URL，便于首页卡片直接跳转。 */
async function syncQuery(): Promise<void> {
  await router.replace({
    query: {
      ...(scope.value === "resolved" ? { scope: "resolved" } : {}),
      ...(scope.value === "pending" && overdueOnly.value ? { overdue: "1" } : {}),
    },
  });
}

/** 分页读取当前页签的收款事件。 */
async function load(silent = false, fresh = false): Promise<void> {
  const sequence = ++loadSequence;
  if (silent) refreshing.value = true;
  else loading.value = true;
  try {
    const params = new URLSearchParams({ scope: scope.value, page: String(page.value), pageSize: String(pageSize.value) });
    if (scope.value === "pending" && overdueOnly.value) params.set("overdue", "1");
    if (fresh) params.set("refresh", "1");
    const data = await api<PagedResponse<PaymentEventListItem>>(`/admin-api/payment-events?${params}`);
    if (sequence !== loadSequence) return;
    rows.value = data.items;
    selectedIds.value = selectedIds.value.filter((id) => data.items.some((row) => row.id === id && row.status === "IGNORED"));
    total.value = data.total;
    serverNow.value = data.now ?? Date.now();
    failure.value = "";
  } catch (error) {
    if (sequence !== loadSequence) return;
    failure.value = errorMessage(error);
  } finally {
    if (sequence !== loadSequence) return;
    loading.value = false;
    refreshing.value = false;
  }
}

/** 切换待处理或处理记录，并从第一页重新读取。 */
async function changeScope(value: string | number): Promise<void> {
  scope.value = value === "resolved" ? "resolved" : "pending";
  overdueOnly.value = scope.value === "pending" && route.query.overdue === "1";
  page.value = 1;
  selectedIds.value = [];
  await syncQuery();
  await load();
}

/** 切换一条已忽略收款的选择状态。 */
function toggleSelected(eventId: string, checked: boolean): void {
  selectedIds.value = checked
    ? [...new Set([...selectedIds.value, eventId])]
    : selectedIds.value.filter((id) => id !== eventId);
}

/** 选择或取消选择当前页全部已忽略收款。 */
function toggleIgnoredPage(): void {
  const ignoredIds = rows.value.filter((row) => row.status === "IGNORED").map((row) => row.id);
  selectedIds.value = selectedIds.value.length === ignoredIds.length ? [] : ignoredIds;
}

/** 导出选中的已忽略收款和完整处理记录。 */
async function exportSelected(format: "json" | "csv", ids = selectedIds.value): Promise<void> {
  try {
    await downloadFile("/admin-api/payment-events/export", { method: "POST", body: JSON.stringify({ ids, format }) });
    toast.success(`收款记录${format.toUpperCase()}导出已开始下载`);
  } catch (error) {
    notifyError(error);
  }
}

/** 打开已忽略收款的永久删除确认弹窗。 */
function openDelete(targets: PaymentEventListItem[]): void {
  deleteRows.value = targets;
  deleteConfirm.value = "";
  deleteOpen.value = true;
}

/** 永久删除确认中的已忽略收款及其处理记录。 */
async function confirmDelete(): Promise<void> {
  if (!deleteRows.value.length || deleteConfirm.value !== deleteExpected.value) return;
  deleting.value = true;
  const ids = deleteRows.value.map((row) => row.id);
  try {
    const result = ids.length === 1
      ? await api<{ deleted: number }>(`/admin-api/payment-events/${encodeURIComponent(ids[0]!)}`, { method: "DELETE" })
      : await api<{ deleted: number }>("/admin-api/payment-events/bulk-delete", { method: "POST", body: JSON.stringify({ ids }) });
    deleteOpen.value = false;
    selectedIds.value = [];
    await load();
    toast.success(`已永久删除 ${result.deleted} 条收款记录`);
  } catch (error) {
    notifyError(error);
  } finally {
    deleting.value = false;
  }
}

/** 切换超时筛选，并从第一页重新读取。 */
async function toggleOverdue(): Promise<void> {
  overdueOnly.value = !overdueOnly.value;
  page.value = 1;
  await syncQuery();
  await load();
}

/** 打开收款事件详情并展示解析数据与操作历史。 */
async function showDetail(eventId: string): Promise<void> {
  try {
    const data = await api<PaymentEventDetail>(`/admin-api/payment-events/${encodeURIComponent(eventId)}`);
    detailTitle.value = `收款事件 ${eventId}`;
    detailSections.value = eventDetailSections(data);
    detailOpen.value = true;
  } catch (error) {
    notifyError(error);
  }
}

/** 读取人工匹配允许选择的候选订单。 */
async function loadCandidates(): Promise<void> {
  if (!matchEvent.value) return;
  const sequence = ++candidateLoadSequence;
  candidatesLoading.value = true;
  try {
    const params = new URLSearchParams({ page: String(candidatePage.value), pageSize: "20" });
    if (candidateQuery.value.trim()) params.set("q", candidateQuery.value.trim());
    const data = await api<PagedResponse<PaymentEventCandidate>>(
      `/admin-api/payment-events/${encodeURIComponent(matchEvent.value.id)}/candidates?${params}`,
    );
    if (sequence !== candidateLoadSequence) return;
    candidates.value = data.items;
    candidateTotal.value = data.total;
  } catch (error) {
    if (sequence !== candidateLoadSequence) return;
    notifyError(error);
  } finally {
    if (sequence !== candidateLoadSequence) return;
    candidatesLoading.value = false;
  }
}

/** 打开匹配弹窗并加载第一页候选订单。 */
async function openMatch(row: PaymentEventListItem): Promise<void> {
  matchEvent.value = row;
  candidatePage.value = 1;
  candidateQuery.value = "";
  candidates.value = [];
  matchOpen.value = true;
  await loadCandidates();
}

/** 将收款事件关联到管理员确认的候选订单。 */
async function confirmMatch(order: PaymentEventCandidate): Promise<void> {
  if (!matchEvent.value) return;
  if (!window.confirm(`确认将这笔 ${formatFen(matchEvent.value.amount_fen)} 收款关联到订单 ${order.merchant_order_no}？`)) return;
  matchingOrderId.value = order.id;
  try {
    await api(`/admin-api/payment-events/${encodeURIComponent(matchEvent.value.id)}/match`, {
      method: "POST",
      body: JSON.stringify({ orderId: order.id }),
    });
    matchOpen.value = false;
    toast.success("收款事件已匹配，商户回调将按订单配置执行");
    await load(true);
  } catch (error) {
    notifyError(error);
  } finally {
    matchingOrderId.value = null;
  }
}

/** 打开忽略原因输入框。 */
function openIgnore(row: PaymentEventListItem): void {
  ignoreEvent.value = row;
  ignoreReason.value = "";
  ignoreOpen.value = true;
}

/** 提交忽略原因并把事件移入处理记录。 */
async function confirmIgnore(): Promise<void> {
  if (!ignoreEvent.value) return;
  const reason = ignoreReason.value.trim();
  if (!reason) {
    toast.error("请填写忽略原因");
    return;
  }
  ignoring.value = true;
  try {
    await api(`/admin-api/payment-events/${encodeURIComponent(ignoreEvent.value.id)}/ignore`, {
      method: "POST",
      body: JSON.stringify({ reason }),
    });
    ignoreOpen.value = false;
    toast.success("收款事件已标记为忽略");
    await load(true);
  } catch (error) {
    notifyError(error);
  } finally {
    ignoring.value = false;
  }
}

/** 将误忽略的事件恢复到待处理页签。 */
async function restore(row: PaymentEventListItem): Promise<void> {
  if (!window.confirm("确认恢复为待处理？原忽略原因和操作记录会继续保留。")) return;
  restoringId.value = row.id;
  try {
    await api(`/admin-api/payment-events/${encodeURIComponent(row.id)}/restore`, { method: "POST" });
    toast.success("收款事件已恢复为待处理");
    await load(true);
  } catch (error) {
    notifyError(error);
  } finally {
    restoringId.value = null;
  }
}

/** 手动刷新当前页签。 */
async function onRefresh(): Promise<void> {
  await load(true, true);
}

/** 切换收款记录页码并只触发一次读取。 */
function changePage(value: number): void {
  if (page.value === value) return;
  page.value = value;
  void load();
}

/** 修改收款记录每页条数后回到第一页并只触发一次读取。 */
function changePageSize(value: number): void {
  if (pageSize.value === value && page.value === 1) return;
  pageSize.value = value;
  page.value = 1;
  void load();
}

/** 提交候选订单搜索并从第一页读取一次。 */
function searchCandidates(): void {
  candidatePage.value = 1;
  void loadCandidates();
}

/** 切换候选订单页码并只触发一次读取。 */
function changeCandidatePage(value: number): void {
  if (candidatePage.value === value) return;
  candidatePage.value = value;
  void loadCandidates();
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
      <AlertTitle>收款事件读取失败</AlertTitle>
      <AlertDescription class="flex min-w-0 flex-wrap items-center gap-3">
        <span class="break-all">{{ failure }}</span><Button size="sm" variant="outline" @click="load()">重试</Button>
      </AlertDescription>
    </Alert>

    <Tabs :model-value="scope" @update:model-value="changeScope">
      <div class="flex min-w-0 flex-wrap items-center justify-between gap-3">
        <TabsList><TabsTrigger value="pending">待处理</TabsTrigger><TabsTrigger value="resolved">处理记录</TabsTrigger></TabsList>
        <Button v-if="scope === 'pending'" size="sm" :variant="overdueOnly ? 'destructive' : 'outline'" @click="toggleOverdue">
          {{ overdueOnly ? "显示全部待处理" : "只看超过 24 小时" }}
        </Button>
      </div>
    </Tabs>

    <AdminDataTable
      :columns="columns"
      :rows="rows"
      row-key="id"
      :empty-text="loading ? '读取中…' : scope === 'pending' ? '暂无待处理收款' : '暂无处理记录'"
    >
      <template v-if="scope === 'resolved'" #toolbar>
        <div class="flex min-w-0 flex-wrap items-center gap-2">
          <Button size="sm" variant="outline" :disabled="!rows.some((row) => row.status === 'IGNORED')" @click="toggleIgnoredPage">{{ selectedIds.length && selectedIds.length === rows.filter((row) => row.status === 'IGNORED').length ? "取消全选" : "全选已忽略" }}</Button>
          <span class="text-sm text-muted-foreground">已选 {{ selectedIds.length }} 条</span>
        </div>
        <div class="flex min-w-0 flex-wrap gap-2">
          <Button size="sm" variant="outline" :disabled="!selectedIds.length" @click="exportSelected('json')"><DownloadIcon />导出 JSON</Button>
          <Button size="sm" variant="outline" :disabled="!selectedIds.length" @click="exportSelected('csv')"><DownloadIcon />导出 CSV</Button>
          <Button size="sm" variant="destructive" :disabled="!selectedIds.length" @click="openDelete(rows.filter((row) => selectedIds.includes(row.id)))"><Trash2Icon />删除已选</Button>
        </div>
      </template>
      <template #cell-_selected="{ row }"><Checkbox v-if="row.status === 'IGNORED'" :model-value="selectedIds.includes(row.id)" :aria-label="`选择收款记录 ${row.id}`" @update:model-value="toggleSelected(row.id, Boolean($event))" /></template>
      <template #cell-provider="{ value }">{{ PROVIDER_LABELS[String(value)] ?? value }}</template>
      <template #cell-amount_fen="{ row }">{{ formatFen(row.amount_fen) }}</template>
      <template #cell-paid_at="{ value }">{{ formatDateTime(Number(value)) }}</template>
      <template #cell-created_at="{ row }"><Badge :variant="isOverdue(row) ? 'destructive' : 'outline'">{{ waitingText(row.created_at) }}</Badge></template>
      <template #cell-status="{ row }">
        <Badge :variant="row.status === 'IGNORED' ? 'secondary' : 'default'">{{ row.status === "IGNORED" ? "已忽略" : row.latest_action === "MANUAL_MATCH" ? "人工匹配" : "自动匹配" }}</Badge>
      </template>
      <template #cell-matched_merchant_order_no="{ row }">
        <span v-if="row.matched_merchant_order_no">{{ row.matched_merchant_order_no }}</span>
        <span v-else class="break-words text-muted-foreground">{{ row.latest_reason || "-" }}</span>
      </template>
      <template #cell-latest_action_at="{ row }">{{ formatDateTime(row.latest_action_at) }}</template>
      <template #actions="{ row }">
        <div class="flex min-w-0 flex-wrap justify-end gap-2">
          <Button size="sm" variant="outline" @click="showDetail(row.id)">详情</Button>
          <template v-if="scope === 'pending'">
            <Button size="sm" @click="openMatch(row)"><CircleCheckIcon />匹配订单</Button>
            <Button size="sm" variant="outline" @click="openIgnore(row)"><XCircleIcon />忽略</Button>
          </template>
          <template v-else-if="row.status === 'IGNORED'">
            <Button size="sm" variant="outline" :disabled="restoringId === row.id" @click="restore(row)"><RotateCcwIcon />{{ restoringId === row.id ? "恢复中…" : "恢复待处理" }}</Button>
            <Button size="sm" variant="destructive" @click="openDelete([row])"><Trash2Icon />删除</Button>
          </template>
        </div>
      </template>
      <template #pagination>
        <Pagination :page="page" :page-size="pageSize" :total="total" @update:page="changePage" @update:page-size="changePageSize" />
      </template>
    </AdminDataTable>

    <DetailDialog v-model:open="detailOpen" :title="detailTitle" :sections="detailSections" />

    <Dialog v-model:open="matchOpen">
      <DialogContent class="max-h-[85vh] w-[calc(100vw-2rem)] max-w-[calc(100vw-2rem)] overflow-hidden sm:max-w-3xl">
        <DialogHeader>
          <DialogTitle>匹配订单</DialogTitle>
          <DialogDescription>只显示同渠道、同实付金额且尚未关联收款事件的普通订单。</DialogDescription>
        </DialogHeader>
        <form class="flex min-w-0 gap-2" @submit.prevent="searchCandidates">
          <Input v-model="candidateQuery" placeholder="搜索商户订单号或系统订单号" />
          <Button type="submit" variant="outline"><SearchIcon />搜索</Button>
        </form>
        <div class="max-h-[52vh] min-w-0 overflow-auto rounded-lg border">
          <div v-if="candidatesLoading" class="p-8 text-center text-sm text-muted-foreground">读取候选订单…</div>
          <div v-else-if="!candidates.length" class="p-8 text-center text-sm text-muted-foreground">没有符合条件的订单</div>
          <div v-else class="divide-y">
            <div v-for="candidate in candidates" :key="candidate.id" class="flex min-w-0 flex-wrap items-center justify-between gap-3 p-4">
              <div class="min-w-0 space-y-1">
                <p class="break-all font-mono text-sm">{{ candidate.merchant_order_no }}</p>
                <p class="text-xs text-muted-foreground">{{ ORDER_STATUS_LABELS[candidate.status] ?? candidate.status }} · {{ formatFen(candidate.payable_amount_fen) }} · {{ formatDateTime(candidate.created_at) }}</p>
              </div>
              <Button size="sm" :disabled="matchingOrderId === candidate.id" @click="confirmMatch(candidate)">{{ matchingOrderId === candidate.id ? "匹配中…" : "选择并匹配" }}</Button>
            </div>
          </div>
        </div>
        <Pagination :page="candidatePage" :page-size="20" :total="candidateTotal" :page-size-options="[20]" @update:page="changeCandidatePage" />
      </DialogContent>
    </Dialog>

    <Dialog v-model:open="ignoreOpen">
      <DialogContent class="sm:max-w-lg">
        <DialogHeader><DialogTitle>忽略收款事件</DialogTitle><DialogDescription>忽略后记录仍会保留，并可从处理记录恢复为待处理。</DialogDescription></DialogHeader>
        <div class="grid gap-2">
          <Label for="ignore-reason">忽略原因</Label>
          <Textarea id="ignore-reason" v-model="ignoreReason" maxlength="200" placeholder="例如：已线下核对，不属于本系统订单" />
          <p class="text-right text-xs text-muted-foreground">{{ ignoreReason.trim().length }} / 200</p>
        </div>
        <DialogFooter>
          <Button variant="outline" @click="ignoreOpen = false">取消</Button>
          <Button :disabled="ignoring || !ignoreReason.trim()" @click="confirmIgnore">{{ ignoring ? "处理中…" : "确认忽略" }}</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>

    <Dialog v-model:open="deleteOpen">
      <DialogContent class="sm:max-w-xl">
        <DialogHeader><DialogTitle>永久删除已忽略收款</DialogTitle><DialogDescription>将删除收款原始数据和全部处理记录，此操作不可恢复。</DialogDescription></DialogHeader>
        <div class="flex min-w-0 flex-wrap gap-2">
          <Button variant="outline" @click="exportSelected('json', deleteRows.map((row) => row.id))"><DownloadIcon />删除前导出 JSON</Button>
          <Button variant="outline" @click="exportSelected('csv', deleteRows.map((row) => row.id))"><DownloadIcon />删除前导出 CSV</Button>
        </div>
        <div class="grid gap-2">
          <Label for="delete-payment-event-confirm">请输入“{{ deleteExpected }}”确认</Label>
          <Input id="delete-payment-event-confirm" v-model="deleteConfirm" autocomplete="off" />
        </div>
        <DialogFooter>
          <Button variant="outline" :disabled="deleting" @click="deleteOpen = false">取消</Button>
          <Button variant="destructive" :disabled="deleting || deleteConfirm !== deleteExpected" @click="confirmDelete"><Trash2Icon />{{ deleting ? "删除中…" : "永久删除" }}</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  </section>
</template>
