<script setup lang="ts">
import { onMounted, reactive, ref } from "vue";
import { useRouter } from "vue-router";
import { toast } from "vue-sonner";
import AdminPageHeader from "@/components/admin/AdminPageHeader.vue";
import SettingSection from "@/components/admin/SettingSection.vue";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { api, errorMessage } from "@/lib/api";
import { markLoggedOut } from "@/composables/useSession";

const router = useRouter();
const profile = reactive({ username: "", adminPath: "" });
const password = reactive({ current: "", next: "", confirm: "" });
const loading = ref(true);
const savingProfile = ref(false);
const savingPassword = ref(false);
const failure = ref("");
const failureTitle = ref("账户设置读取失败");

/** 读取当前管理员账号与入口路径。 */
async function load(): Promise<void> {
  try {
    const settings = await api<{ username: string; adminPath: string }>("/admin-api/admin-settings");
    profile.username = settings.username;
    profile.adminPath = settings.adminPath;
  } catch (error) {
    failureTitle.value = "账户设置读取失败";
    failure.value = errorMessage(error);
  } finally {
    loading.value = false;
  }
}

/** 保存管理员账号；账号变化后服务端会使旧会话失效。 */
async function saveProfile(): Promise<void> {
  savingProfile.value = true;
  failure.value = "";
  try {
    await api("/admin-api/admin-settings", {
      method: "PUT",
      body: JSON.stringify({ username: profile.username, adminPath: profile.adminPath }),
    });
    markLoggedOut();
    toast.success("管理员账号已保存，请重新登录");
    await router.replace({ name: "login" });
  } catch (error) {
    failureTitle.value = "管理员账号保存失败";
    failure.value = errorMessage(error);
  } finally {
    savingProfile.value = false;
  }
}

/** 验证当前密码并修改新密码。 */
async function changePassword(): Promise<void> {
  if (password.next !== password.confirm) {
    failure.value = "两次输入的新密码不一致";
    failureTitle.value = "密码修改失败";
    return;
  }
  savingPassword.value = true;
  failure.value = "";
  try {
    await api("/admin-api/account/password", {
      method: "PUT",
      body: JSON.stringify({ currentPassword: password.current, newPassword: password.next }),
    });
    markLoggedOut();
    toast.success("密码已修改，请重新登录");
    await router.replace({ name: "login" });
  } catch (error) {
    failureTitle.value = "密码修改失败";
    failure.value = errorMessage(error);
  } finally {
    savingPassword.value = false;
  }
}

onMounted(load);
</script>

<template>
  <section class="flex w-full min-w-0 flex-col gap-6">
    <AdminPageHeader />
    <Alert v-if="failure" variant="destructive"><AlertTitle>{{ failureTitle }}</AlertTitle><AlertDescription class="break-words">{{ failure }}</AlertDescription></Alert>
    <div v-if="loading" class="grid w-full gap-6"><div class="h-48 animate-pulse rounded-lg border bg-muted/30" /><div class="h-64 animate-pulse rounded-lg border bg-muted/30" /></div>
    <div v-else class="grid w-full gap-6">
      <SettingSection title="管理员账号" description="修改当前登录账号。后台入口路径在“安全配置”中管理。">
        <form class="grid w-full min-w-0 gap-4" @submit.prevent="saveProfile">
          <div class="grid w-full min-w-0 gap-2"><Label for="account-username">管理员账号</Label><Input id="account-username" v-model="profile.username" autocomplete="username" required /></div>
          <div><Button type="submit" :disabled="savingProfile">{{ savingProfile ? "保存中…" : "保存管理员账号" }}</Button></div>
        </form>
      </SettingSection>

      <SettingSection title="修改密码" description="修改密码前，请先验证当前密码。保存后需要重新登录。">
        <form class="grid w-full min-w-0 gap-4" @submit.prevent="changePassword">
          <div class="grid w-full min-w-0 gap-2"><Label for="current-password">当前密码</Label><Input id="current-password" v-model="password.current" type="password" autocomplete="current-password" required /></div>
          <div class="grid w-full min-w-0 gap-2"><Label for="new-password">新密码</Label><Input id="new-password" v-model="password.next" type="password" autocomplete="new-password" minlength="12" required /></div>
          <div class="grid w-full min-w-0 gap-2"><Label for="confirm-password">确认新密码</Label><Input id="confirm-password" v-model="password.confirm" type="password" autocomplete="new-password" minlength="12" required /></div>
          <div><Button type="submit" :disabled="savingPassword">{{ savingPassword ? "修改中…" : "修改密码" }}</Button></div>
        </form>
      </SettingSection>
    </div>
  </section>
</template>
