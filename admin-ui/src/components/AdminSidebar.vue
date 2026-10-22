<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from "vue";
import { RouterLink, useRoute, useRouter } from "vue-router";
import { toast } from "vue-sonner";
import { CollapsibleContent, CollapsibleRoot as Collapsible, CollapsibleTrigger } from "reka-ui";
import {
  ChevronRightIcon, ChevronUpIcon, ChevronsUpDownIcon, ClipboardListIcon, ExternalLinkIcon,
  FlaskConicalIcon, InfoIcon, LayoutDashboardIcon, LogOutIcon, SettingsIcon, UserRoundCogIcon, WalletCardsIcon,
} from "@lucide/vue";
import { Avatar, AvatarFallback } from "@/components/ui/avatar";
import {
  DropdownMenu, DropdownMenuContent, DropdownMenuGroup, DropdownMenuItem, DropdownMenuLabel,
  DropdownMenuSeparator, DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import {
  Sidebar, SidebarContent, SidebarFooter, SidebarGroup, SidebarGroupContent, SidebarGroupLabel,
  SidebarHeader, SidebarMenu, SidebarMenuButton, SidebarMenuItem, SidebarMenuSub, SidebarMenuSubButton,
  SidebarMenuSubItem, useSidebar,
} from "@/components/ui/sidebar";
import { logout, sessionUsername } from "@/composables/useSession";
import { api } from "@/lib/api";
import type { SiteSettings } from "@/lib/types";
import {
  adminNavigation, adminPages, isAdminNavigationGroupActive, isAdminNavigationItemActive,
  isNavigationModule, type AdminNavigationGroup, type AdminPageMeta,
} from "@/lib/admin-navigation";

const route = useRoute();
const router = useRouter();
const { isMobile, setOpenMobile } = useSidebar();
const appVersion = __APP_VERSION__;
const updateStatus = ref("检查更新");
const lastCheckTime = ref(0);
const UPDATE_COOLDOWN_MS = 10 * 60 * 1000;
// 更新检查与仓库链接均由构建环境提供，仓库不内置任何真实地址。
const UPDATE_MANIFEST_URL = (import.meta.env.VITE_UPDATE_MANIFEST_URL ?? "").trim();
const REPOSITORY_URL = (import.meta.env.VITE_REPOSITORY_URL ?? "").trim();
const siteName = ref("CFPay");
const siteSubtitle = ref("个人收款控制台");
const siteIcon = ref("/favicon.svg");

const displayName = computed(() => sessionUsername.value?.trim() || "管理员");
const userInitials = computed(() => displayName.value.slice(0, 2).toUpperCase());
const itemIcons = new Map<string, unknown>([
  [adminPages.dashboard.path, LayoutDashboardIcon],
  [adminPages.wechat.path, WalletCardsIcon],
  [adminPages.orders.path, ClipboardListIcon],
  [adminPages.quickPay.path, FlaskConicalIcon],
  [adminPages.gateway.path, SettingsIcon],
]);

/** 根据页面路径返回侧栏图标；分组图标使用其第一个页面的路径。 */
function iconFor(path: string) {
  return itemIcons.get(path) ?? SettingsIcon;
}

/** 当前页面变化时关闭移动端抽屉，避免菜单盖住新页面。 */
watch(() => route.path, () => {
  if (isMobile.value) setOpenMobile(false);
});

/** 判断页面项是否为当前路由。 */
function isActive(item: AdminPageMeta): boolean {
  return isAdminNavigationItemActive(item, route.path);
}

/** 判断分组是否包含当前路由。 */
function isGroupActive(group: AdminNavigationGroup): boolean {
  return isAdminNavigationGroupActive(group, route.path);
}

/** 比较点分版本号，供顶部关于菜单显示服务端是否已有新版本。 */
function compareVersions(current: string, latest: string): number {
  const currentParts = current.split(".").map(Number);
  const latestParts = latest.split(".").map(Number);
  const length = Math.max(currentParts.length, latestParts.length);
  for (let index = 0; index < length; index += 1) {
    const currentPart = currentParts[index] || 0;
    const latestPart = latestParts[index] || 0;
    if (currentPart !== latestPart) return currentPart > latestPart ? 1 : -1;
  }
  return 0;
}

/** 应用站点设置到侧栏顶部品牌入口。 */
function applySiteBrand(settings: Pick<SiteSettings, "siteName" | "siteSubtitle" | "faviconDataUrl">): void {
  siteName.value = settings.siteName.trim() || "CFPay";
  siteSubtitle.value = settings.siteSubtitle.trim() || "个人收款控制台";
  siteIcon.value = settings.faviconDataUrl || "/favicon.svg";
}

/** 读取站点设置；失败时保留内置品牌，避免影响管理端导航。 */
async function loadSiteBrand(): Promise<void> {
  try {
    applySiteBrand(await api<SiteSettings>("/admin-api/site-settings"));
  } catch {
    // 品牌信息不是管理端可用性的前置条件，接口错误由站点设置页集中提示。
  }
}

/** 接收站点设置页保存成功事件，让顶部图标和文案无需刷新即可更新。 */
function onSiteSettingsUpdated(event: Event): void {
  const settings = (event as CustomEvent<SiteSettings>).detail;
  if (settings) applySiteBrand(settings);
}

/** 查询 GitHub 主分支发布版本；十分钟内重复点击只提示等待。 */
async function checkUpdate(): Promise<void> {
  if (!UPDATE_MANIFEST_URL) {
    updateStatus.value = "未配置";
    return;
  }
  const now = Date.now();
  if (now - lastCheckTime.value < UPDATE_COOLDOWN_MS) {
    updateStatus.value = "请稍后";
    return;
  }
  lastCheckTime.value = now;
  updateStatus.value = "检查中…";
  try {
    const response = await fetch(UPDATE_MANIFEST_URL, { cache: "no-store" });
    const packageInfo = await response.json() as { version?: string };
    if (!response.ok || !packageInfo.version) throw new Error("缺少版本信息");
    updateStatus.value = compareVersions(appVersion, packageInfo.version) >= 0 ? "已是最新" : `最新 v${packageInfo.version}`;
  } catch {
    updateStatus.value = "检查失败";
  }
}

/** 退出当前管理员会话并返回登录页。 */
async function onSignOut(): Promise<void> {
  await logout();
  toast.success("已退出登录");
  await router.replace({ name: "login" });
}

onMounted(() => {
  void loadSiteBrand();
  window.addEventListener("cfpay:site-settings-updated", onSiteSettingsUpdated);
});

onBeforeUnmount(() => {
  window.removeEventListener("cfpay:site-settings-updated", onSiteSettingsUpdated);
});
</script>

<template>
  <Sidebar variant="inset" collapsible="icon">
    <SidebarHeader>
      <SidebarMenu>
        <SidebarMenuItem>
          <DropdownMenu>
            <DropdownMenuTrigger as-child>
              <SidebarMenuButton size="lg" class="h-14 cursor-pointer data-[state=open]:bg-sidebar-accent data-[state=open]:text-sidebar-accent-foreground">
                <img :src="siteIcon" :alt="`${siteName} 图标`" class="aspect-square size-10 rounded-lg object-contain" />
                <div class="grid min-w-0 flex-1 text-left leading-tight"><span class="break-words text-sm font-semibold">{{ siteName }}</span><span class="break-words text-xs">{{ siteSubtitle }}</span></div>
                <ChevronsUpDownIcon class="ml-auto" />
              </SidebarMenuButton>
            </DropdownMenuTrigger>
            <DropdownMenuContent side="right" align="start" :side-offset="8" class="w-64">
              <DropdownMenuLabel>关于 {{ siteName }}</DropdownMenuLabel>
              <DropdownMenuSeparator />
              <DropdownMenuGroup>
                <DropdownMenuItem @select.prevent="checkUpdate"><InfoIcon data-icon="inline-start" /><span>当前版本 v{{ appVersion }}</span><span class="ml-auto text-xs text-muted-foreground">{{ updateStatus }}</span></DropdownMenuItem>
                <DropdownMenuItem v-if="REPOSITORY_URL" as-child><a :href="REPOSITORY_URL" target="_blank" rel="noopener noreferrer"><ExternalLinkIcon data-icon="inline-start" />GitHub 仓库</a></DropdownMenuItem>
              </DropdownMenuGroup>
            </DropdownMenuContent>
          </DropdownMenu>
        </SidebarMenuItem>
      </SidebarMenu>
    </SidebarHeader>

    <SidebarContent>
      <SidebarGroup>
        <SidebarGroupLabel>{{ adminNavigation.title }}</SidebarGroupLabel>
        <SidebarGroupContent>
          <SidebarMenu>
            <SidebarMenuItem v-for="item in adminNavigation.items" :key="isNavigationModule(item) ? item.title : item.path">
              <SidebarMenuButton v-if="!isNavigationModule(item)" as-child :is-active="isActive(item)">
                <RouterLink :to="item.path"><component :is="iconFor(item.path)" /><span>{{ item.title }}</span></RouterLink>
              </SidebarMenuButton>
              <Collapsible v-else default-open class="group/collapsible">
                <CollapsibleTrigger as-child>
                  <SidebarMenuButton :is-active="isGroupActive(item)"><component :is="iconFor(item.items[0]?.path ?? '')" /><span>{{ item.title }}</span><ChevronRightIcon class="ml-auto transition-transform group-data-[state=open]/collapsible:rotate-90" /></SidebarMenuButton>
                </CollapsibleTrigger>
                <CollapsibleContent>
                  <SidebarMenuSub>
                    <SidebarMenuSubItem v-for="child in item.items" :key="child.path">
                      <SidebarMenuSubButton as-child :is-active="isActive(child)"><RouterLink :to="child.path"><span>{{ child.title }}</span></RouterLink></SidebarMenuSubButton>
                    </SidebarMenuSubItem>
                  </SidebarMenuSub>
                </CollapsibleContent>
              </Collapsible>
            </SidebarMenuItem>
          </SidebarMenu>
        </SidebarGroupContent>
      </SidebarGroup>
    </SidebarContent>

    <SidebarFooter>
      <SidebarMenu>
        <SidebarMenuItem>
          <DropdownMenu>
            <DropdownMenuTrigger as-child>
              <SidebarMenuButton size="lg" class="cursor-pointer data-[state=open]:bg-sidebar-accent data-[state=open]:text-sidebar-accent-foreground">
                <Avatar class="size-8 rounded-lg"><AvatarFallback class="rounded-lg">{{ userInitials }}</AvatarFallback></Avatar>
                <div class="grid min-w-0 flex-1 text-left text-sm leading-tight"><span class="break-all font-medium">{{ displayName }}</span><span class="break-words text-xs">当前登录账号</span></div>
                <ChevronUpIcon class="ml-auto" />
              </SidebarMenuButton>
            </DropdownMenuTrigger>
            <DropdownMenuContent side="right" align="end" :side-offset="8" class="w-48">
              <DropdownMenuLabel>{{ displayName }}</DropdownMenuLabel>
              <DropdownMenuSeparator />
              <DropdownMenuGroup>
                <DropdownMenuItem as-child><RouterLink :to="adminPages.account.path"><UserRoundCogIcon data-icon="inline-start" />账户设置</RouterLink></DropdownMenuItem>
                <DropdownMenuItem @select="onSignOut"><LogOutIcon data-icon="inline-start" />退出登录</DropdownMenuItem>
              </DropdownMenuGroup>
            </DropdownMenuContent>
          </DropdownMenu>
        </SidebarMenuItem>
      </SidebarMenu>
    </SidebarFooter>
  </Sidebar>
</template>
