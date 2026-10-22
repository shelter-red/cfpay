<script setup lang="ts">
import { ref } from "vue";
import { toast } from "vue-sonner";
import { CheckIcon, CopyIcon } from "@lucide/vue";
import { Button } from "@/components/ui/button";
import { copyText } from "@/lib/secrets";
import { errorMessage } from "@/lib/api";

const props = withDefaults(defineProps<{ value: string; label?: string; size?: "sm" | "default" | "icon" }>(), {
  label: "复制",
  size: "sm",
});

const copied = ref(false);

async function onCopy() {
  try {
    await copyText(props.value);
    copied.value = true;
    setTimeout(() => (copied.value = false), 1600);
  } catch (error) {
    toast.error(errorMessage(error));
  }
}
</script>

<template>
  <Button type="button" variant="outline" :size="props.size" :disabled="!props.value" @click="onCopy">
    <CheckIcon v-if="copied" />
    <CopyIcon v-else />
    <span v-if="props.size !== 'icon'">{{ copied ? "已复制" : props.label }}</span>
  </Button>
</template>
