<script setup lang="ts">
import { Card, CardContent, CardDescription, CardFooter, CardHeader, CardTitle } from "@/components/ui/card";

defineProps<{ title: string; description?: string; status?: string }>();
</script>

<template>
  <Card class="gap-4 py-5">
    <CardHeader class="px-5">
      <div class="flex min-w-0 flex-wrap items-start justify-between gap-2">
        <div class="space-y-1">
          <CardTitle>{{ title }}</CardTitle>
          <CardDescription v-if="description">{{ description }}</CardDescription>
        </div>
        <span v-if="status" class="text-xs text-muted-foreground">{{ status }}</span>
      </div>
    </CardHeader>
    <CardContent class="grid min-w-0 gap-4 px-5">
      <slot />
    </CardContent>
    <CardFooter v-if="$slots.footer" class="flex min-w-0 flex-wrap items-center justify-between gap-3 border-t px-5 pt-4">
      <slot name="footer" />
    </CardFooter>
  </Card>
</template>
