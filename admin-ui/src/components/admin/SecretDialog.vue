<script setup lang="ts">
import { Button } from "@/components/ui/button";
import {
  Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import CopyButton from "@/components/admin/CopyButton.vue";

const open = defineModel<boolean>("open", { required: true });

defineProps<{ title: string; label: string; value: string; saveHint: string }>();
</script>

<template>
  <Dialog v-model:open="open">
    <DialogContent class="sm:max-w-lg">
      <DialogHeader>
        <DialogTitle>{{ title }}</DialogTitle>
        <DialogDescription>
          密钥已填入表单，但尚未保存到服务器。请先在安全位置备份，关闭弹窗后{{ saveHint }}
        </DialogDescription>
      </DialogHeader>
      <div class="grid gap-2">
        <Label>{{ label }}</Label>
        <div class="flex min-w-0 gap-2">
          <Input :model-value="value" readonly class="min-w-0 flex-1 font-mono" />
          <CopyButton :value="value" label="复制" size="default" class="shrink-0" />
        </div>
      </div>
      <DialogFooter>
        <Button type="button" @click="open = false">我已妥善保存</Button>
      </DialogFooter>
    </DialogContent>
  </Dialog>
</template>
