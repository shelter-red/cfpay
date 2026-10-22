<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { toast } from "vue-sonner";
import AdminPageHeader from "@/components/admin/AdminPageHeader.vue";
import CopyButton from "@/components/admin/CopyButton.vue";
import SecretDialog from "@/components/admin/SecretDialog.vue";
import SettingSection from "@/components/admin/SettingSection.vue";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Skeleton } from "@/components/ui/skeleton";
import { api, errorMessage } from "@/lib/api";
import { randomIdentifier, randomSecret } from "@/lib/secrets";
import type { GatewaySettings } from "@/lib/types";

const settings = ref<GatewaySettings | null>(null);
const merchantId = ref("");
const merchantKey = ref("");
const orderTtlMinutes = ref(5);
const amountDirection = ref<"UP" | "DOWN">("UP");
const maxAmountAdjustments = ref(10);
const saving = ref(false);
const failure = ref("");
const failureTitle = ref("支付配置读取失败");

const secretDialogOpen = ref(false);
const secretDialogTitle = ref("");
const secretDialogLabel = ref("");
const secretDialogValue = ref("");

const endpoints = computed(() => {
  const base = settings.value?.gatewayUrl ?? "";
  return [
    { url: `${base}/submit.php`, hint: "POST / GET · 页面支付" },
    { url: `${base}/mapi.php`, hint: "POST / GET · API 支付" },
    { url: `${base}/api.php?act=order`, hint: "GET / POST · 标准订单查询" },
    { url: `${base}/query.php`, hint: "GET / POST · 兼容订单查询" },
  ];
});

async function load() {
  failure.value = "";
  try {
    const data = await api<GatewaySettings>("/admin-api/settings");
    settings.value = data;
    merchantId.value = data.merchantId;
    merchantKey.value = "";
    orderTtlMinutes.value = data.orderTtlMinutes;
    amountDirection.value = data.amountDirection;
    maxAmountAdjustments.value = data.maxAmountAdjustments;
  } catch (error) {
    failureTitle.value = "支付配置读取失败";
    failure.value = errorMessage(error);
  }
}

function generateMerchantKey() {
  const value = randomSecret();
  merchantKey.value = value;
  secretDialogTitle.value = "保存商户密钥";
  secretDialogLabel.value = "新商户密钥";
  secretDialogValue.value = value;
  secretDialogOpen.value = true;
}

function generateMerchantId() {
  merchantId.value = randomIdentifier("m_");
  toast.info("已生成新的易支付商户 ID，保存设置后生效");
}

async function onSubmit() {
  saving.value = true;
  failure.value = "";
  try {
    await api("/admin-api/settings", {
      method: "PUT",
      body: JSON.stringify({
        merchantId: merchantId.value,
        merchantKey: merchantKey.value,
        orderTtlMinutes: Number(orderTtlMinutes.value),
        amountDirection: amountDirection.value,
        maxAmountAdjustments: Number(maxAmountAdjustments.value),
      }),
    });
    toast.success("支付网关设置已保存");
    await load();
  } catch (error) {
    failureTitle.value = "支付配置保存失败";
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

    <Alert v-if="failure" variant="destructive">
      <AlertTitle>{{ failureTitle }}</AlertTitle>
      <AlertDescription>{{ failure }}</AlertDescription>
    </Alert>

    <SettingSection title="易支付兼容接口" description="商户系统对接地址，签名规则为 MD5。">
      <template v-if="!settings">
        <Skeleton class="h-9 w-full max-w-md" />
        <Skeleton class="h-24 w-full" />
      </template>
      <template v-else>
        <div class="grid gap-2">
          <Label for="gateway-url">网关基础地址</Label>
          <div class="flex min-w-0 gap-2">
            <Input id="gateway-url" :model-value="settings.gatewayUrl" readonly class="min-w-0 flex-1 font-mono" />
            <CopyButton :value="settings.gatewayUrl" size="default" class="shrink-0" />
          </div>
        </div>
        <div class="grid min-w-0 gap-2 rounded-lg border bg-muted/30 p-4 sm:grid-cols-2">
          <div v-for="endpoint in endpoints" :key="endpoint.url" class="flex min-w-0 items-center justify-between gap-3">
            <div class="min-w-0">
              <code class="block font-mono text-sm break-all">{{ endpoint.url }}</code>
              <span class="text-xs text-muted-foreground">{{ endpoint.hint }}</span>
            </div>
            <CopyButton :value="endpoint.url" size="icon" label="" class="shrink-0" />
          </div>
        </div>
      </template>
    </SettingSection>

    <SettingSection title="商户凭据与订单规则" description="密钥留空不修改。">
      <template v-if="!settings">
        <Skeleton class="h-9 w-full" />
        <Skeleton class="h-9 w-full" />
      </template>
      <form v-else class="grid gap-4" @submit.prevent="onSubmit">
        <div class="grid min-w-0 gap-4 md:grid-cols-2">
          <div class="grid gap-2">
            <Label for="merchant-id">易支付商户 ID</Label>
            <div class="flex min-w-0 gap-2">
              <Input id="merchant-id" v-model="merchantId" required class="min-w-0 flex-1 font-mono" />
              <Button type="button" variant="outline" class="shrink-0" @click="generateMerchantId">生成</Button>
            </div>
          </div>
          <div class="grid gap-2">
            <Label for="merchant-key">易支付商户密钥</Label>
            <div class="flex min-w-0 gap-2">
              <Input
                id="merchant-key"
                v-model="merchantKey"
                type="password"
                autocomplete="off"
                :placeholder="settings.merchantKeyConfigured ? '已配置，留空不修改（至少 16 位）' : '至少 16 位'"
                class="min-w-0 flex-1"
              />
              <Button type="button" variant="outline" class="shrink-0" @click="generateMerchantKey">生成</Button>
            </div>
          </div>
        </div>

        <div class="grid min-w-0 gap-4 md:grid-cols-3">
          <div class="grid gap-2">
            <Label for="order-ttl">订单有效期（分钟）</Label>
            <Input id="order-ttl" v-model.number="orderTtlMinutes" type="number" min="1" max="60" />
          </div>
          <div class="grid gap-2">
            <Label for="amount-direction">尾差方向</Label>
            <Select v-model="amountDirection">
              <SelectTrigger id="amount-direction" class="w-full">
                <SelectValue placeholder="请选择" />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="UP">递增 +0.01</SelectItem>
                <SelectItem value="DOWN">递减 -0.01</SelectItem>
              </SelectContent>
            </Select>
          </div>
          <div class="grid gap-2">
            <Label for="max-adjustments">最大尾差次数</Label>
            <Input id="max-adjustments" v-model.number="maxAmountAdjustments" type="number" min="0" max="999" />
          </div>
        </div>

        <div>
          <Button type="submit" :disabled="saving">{{ saving ? "保存中…" : "保存网关设置" }}</Button>
        </div>
      </form>
    </SettingSection>

    <SecretDialog
      v-model:open="secretDialogOpen"
      :title="secretDialogTitle"
      :label="secretDialogLabel"
      :value="secretDialogValue"
      saveHint="再点击「保存网关设置」。"
    />
  </section>
</template>
