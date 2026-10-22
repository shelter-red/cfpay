<script setup lang="ts">
import { Badge } from "@/components/ui/badge";
import { statusTone } from "@/lib/format";

const props = defineProps<{ value: string; label?: string }>();
</script>

<template>
  <Badge :variant="statusTone(props.value)">{{ props.label ?? props.value }}</Badge>
</template>
