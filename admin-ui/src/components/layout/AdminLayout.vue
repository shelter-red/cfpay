<script setup lang="ts">
import { computed } from "vue";
import { useRoute } from "vue-router";
import AdminSidebar from "@/components/AdminSidebar.vue";
import { Separator } from "@/components/ui/separator";
import { SidebarInset, SidebarProvider, SidebarTrigger } from "@/components/ui/sidebar";
import { getAdminBreadcrumb } from "@/lib/admin-navigation";

const route = useRoute();
const routeMeta = computed(() => getAdminBreadcrumb(route.path));
</script>

<template>
  <SidebarProvider class="min-h-svh bg-muted/40">
    <AdminSidebar />
    <SidebarInset class="min-w-0">
      <header
        class="sticky top-0 z-20 flex h-14 shrink-0 items-center gap-2 border-b bg-background px-4 md:rounded-t-xl"
      >
        <SidebarTrigger class="-ml-1 cursor-pointer" />
        <Separator orientation="vertical" class="h-4" />
        <nav class="flex items-center gap-2 text-sm" aria-label="当前位置">
          <template v-for="(title, index) in routeMeta.titles" :key="`${index}-${title}`">
            <span v-if="index > 0" class="text-muted-foreground">/</span>
            <span :class="index === routeMeta.titles.length - 1 ? 'font-medium text-foreground' : 'text-muted-foreground'">
              {{ title }}
            </span>
          </template>
        </nav>
      </header>
      <main class="@container/main flex min-w-0 flex-1 flex-col p-4 lg:p-6">
        <RouterView />
      </main>
    </SidebarInset>
  </SidebarProvider>
</template>
