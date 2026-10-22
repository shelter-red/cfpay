<script setup lang="ts">
import { Button } from "@/components/ui/button";
import {
  Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle,
} from "@/components/ui/dialog";
import { Separator } from "@/components/ui/separator";
import {
  Table, TableBody, TableCell, TableHead, TableHeader, TableRow,
} from "@/components/ui/table";
import CopyButton from "@/components/admin/CopyButton.vue";
import type { DetailSection } from "@/lib/detail";
import { formatDateTime } from "@/lib/format";

const open = defineModel<boolean>("open", { required: true });

defineProps<{ title: string; sections: DetailSection[] }>();
</script>

<template>
  <Dialog v-model:open="open">
    <DialogContent class="max-h-[85vh] w-[calc(100vw-2rem)] max-w-[calc(100vw-2rem)] gap-0 overflow-hidden sm:max-w-3xl">
      <DialogHeader>
        <DialogTitle class="break-all">{{ title }}</DialogTitle>
        <DialogDescription class="sr-only">查看完整字段、上报 JSON 和回调记录。</DialogDescription>
      </DialogHeader>
      <Separator />
      <div class="-mr-2 max-h-[65vh] space-y-6 overflow-y-auto pr-2">
        <section v-for="(section, index) in sections" :key="`${section.title}-${index}`" class="grid gap-2">
          <h3 class="text-sm font-semibold">{{ section.title }}</h3>

          <div v-if="section.kind === 'table'" class="min-w-0 overflow-x-auto rounded-lg border">
            <Table>
              <TableBody>
                <TableRow v-for="row in section.rows" :key="row[0]">
                  <TableCell class="w-44 align-top text-muted-foreground">{{ row[0] }}</TableCell>
                  <TableCell class="break-all align-top font-mono text-xs">
                    {{ row[1] === null || row[1] === undefined || row[1] === "" ? "-" : row[1] }}
                  </TableCell>
                </TableRow>
              </TableBody>
            </Table>
          </div>

          <p v-else-if="section.kind === 'text'" class="text-sm text-muted-foreground">{{ section.text }}</p>

          <div v-else-if="section.kind === 'raw'" class="grid gap-2">
            <div class="flex min-w-0 flex-wrap items-center gap-3">
              <CopyButton :value="section.raw" />
              <span class="text-xs text-muted-foreground">{{ section.raw.length }} 字符</span>
            </div>
            <pre
              class="max-h-72 overflow-auto rounded-lg border bg-muted/40 p-3 font-mono text-xs whitespace-pre-wrap break-all"
              >{{ section.raw }}</pre
            >
          </div>

          <div v-else class="min-w-0 overflow-x-auto rounded-lg border">
            <Table>
              <TableHeader>
                <TableRow>
                  <TableHead class="w-16">次数</TableHead>
                  <TableHead class="w-20">HTTP</TableHead>
                  <TableHead>响应 / 错误</TableHead>
                  <TableHead class="w-44">时间</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                <TableRow v-for="attempt in section.attempts" :key="attempt.attempt_no">
                  <TableCell>{{ attempt.attempt_no }}</TableCell>
                  <TableCell>{{ attempt.response_status ?? "-" }}</TableCell>
                  <TableCell class="break-all font-mono text-xs">
                    {{ attempt.error || attempt.response_excerpt || "-" }}
                  </TableCell>
                  <TableCell class="text-xs text-muted-foreground">{{ formatDateTime(attempt.created_at) }}</TableCell>
                </TableRow>
              </TableBody>
            </Table>
          </div>
        </section>
      </div>
      <div class="flex justify-end border-t pt-4">
        <Button type="button" variant="outline" @click="open = false">关闭</Button>
      </div>
    </DialogContent>
  </Dialog>
</template>
