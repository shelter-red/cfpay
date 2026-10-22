<script setup lang="ts">
import { onMounted, ref } from "vue";
import { Trash2Icon, UploadIcon } from "@lucide/vue";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Skeleton } from "@/components/ui/skeleton";
import { useQrCode } from "@/composables/useQrCode";
import { formatDateTime } from "@/lib/format";
import type { Provider } from "@/lib/types";

const props = defineProps<{ provider: Provider }>();

const { item, loaded, busy, label, reload, upload, remove } = useQrCode(props.provider);
const file = ref<File | null>(null);
// 组件 ref 拿到的是组件实例而不是 DOM 元素，因此从 change 事件里记住真实 input。
let inputElement: HTMLInputElement | null = null;

function onFileChange(event: Event) {
  inputElement = event.target as HTMLInputElement;
  file.value = inputElement.files?.[0] ?? null;
}

async function onUpload() {
  await upload(file.value);
  file.value = null;
  if (inputElement) inputElement.value = "";
}

onMounted(reload);
</script>

<template>
  <Card class="min-w-0 gap-4 py-5">
    <CardHeader class="px-5">
      <CardTitle class="text-base">{{ label }}收款码</CardTitle>
      <CardDescription>
        <template v-if="!loaded">读取中…</template>
        <template v-else-if="item">已配置 · {{ formatDateTime(item.updatedAt) }}</template>
        <template v-else>未配置</template>
      </CardDescription>
    </CardHeader>
    <CardContent class="grid min-w-0 gap-4 px-5 sm:grid-cols-[11rem_minmax(0,1fr)] sm:items-center">
      <div class="grid min-h-44 place-items-center rounded-lg border bg-muted/30 p-3">
        <Skeleton v-if="!loaded" class="size-36" />
        <img v-else-if="item" :src="item.url" :alt="`${label}收款码`" class="size-36 rounded-md bg-white object-contain p-2" />
        <span v-else class="px-4 text-center text-sm text-muted-foreground">尚未上传</span>
      </div>

      <div class="grid min-w-0 gap-4">
        <div class="grid min-w-0 gap-2">
          <Label :for="`qr-${provider}`">二维码图片</Label>
          <Input :id="`qr-${provider}`" type="file" accept="image/png,image/jpeg,.png,.jpg,.jpeg" @change="onFileChange" />
        </div>

        <div class="flex min-w-0 flex-wrap items-center justify-between gap-3">
          <Button type="button" variant="destructive" size="sm" :disabled="!item || busy" @click="remove">
            <Trash2Icon />删除
          </Button>
          <Button type="button" size="sm" :disabled="busy || !file" @click="onUpload">
            <UploadIcon />{{ busy ? "处理中…" : "解析并保存" }}
          </Button>
        </div>
      </div>
    </CardContent>
  </Card>
</template>
