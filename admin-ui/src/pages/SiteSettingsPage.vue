<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { RotateCcwIcon } from "@lucide/vue";
import { toast } from "vue-sonner";
import AdminPageHeader from "@/components/admin/AdminPageHeader.vue";
import SettingSection from "@/components/admin/SettingSection.vue";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import { FieldDescription } from "@/components/ui/field";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Skeleton } from "@/components/ui/skeleton";
import { Textarea } from "@/components/ui/textarea";
import { api, errorMessage } from "@/lib/api";
import type { SiteSettings } from "@/lib/types";

const settings = ref<SiteSettings | null>(null);
const saving = ref(false);
const failure = ref("");
const failureTitle = ref("站点设置读取失败");
let faviconInput: HTMLInputElement | null = null;
const faviconPreview = computed(() => settings.value?.faviconDataUrl || "/favicon.svg");

/** 读取当前公开站点设置。 */
async function load(): Promise<void> {
  failure.value = "";
  try {
    settings.value = await api<SiteSettings>("/admin-api/site-settings");
  } catch (error) {
    failureTitle.value = "站点设置读取失败";
    failure.value = errorMessage(error);
  }
}

/** 保存品牌、图标和页脚设置。 */
async function onSubmit(): Promise<void> {
  if (!settings.value) return;
  saving.value = true;
  failure.value = "";
  try {
    await api("/admin-api/site-settings", { method: "PUT", body: JSON.stringify(settings.value) });
    toast.success("站点设置已保存");
    await load();
    publishSiteSettings();
  } catch (error) {
    failureTitle.value = "站点设置保存失败";
    failure.value = errorMessage(error);
  } finally {
    saving.value = false;
  }
}

/** 读取浏览器选择的站点图标并写入待保存表单。 */
function onFaviconChange(event: Event): void {
  faviconInput = event.target as HTMLInputElement;
  const file = faviconInput.files?.[0];
  if (!file || !settings.value) return;
  const supportedType = ["image/png", "image/jpeg", "image/webp", "image/x-icon", "image/vnd.microsoft.icon"].includes(file.type);
  if (!supportedType && !/\.ico$/i.test(file.name)) {
    toast.error("站点图标仅支持 PNG、JPEG、WebP 或 ICO");
    faviconInput.value = "";
    return;
  }
  if (file.size > 512 * 1024) {
    toast.error("站点图标不能超过 512 KB");
    faviconInput.value = "";
    return;
  }
  const reader = new FileReader();
  reader.onload = () => {
    if (typeof reader.result === "string" && settings.value) {
      settings.value.faviconDataUrl = !file.type && /\.ico$/i.test(file.name)
        ? reader.result.replace(/^data:;base64,/, "data:image/x-icon;base64,")
        : reader.result;
    }
    if (faviconInput) faviconInput.value = "";
  };
  reader.onerror = () => toast.error("站点图标读取失败");
  reader.readAsDataURL(file);
}

/** 清除自定义图标并恢复内置 favicon。 */
function clearFavicon(): void {
  if (!settings.value) return;
  settings.value.faviconDataUrl = "";
  if (faviconInput) faviconInput.value = "";
}

/** 通知管理端外壳即时刷新顶部品牌图标与文案。 */
function publishSiteSettings(): void {
  if (!settings.value) return;
  window.dispatchEvent(new CustomEvent<SiteSettings>("cfpay:site-settings-updated", { detail: { ...settings.value } }));
}

onMounted(load);
</script>

<template>
  <section class="flex w-full min-w-0 flex-col gap-6">
    <AdminPageHeader />

    <Alert v-if="failure" variant="destructive">
      <AlertTitle>{{ failureTitle }}</AlertTitle>
      <AlertDescription class="break-words">{{ failure }}</AlertDescription>
    </Alert>

    <template v-if="!settings">
      <SettingSection title="基础信息" description="收银台顶部显示的站点名称与副标题。">
        <Skeleton class="h-9 w-full" /><Skeleton class="h-9 w-full" />
      </SettingSection>
    </template>
    <form v-else class="grid min-w-0 gap-6" @submit.prevent="onSubmit">
      <SettingSection title="基础信息" description="收银台标题和顶部品牌文案。">
        <div class="grid min-w-0 gap-4 md:grid-cols-2">
          <div class="grid min-w-0 gap-2">
            <Label for="site-name">站点名称</Label>
            <Input id="site-name" v-model="settings.siteName" maxlength="120" required />
          </div>
          <div class="grid min-w-0 gap-2">
            <Label for="site-subtitle">副标题</Label>
            <Input id="site-subtitle" v-model="settings.siteSubtitle" maxlength="300" />
          </div>
        </div>
      </SettingSection>

      <SettingSection title="品牌资源" description="图标用于公开首页、收银台和管理后台的浏览器标签。">
        <div class="flex min-w-0 flex-col gap-4 sm:flex-row sm:items-start">
          <div class="grid size-20 shrink-0 place-items-center overflow-hidden rounded-lg border bg-muted/30 p-2">
            <img :src="faviconPreview" alt="站点图标预览" class="size-full object-contain" />
          </div>
          <div class="grid min-w-0 flex-1 gap-3">
            <div class="grid min-w-0 gap-2">
              <Label for="site-favicon">网站图标</Label>
              <Input id="site-favicon" type="file" accept="image/png,image/jpeg,image/webp,image/x-icon,image/vnd.microsoft.icon,.ico" @change="onFaviconChange" />
              <FieldDescription>支持 PNG、JPEG、WebP 或 ICO，文件不能超过 512 KB。选择后点击“保存设置”生效。</FieldDescription>
            </div>
            <div>
              <Button v-if="settings.faviconDataUrl" type="button" variant="outline" size="sm" @click="clearFavicon">
                <RotateCcwIcon />恢复默认图标
              </Button>
            </div>
          </div>
        </div>
      </SettingSection>

      <SettingSection title="页脚" description="显示在公开首页和收银台底部，请勿填写敏感信息。">
        <div class="grid min-w-0 gap-2">
          <Label for="footer-text">页脚文案</Label>
          <Textarea id="footer-text" v-model="settings.footerText" rows="3" maxlength="1000" placeholder="例如：联系我们|example@site.com" />
          <FieldDescription>每行一条。无“|”时显示普通文字；“显示文字|邮箱、链接或域名”会生成在新窗口打开的链接。</FieldDescription>
        </div>
      </SettingSection>

      <div><Button type="submit" :disabled="saving">{{ saving ? "保存中…" : "保存设置" }}</Button></div>
    </form>
  </section>
</template>
