<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { useRouter } from "vue-router";
import { VisArea, VisAxis, VisXYContainer } from "@unovis/vue";
import { RefreshCwIcon } from "@lucide/vue";
import { toast } from "vue-sonner";
import AdminPageHeader from "@/components/admin/AdminPageHeader.vue";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import { ButtonGroup } from "@/components/ui/button-group";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import {
  ChartContainer,
  ChartCrosshair,
  ChartTooltip,
  ChartTooltipContent,
  componentToString,
  type ChartConfig,
} from "@/components/ui/chart";
import { api, errorMessage } from "@/lib/api";
import { formatBytes, formatFen } from "@/lib/format";
import type { D1UsageData, D1UsageMetric, RuntimeCacheStats } from "@/lib/types";

type TrendPoint = { date: string; timestamp: number; createdOrders: number; paidOrders: number };
type UsageTrendPoint = {
  date: string;
  timestamp: number;
  readUsagePercent: number;
  writeUsagePercent: number;
};
type DashboardMetric = {
  label: string;
  value: string;
  description: string;
  path: { path: string; query?: Record<string, string> } | null;
};
type DashboardData = {
  metrics: {
    todayOrders: number;
    todayPaidFen: number;
    totalPaidFen: number;
    pendingNotify: number;
    failedNotify: number;
    unmatched: number;
    staleUnmatched: number;
  };
  orderTrend: TrendPoint[];
};

const data = ref<DashboardData | null>(null);
const router = useRouter();
const d1Usage = ref<D1UsageData | null>(null);
const runtimeCache = ref<RuntimeCacheStats | null>(null);
const failure = ref("");
const usageFailure = ref("");
const refreshing = ref(false);
const selectedOrderRange = ref<7 | 30 | 90>(7);
const selectedUsageRange = ref<7 | 30>(7);
const rangeOptions = [
  { days: 90 as const, label: "最近 90 天" },
  { days: 30 as const, label: "最近 30 天" },
  { days: 7 as const, label: "最近 7 天" },
];
const usageRangeOptions = rangeOptions.map((option) => ({ ...option, disabled: option.days === 90 }));
const chartConfig = {
  createdOrders: { label: "新建订单", color: "#2563eb" },
  paidOrders: { label: "已支付订单", color: "#f97316" },
} satisfies ChartConfig;
const xAccessor = (point: TrendPoint) => point.timestamp;
const createdOrdersAccessor = (point: TrendPoint) => point.createdOrders;
const paidOrdersAccessor = (point: TrendPoint) => point.paidOrders;
const crosshairColors = (_point: TrendPoint, index: number) => index === 0
  ? chartConfig.createdOrders.color
  : chartConfig.paidOrders.color;
const tooltipTemplate = componentToString(chartConfig, ChartTooltipContent, {
  hiddenKeys: ["date", "timestamp"],
  labelFormatter: (value: number | Date) => new Date(Number(value)).toLocaleDateString("zh-CN", {
    year: "numeric",
    month: "long",
    day: "numeric",
  }),
});
const usageChartConfig = {
  readUsagePercent: { label: "读取额度使用率（%）", color: "#2563eb" },
  writeUsagePercent: { label: "写入额度使用率（%）", color: "#f97316" },
} satisfies ChartConfig;
const usageXAccessor = (point: UsageTrendPoint) => point.timestamp;
const readUsageAccessor = (point: UsageTrendPoint) => point.readUsagePercent;
const writeUsageAccessor = (point: UsageTrendPoint) => point.writeUsagePercent;
const usageCrosshairColors = (_point: UsageTrendPoint, index: number) => index === 0
  ? usageChartConfig.readUsagePercent.color
  : usageChartConfig.writeUsagePercent.color;
const usageTooltipTemplate = componentToString(usageChartConfig, ChartTooltipContent, {
  hiddenKeys: ["date", "timestamp"],
  labelFormatter: (value: number | Date) => new Date(Number(value)).toLocaleDateString("zh-CN", {
    year: "numeric",
    month: "long",
    day: "numeric",
    timeZone: "UTC",
  }),
});

/** 读取面板聚合数据。 */
async function load(silent = false): Promise<void> {
  if (silent) refreshing.value = true;
  const dashboardPath = silent ? "/admin-api/dashboard?refresh=1" : "/admin-api/dashboard";
  const usagePath = silent ? "/admin-api/cloudflare/d1-usage?refresh=1" : "/admin-api/cloudflare/d1-usage";
  const [dashboardResult, usageResult, cacheResult] = await Promise.allSettled([
    api<DashboardData>(dashboardPath),
    api<D1UsageData>(usagePath),
    api<RuntimeCacheStats>("/admin-api/runtime-cache"),
  ]);
  if (dashboardResult.status === "fulfilled") {
    data.value = dashboardResult.value;
    failure.value = "";
  } else {
    failure.value = errorMessage(dashboardResult.reason);
  }
  if (usageResult.status === "fulfilled") {
    d1Usage.value = usageResult.value;
    usageFailure.value = "";
  } else {
    usageFailure.value = errorMessage(usageResult.reason);
  }
  if (cacheResult.status === "fulfilled") runtimeCache.value = cacheResult.value;
  refreshing.value = false;
}

/** 清空当前运行缓存并重新读取面板数据。 */
async function clearCache(): Promise<void> {
  runtimeCache.value = await api<RuntimeCacheStats>("/admin-api/runtime-cache", { method: "DELETE" });
  toast.success("运行缓存已清空");
  await load(true);
}

/** 手动刷新面板；页面数据变化本身就是成功反馈。 */
async function refresh(): Promise<void> {
  await load(true);
}

/** 格式化趋势横轴日期。 */
function formatAxisDate(value: number | Date): string {
  return new Date(Number(value)).toLocaleDateString("zh-CN", { month: "numeric", day: "numeric" });
}

/** 只在纵轴显示整数订单数量。 */
function formatOrderCount(value: number | Date): string {
  const count = Number(value);
  return Number.isInteger(count) ? String(count) : "";
}

/** 把较大的用量数字压缩为适合卡片展示的中文计数。 */
function formatCount(value: number): string {
  return new Intl.NumberFormat("zh-CN", { notation: "compact", maximumFractionDigits: 2 }).format(value);
}

/** 格式化免费额度占比；付费计划无日硬上限时展示按量计费。 */
function metricDescription(metric: D1UsageMetric, formatter = formatCount): string {
  if (metric.limit === null) return "付费计划按量计费";
  return `${formatter(metric.used)} / ${formatter(metric.limit)}（${Math.min(100, Math.round((metric.ratio ?? 0) * 100))}%）`;
}

/** 根据 70%、90%、100% 三档阈值返回进度条颜色。 */
function usageBarClass(metric: D1UsageMetric): string {
  if (metric.level === "exhausted" || metric.level === "danger") return "bg-destructive";
  if (metric.level === "warning") return "bg-amber-500";
  return "bg-primary";
}

/** 免费额度进度条最多显示到 100%，付费计划固定满格用于展示当前值。 */
function usageBarWidth(metric: D1UsageMetric): string {
  return `${metric.ratio === null ? 100 : Math.min(100, Math.max(0, metric.ratio * 100))}%`;
}

/** D1 免费额度纵轴显示百分比；超过 100% 时保留实际比例，便于识别超额。 */
function formatUsagePercent(value: number | Date): string {
  return `${Math.round(Number(value))}%`;
}

/** 把趋势数据按当前按钮组选择裁剪。 */
const visibleOrderTrend = computed(() => data.value?.orderTrend.slice(-selectedOrderRange.value) ?? []);
/** 构造可点击指标卡片的路由，避免不同查询参数对象被推断成互斥联合类型。 */
function metricPath(path: string, query?: Record<string, string>): NonNullable<DashboardMetric["path"]> {
  return query ? { path, query } : { path };
}

const metrics = computed<DashboardMetric[]>(() => {
  const value = data.value?.metrics;
  return [
    { label: "今日订单", value: String(value?.todayOrders ?? 0), description: "今日创建", path: null },
    { label: "今日收款", value: formatFen(value?.todayPaidFen), description: "今日已支付", path: null },
    { label: "累计收款", value: formatFen(value?.totalPaidFen), description: "全部已支付", path: null },
    { label: "待完成回调", value: String(value?.pendingNotify ?? 0), description: "等待执行或重试", path: metricPath("/orders", { notifyStatus: "ACTIVE" }) },
    { label: "回调失败", value: String(value?.failedNotify ?? 0), description: "需要处理", path: metricPath("/orders", { notifyStatus: "FAILED" }) },
    { label: "未匹配收款", value: String(value?.unmatched ?? 0), description: "需要核对", path: metricPath("/unmatched") },
    { label: "超时未处理", value: String(value?.staleUnmatched ?? 0), description: "超过 24 小时", path: metricPath("/unmatched", { overdue: "1" }) },
  ];
});

/** 只让具有明确处理入口的指标卡片执行页面跳转。 */
function openMetric(path: { path: string; query?: Record<string, string> } | null): void {
  if (path) void router.push(path);
}
const usageCards = computed(() => d1Usage.value ? [
  { label: "当前计费日读取行数", value: formatCount(d1Usage.value.today.rowsRead.used), metric: d1Usage.value.today.rowsRead, description: metricDescription(d1Usage.value.today.rowsRead) },
  { label: "当前计费日写入行数", value: formatCount(d1Usage.value.today.rowsWritten.used), metric: d1Usage.value.today.rowsWritten, description: metricDescription(d1Usage.value.today.rowsWritten) },
  { label: "当前计费日查询次数", value: formatCount(d1Usage.value.today.queries), metric: null, description: "Cloudflare 统计的查询次数" },
  { label: "数据库存储", value: formatBytes(d1Usage.value.storage.used), metric: d1Usage.value.storage, description: metricDescription(d1Usage.value.storage, formatBytes) },
] : []);
const usageTrend = computed<UsageTrendPoint[]>(() => {
  const usage = d1Usage.value;
  if (!usage) return [];
  const readLimit = usage.today.rowsRead.limit;
  const writeLimit = usage.today.rowsWritten.limit;
  return usage.trend.map((day) => ({
    date: day.date,
    timestamp: Date.parse(`${day.date}T00:00:00Z`),
    readUsagePercent: readLimit ? Number(((day.rowsRead / readLimit) * 100).toFixed(2)) : day.rowsRead,
    writeUsagePercent: writeLimit ? Number(((day.rowsWritten / writeLimit) * 100).toFixed(2)) : day.rowsWritten,
  }));
});
/** 把 D1 用量趋势按当前按钮组选择裁剪。 */
const visibleUsageTrend = computed(() => usageTrend.value.slice(-selectedUsageRange.value));

/** 计算缓存数据量占运行预算的比例。 */
function cacheUsagePercent(stats: RuntimeCacheStats): string {
  if (!stats.budgetBytes) return "0";
  return Math.min(100, (stats.estimatedBytes / stats.budgetBytes) * 100).toFixed(2).replace(/\.00$/, "");
}

/** 切换 D1 用量周期；Cloudflare 只提供最近约 31 天，因此不接受 90 天选项。 */
function selectUsageRange(days: 7 | 30 | 90): void {
  if (days === 7 || days === 30) selectedUsageRange.value = days;
}

onMounted(() => void load());
</script>

<template>
  <section class="flex w-full min-w-0 flex-col gap-6">
    <AdminPageHeader>
      <template #actions>
        <Button variant="outline" size="sm" :disabled="refreshing" @click="refresh"><RefreshCwIcon :class="refreshing ? 'animate-spin' : undefined" />刷新</Button>
      </template>
    </AdminPageHeader>

    <Alert v-if="failure" variant="destructive">
      <AlertTitle>面板读取失败</AlertTitle>
      <AlertDescription class="break-words">{{ failure }}</AlertDescription>
    </Alert>

    <Alert v-if="usageFailure" variant="destructive">
      <AlertTitle>D1 用量读取失败</AlertTitle>
      <AlertDescription class="break-words">{{ usageFailure }}</AlertDescription>
    </Alert>

    <Card class="min-w-0 gap-0 overflow-hidden py-0">
      <div class="grid gap-px bg-border sm:grid-cols-2 xl:grid-cols-7">
        <div
          v-for="(metric, index) in metrics"
          :key="metric.label"
          class="min-w-0 bg-card"
          :class="index === metrics.length - 1 ? 'sm:col-span-2 xl:col-span-1' : undefined"
        >
          <button
            v-if="metric.path"
            type="button"
            class="grid h-full w-full min-w-0 gap-1 p-4 text-left transition-colors hover:bg-muted/40 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-ring"
            @click="openMetric(metric.path)"
          >
            <span class="text-xs text-muted-foreground">{{ metric.label }}</span>
            <span class="break-words text-2xl font-semibold tabular-nums">{{ metric.value }}</span>
            <span class="text-xs text-muted-foreground">{{ metric.description }}</span>
          </button>
          <div v-else class="grid h-full min-w-0 gap-1 p-4">
            <span class="text-xs text-muted-foreground">{{ metric.label }}</span>
            <span class="break-words text-2xl font-semibold tabular-nums">{{ metric.value }}</span>
            <span class="text-xs text-muted-foreground">{{ metric.description }}</span>
          </div>
        </div>
      </div>
    </Card>

    <Card v-if="d1Usage" class="min-w-0 gap-0 overflow-hidden py-0">
      <div class="grid gap-px bg-border sm:grid-cols-2 xl:grid-cols-4">
        <div v-for="item in usageCards" :key="item.label" class="grid min-w-0 gap-2 bg-card p-4">
          <p class="text-xs text-muted-foreground">{{ item.label }}</p>
          <p class="break-words text-2xl font-semibold tabular-nums">{{ item.value }}</p>
          <p class="break-words text-xs text-muted-foreground">{{ item.description }}</p>
          <div v-if="item.metric" class="h-1.5 overflow-hidden rounded-full bg-muted">
            <div class="h-full rounded-full transition-[width]" :class="usageBarClass(item.metric)" :style="{ width: usageBarWidth(item.metric) }" />
          </div>
        </div>
      </div>
    </Card>

    <Card v-if="runtimeCache" class="min-w-0 gap-0 overflow-hidden py-0">
      <CardHeader class="flex flex-col gap-3 border-b py-4 sm:flex-row sm:items-center sm:justify-between">
        <div>
          <CardTitle>运行缓存</CardTitle>
          <CardDescription class="mt-1">当前缓存状态</CardDescription>
        </div>
        <Button variant="outline" size="sm" @click="clearCache">清空运行缓存</Button>
      </CardHeader>
      <CardContent class="grid min-w-0 gap-px bg-border p-0 sm:grid-cols-2 xl:grid-cols-6">
        <div class="bg-card p-4"><p class="text-xs text-muted-foreground">缓存条目</p><p class="mt-1 text-lg font-semibold">{{ runtimeCache.entries }}</p></div>
        <div class="bg-card p-4"><p class="text-xs text-muted-foreground">缓存占用</p><p class="mt-1 text-lg font-semibold">约 {{ formatBytes(runtimeCache.estimatedBytes) }}</p><p class="mt-1 text-xs text-muted-foreground">{{ formatBytes(runtimeCache.budgetBytes) }} 上限 · {{ cacheUsagePercent(runtimeCache) }}%</p></div>
        <div class="bg-card p-4"><p class="text-xs text-muted-foreground">缓存命中</p><p class="mt-1 text-lg font-semibold">{{ formatCount(runtimeCache.hits) }}</p></div>
        <div class="bg-card p-4"><p class="text-xs text-muted-foreground">缓存未命中</p><p class="mt-1 text-lg font-semibold">{{ formatCount(runtimeCache.misses) }}</p></div>
        <div class="bg-card p-4"><p class="text-xs text-muted-foreground">D1 读取</p><p class="mt-1 text-lg font-semibold">{{ formatCount(runtimeCache.d1Reads) }}</p></div>
        <div class="bg-card p-4"><p class="text-xs text-muted-foreground">自动淘汰</p><p class="mt-1 text-lg font-semibold">{{ formatCount(runtimeCache.evictions) }}</p></div>
      </CardContent>
    </Card>

    <div class="grid min-w-0 items-stretch gap-6 xl:grid-cols-2">
      <Card class="flex h-full min-w-0 flex-col">
        <CardHeader class="flex flex-col gap-4 sm:flex-row sm:items-start sm:justify-between">
          <div class="space-y-1.5">
            <CardTitle>D1 用量趋势</CardTitle>
            <CardDescription>按 Cloudflare 计费日统计，北京时间每日 08:00 重置。</CardDescription>
          </div>
          <ButtonGroup aria-label="选择 D1 用量统计周期">
            <Button
              v-for="option in usageRangeOptions"
              :key="option.days"
              size="sm"
              :variant="selectedUsageRange === option.days ? 'secondary' : 'outline'"
              :aria-pressed="selectedUsageRange === option.days"
              :disabled="option.disabled"
              :title="option.disabled ? 'Cloudflare D1 Analytics 仅提供最近约 31 天数据' : undefined"
              @click="selectUsageRange(option.days)"
            >{{ option.label }}</Button>
          </ButtonGroup>
        </CardHeader>
        <CardContent v-if="d1Usage" class="flex min-w-0 flex-1 flex-col">
          <ChartContainer :config="usageChartConfig" class="h-80 min-h-65 w-full sm:h-95">
            <VisXYContainer :data="visibleUsageTrend" :margin="{ left: 8, right: 8, top: 12, bottom: 8 }">
              <VisArea :x="usageXAccessor" :y="readUsageAccessor" :color="usageChartConfig.readUsagePercent.color" :opacity="0.18" :line="true" :line-color="usageChartConfig.readUsagePercent.color" :line-width="2" curve-type="monotoneX" />
              <VisArea :x="usageXAccessor" :y="writeUsageAccessor" :color="usageChartConfig.writeUsagePercent.color" :opacity="0.15" :line="true" :line-color="usageChartConfig.writeUsagePercent.color" :line-width="2" curve-type="monotoneX" />
              <VisAxis type="x" :x="usageXAccessor" :tick-format="formatAxisDate" :num-ticks="selectedUsageRange === 7 ? 7 : 6" :tick-line="false" :domain-line="false" :tick-text-hide-overlapping="true" />
              <VisAxis type="y" :y="readUsageAccessor" :tick-format="formatUsagePercent" :num-ticks="5" :tick-line="false" :domain-line="false" />
              <ChartTooltip />
              <ChartCrosshair :x="usageXAccessor" :y="[readUsageAccessor, writeUsageAccessor]" :color="usageCrosshairColors" :template="usageTooltipTemplate" />
            </VisXYContainer>
          </ChartContainer>
          <div class="mt-3 flex flex-wrap items-center justify-center gap-5 text-sm text-muted-foreground">
            <div v-for="(item, key) in usageChartConfig" :key="key" class="flex items-center gap-2"><span class="size-2.5 rounded-full" :style="{ backgroundColor: item.color }" />{{ item.label }}</div>
          </div>
        </CardContent>
        <CardContent v-else-if="!usageFailure" class="text-sm text-muted-foreground">正在读取 Cloudflare D1 用量…</CardContent>
      </Card>

      <Card class="flex h-full min-w-0 flex-col">
        <CardHeader class="flex flex-col gap-4 sm:flex-row sm:items-start sm:justify-between">
          <div class="space-y-1.5"><CardTitle>订单趋势</CardTitle><CardDescription>按北京时间自然日统计新建与已支付订单。</CardDescription></div>
          <ButtonGroup aria-label="选择统计周期">
            <Button v-for="option in rangeOptions" :key="option.days" size="sm" :variant="selectedOrderRange === option.days ? 'secondary' : 'outline'" :aria-pressed="selectedOrderRange === option.days" @click="selectedOrderRange = option.days">{{ option.label }}</Button>
          </ButtonGroup>
        </CardHeader>
        <CardContent class="flex min-w-0 flex-1 flex-col">
          <ChartContainer :config="chartConfig" class="h-80 min-h-65 w-full sm:h-95">
            <VisXYContainer :data="visibleOrderTrend" :margin="{ left: 8, right: 8, top: 12, bottom: 8 }">
              <VisArea :x="xAccessor" :y="createdOrdersAccessor" :color="chartConfig.createdOrders.color" :opacity="0.18" :line="true" :line-color="chartConfig.createdOrders.color" :line-width="2" curve-type="monotoneX" />
              <VisArea :x="xAccessor" :y="paidOrdersAccessor" :color="chartConfig.paidOrders.color" :opacity="0.15" :line="true" :line-color="chartConfig.paidOrders.color" :line-width="2" curve-type="monotoneX" />
              <VisAxis type="x" :x="xAccessor" :tick-format="formatAxisDate" :num-ticks="selectedOrderRange === 7 ? 7 : 6" :tick-line="false" :domain-line="false" :tick-text-hide-overlapping="true" />
              <VisAxis type="y" :y="createdOrdersAccessor" :tick-format="formatOrderCount" :num-ticks="5" :tick-line="false" :domain-line="false" />
              <ChartTooltip />
              <ChartCrosshair :x="xAccessor" :y="[createdOrdersAccessor, paidOrdersAccessor]" :color="crosshairColors" :template="tooltipTemplate" />
            </VisXYContainer>
          </ChartContainer>
          <div class="mt-3 flex flex-wrap items-center justify-center gap-5 text-sm text-muted-foreground">
            <div v-for="(item, key) in chartConfig" :key="key" class="flex items-center gap-2"><span class="size-2.5 rounded-full" :style="{ backgroundColor: item.color }" />{{ item.label }}</div>
          </div>
        </CardContent>
      </Card>
    </div>

  </section>
</template>
