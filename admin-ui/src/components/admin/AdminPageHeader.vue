<script setup lang="ts">
import { computed } from "vue";
import { useRoute } from "vue-router";
import { getAdminPageMeta } from "@/lib/admin-navigation";

const route = useRoute();
const page = computed(() => getAdminPageMeta(route.path));
</script>

<template>
  <div class="flex flex-wrap items-end justify-between gap-3">
    <div>
      <h1 class="text-2xl font-semibold tracking-normal">{{ page?.pageTitle ?? page?.title ?? "管理后台" }}</h1>
      <p v-if="page?.description" class="mt-1 text-sm text-muted-foreground">{{ page.description }}</p>
    </div>
    <slot name="actions" />
  </div>
</template>
